#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/f2fs.h>
#include <linux/fiemap.h>
#include <linux/fs.h>
#include <linux/magic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/sysmacros.h>
#include <unistd.h>

enum { ALIGNMENT = 4096, BATCH = 128, CHUNK = 8 * 1024 * 1024 };

static int reject(const char *reason)
{
    fprintf(stderr, "%s\n", reason);
    return -1;
}

static int failure(const char *operation)
{
    perror(operation);
    return -1;
}

static int require_f2fs(int fd)
{
    struct statfs fs;
    if (fstatfs(fd, &fs) < 0)
        return failure("fstatfs");
    return fs.f_type == F2FS_SUPER_MAGIC ? 0 : reject("F2FS required");
}

static int get_flags(int fd, unsigned int *flags)
{
    return ioctl(fd, FS_IOC_GETFLAGS, flags) < 0 ? failure("FS_IOC_GETFLAGS") : 0;
}

static int disable_compression(int fd)
{
    unsigned int flags;
    if (get_flags(fd, &flags) < 0)
        return -1;
    if (flags & (FS_COMPR_FL | FS_ENCRYPT_FL))
        return reject("Compressed or fscrypt-encrypted inode is ineligible");
    if (flags & FS_NOCOMP_FL)
        return 0;
    flags |= FS_NOCOMP_FL;
    if (ioctl(fd, FS_IOC_SETFLAGS, &flags) < 0)
        return failure("FS_IOC_SETFLAGS (no compression)");
    if (get_flags(fd, &flags) < 0)
        return -1;
    return flags & FS_NOCOMP_FL ? 0 : reject("No-compression flag did not persist");
}

static int check_pin(int fd, bool immutable)
{
    unsigned int flags;
    uint32_t failures;
    if (ioctl(fd, F2FS_IOC_GET_PIN_FILE, &failures) < 0)
        return failure("F2FS_IOC_GET_PIN_FILE");
    if (get_flags(fd, &flags) < 0)
        return -1;
    /* GET_PIN_FILE reports a GC failure count. Zero also occurs on unpinned files. */
    if (!(flags & FS_NOCOW_FL) || !(flags & FS_NOCOMP_FL) ||
        (flags & (FS_COMPR_FL | FS_ENCRYPT_FL)))
        return reject("File must be pinned, uncompressed and free of fscrypt encoding");
    if (immutable && !(flags & FS_IMMUTABLE_FL))
        return reject("Immutable flag required for a reservation table");
    return 0;
}

static int read_sector_size(dev_t device, unsigned int *size)
{
    const char *suffixes[] = {"queue/logical_block_size", "../queue/logical_block_size"};
    for (size_t i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); i++) {
        char path[128];
        snprintf(path, sizeof(path), "/sys/dev/block/%u:%u/%s",
                 major(device), minor(device), suffixes[i]);
        FILE *fp = fopen(path, "r");
        if (!fp)
            continue;
        int ok = fscanf(fp, "%u", size);
        fclose(fp);
        if (ok == 1 && *size >= 512 && ALIGNMENT % *size == 0)
            return 0;
        return reject("Unsupported logical sector size");
    }
    return reject("Cannot determine filesystem device logical sector size");
}

/* Preserve logical file order, even when physical addresses decrease between runs. */
static int emit_table(int fd, FILE *out, bool immutable)
{
    struct stat st;
    unsigned int sector;
    if (fstat(fd, &st) < 0)
        return failure("fstat");
    if (!S_ISREG(st.st_mode) || st.st_size <= 0 || st.st_size % ALIGNMENT ||
        st.st_nlink != 1)
        return reject("Expected a single-link regular file with a positive aligned size");
    if (require_f2fs(fd) < 0 || check_pin(fd, immutable) < 0 ||
        read_sector_size(st.st_dev, &sector) < 0)
        return -1;
    const size_t bytes = sizeof(struct fiemap) + BATCH * sizeof(struct fiemap_extent);
    struct fiemap *fm = calloc(1, bytes);
    if (!fm)
        return failure("calloc");
    fprintf(out, "# st_size=%" PRIdMAX " st_blocks=%" PRIdMAX
            " logical_block_size=%u inode=%" PRIuMAX "\n",
            (intmax_t)st.st_size, (intmax_t)st.st_blocks, sector, (uintmax_t)st.st_ino);
    uint64_t logical = 0, index = 0;
    const uint64_t size = (uint64_t)st.st_size;
    bool last = false;
    int result = -1;
    while (!last) {
        memset(fm, 0, bytes);
        fm->fm_start = logical;
        fm->fm_length = UINT64_MAX - logical;
        fm->fm_flags = FIEMAP_FLAG_SYNC;
        fm->fm_extent_count = BATCH;
        if (ioctl(fd, FS_IOC_FIEMAP, fm) < 0) {
            failure("FS_IOC_FIEMAP");
            goto done;
        }
        if (!fm->fm_mapped_extents || fm->fm_mapped_extents > BATCH) {
            reject("Missing or invalid FIEMAP batch");
            goto done;
        }
        for (unsigned int i = 0; i < fm->fm_mapped_extents; i++) {
            const struct fiemap_extent *e = &fm->fm_extents[i];
            uint64_t physical = e->fe_physical, length = e->fe_length;
            last = (e->fe_flags & FIEMAP_EXTENT_LAST) != 0;
            if (e->fe_flags & ~(FIEMAP_EXTENT_MERGED | FIEMAP_EXTENT_LAST)) {
                fprintf(stderr, "Rejected FIEMAP flags 0x%x at byte %" PRIu64 "\n",
                        e->fe_flags, logical);
                goto done;
            }
            if (e->fe_logical != logical || !length || length % ALIGNMENT ||
                physical % ALIGNMENT || physical > UINT64_MAX - length ||
                length > size - logical || (last && i + 1 != fm->fm_mapped_extents)) {
                reject("FIEMAP contains a hole, overlap in logical offsets, or invalid extent");
                goto done;
            }
            fprintf(out, "%" PRIu64 " %" PRIu64 " %" PRIu64 " 0x%x\n",
                    index++, physical, length, e->fe_flags);
            logical += length;
        }
    }
    if (logical != size)
        reject("FIEMAP does not cover the entire file");
    else if (ferror(out))
        failure("table output");
    else
        result = check_pin(fd, immutable);
done:
    free(fm);
    return result;
}

static int fill_file(int fd, off_t bytes)
{
    static const unsigned char zero[CHUNK];
    uint32_t pin = 1;
    if (disable_compression(fd) < 0)
        return -1;
    if (ioctl(fd, F2FS_IOC_SET_PIN_FILE, &pin) < 0)
        return failure("F2FS_IOC_SET_PIN_FILE");
    if (check_pin(fd, false) < 0)
        return -1;
    if (fallocate(fd, 0, 0, bytes) < 0)
        return failure("fallocate");
    for (off_t left = bytes; left > 0;) {
        size_t count = left > CHUNK ? CHUNK : (size_t)left;
        ssize_t n = write(fd, zero, count);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return failure("zero fill");
        left -= n;
    }
    if (fsync(fd) < 0)
        return failure("fsync reservation");
    return check_pin(fd, false);
}

static int create_file(char *path, const char *size_text)
{
    char *end;
    errno = 0;
    uintmax_t bytes = strtoumax(size_text, &end, 10);
    if (!isdigit((unsigned char)*size_text) || *end || errno || !bytes ||
        bytes > INT64_MAX || bytes % ALIGNMENT)
        return reject("Size must be a positive 4096-byte multiple in decimal");
    char *slash = strrchr(path, '/');
    if (!slash || slash == path || !slash[1] || !strcmp(slash + 1, ".") ||
        !strcmp(slash + 1, "..") || geteuid() != 0)
        return reject("Create requires root and a file in a dedicated directory");
    *slash = '\0';
    const char *name = slash + 1;
    /* Check the containing filesystem before creating even the directory. */
    char *outer = strrchr(path, '/');
    if (outer)
        *outer = '\0';
    int base = open(outer ? (*path ? path : "/") : ".", O_DIRECTORY | O_NOFOLLOW | O_RDONLY);
    if (outer)
        *outer = '/';
    if (base < 0)
        return failure("open containing directory");
    const char *parent = outer ? outer + 1 : path;
    int result = -1, dir = -1, fd = -1;
    if (!*parent || !strcmp(parent, ".") || !strcmp(parent, "..") || require_f2fs(base) < 0)
        goto done;
    if (mkdirat(base, parent, 0700) < 0 && errno != EEXIST) {
        failure("mkdir reservation directory");
        goto done;
    }
    dir = openat(base, parent, O_DIRECTORY | O_NOFOLLOW | O_RDONLY);
    if (dir < 0) {
        failure("open reservation directory");
        goto done;
    }
    struct stat st;
    if (fstat(dir, &st) < 0 || st.st_uid != 0 || (st.st_mode & 0777) != 0700 ||
        require_f2fs(dir) < 0) {
        reject("Expected a root-owned mode 0700 F2FS directory");
        goto done;
    }
    if (flock(dir, LOCK_EX | LOCK_NB) < 0) {
        failure("lock reservation directory");
        goto done;
    }
    if (fstatat(dir, name, &st, AT_SYMLINK_NOFOLLOW) == 0 || errno != ENOENT) {
        reject("Reservation name already exists or cannot be checked");
        goto done;
    }
    if (disable_compression(dir) < 0)
        goto done;
    fd = openat(dir, name, O_CREAT | O_EXCL | O_RDWR | O_NOFOLLOW, 0600);
    if (fd < 0) {
        failure("create reservation");
        goto done;
    }
    result = fill_file(fd, (off_t)bytes);
    if (close(fd) < 0)
        result = failure("close reservation");
    if (result < 0 && unlinkat(dir, name, 0) < 0)
        failure("remove incomplete reservation");
    if (fsync(dir) < 0 || fsync(base) < 0)
        result = failure("fsync reservation directory");
done:
    if (dir >= 0)
        close(dir);
    close(base);
    return result;
}

static int map_or_check(const char *path, const char *saved)
{
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0)
        return failure("open reservation");
    char *table = NULL;
    size_t length = 0;
    FILE *buffer = open_memstream(&table, &length);
    if (!buffer) {
        close(fd);
        return failure("open_memstream");
    }
    int result = emit_table(fd, buffer, true);
    if (fclose(buffer) < 0)
        result = failure("finish table");
    close(fd);
    if (result == 0 && saved) {
        FILE *input = fopen(saved, "rb");
        if (!input)
            result = failure("open reference table");
        else {
            for (size_t i = 0; i < length; i++) {
                if (fgetc(input) != (unsigned char)table[i]) {
                    result = -1;
                    break;
                }
            }
            if (fgetc(input) != EOF || ferror(input))
                result = -1;
            fclose(input);
        }
    } else if (result == 0 && (fwrite(table, 1, length, stdout) != length || fflush(stdout))) {
        result = failure("write table");
    }
    free(table);
    return result;
}

int main(int argc, char **argv)
{
    if (argc == 4 && !strcmp(argv[1], "create"))
        return create_file(argv[2], argv[3]) == 0 ? 0 : 1;
    if (argc == 3 && !strcmp(argv[1], "map"))
        return map_or_check(argv[2], NULL) == 0 ? 0 : 1;
    if (argc == 4 && !strcmp(argv[1], "check")) {
        int result = map_or_check(argv[2], argv[3]);
        puts(result == 0 ? "MATCH" : "MISMATCH");
        return result == 0 ? 0 : 1;
    }
    fprintf(stderr, "Usage: cmf-reserve create <path> <bytes> | map <path> | check <path> <table>\n");
    return 2;
}
