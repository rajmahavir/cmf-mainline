// Linux device-mapper helper for the CMF reservation.
#define _GNU_SOURCE
#include <endian.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/dm-ioctl.h>
#include <linux/fs.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

enum {
    SECTOR_BYTES = 512, SEGMENT_SECTORS = 4096, STAMP_SECTORS = 8,
    STAMP_BYTES = STAMP_SECTORS * SECTOR_BYTES, MAX_SEGMENTS = 12800,
    ID_BYTES = 16, SHA_BYTES = 32, HEADER_BYTES = 68,
    TARGET_BYTES = 96,
};
static const char mapper[] = "/dev/mapper/cmf-root";

struct area {
    unsigned char id[ID_BYTES], sha[SHA_BYTES];
    uint64_t sectors;
    uint32_t count;
    uint64_t starts[MAX_SEGMENTS];
};

static uint64_t get_le(const unsigned char *src, size_t bytes)
{
    uint64_t number = 0;
    for (size_t i = 0; i < bytes; i++)
        number |= (uint64_t)src[i] << (8 * i);
    return number;
}

static void put_le(unsigned char *dst, uint64_t number, size_t bytes)
{
    for (size_t i = 0; i < bytes; i++)
        dst[i] = number >> (8 * i);
}

static uint32_t crc32(const unsigned char *data, size_t bytes)
{
    uint32_t crc = UINT32_MAX;
    while (bytes--) {
        crc ^= *data++;
        for (int bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ ((crc & 1) ? UINT32_C(0xedb88320) : 0);
    }
    return ~crc;
}

static void load_area(struct area *area, const char *path)
{
    unsigned char raw[HEADER_BYTES + 8 * MAX_SEGMENTS + 4 + 1];
    FILE *file = fopen(path, "rb");
    if (!file)
        err(1, "%s", path);
    size_t bytes = fread(raw, 1, sizeof(raw), file);
    if (ferror(file))
        err(1, "%s", path);
    fclose(file);

    if (bytes < HEADER_BYTES + 4 || memcmp(raw, "CMFTBL01", 8))
        errx(1, "invalid area header");
    memcpy(area->id, raw + 8, ID_BYTES);
    memcpy(area->sha, raw + 24, SHA_BYTES);
    area->sectors = get_le(raw + 56, 8);
    area->count = get_le(raw + 64, 4);
    if (area->count == 0 || area->count > MAX_SEGMENTS ||
        bytes != HEADER_BYTES + 8 * area->count + 4 ||
        get_le(raw + bytes - 4, 4) != crc32(raw, bytes - 4))
        errx(1, "invalid area length or CRC32");

    uint64_t end = 0;
    for (uint32_t i = 0; i < area->count; i++) {
        uint64_t start = get_le(raw + HEADER_BYTES + 8 * i, 8);
        if (start < end || start % SEGMENT_SECTORS ||
            start > area->sectors || area->sectors - start < SEGMENT_SECTORS)
            errx(1, "invalid area bounds at segment %" PRIu32, i);
        area->starts[i] = start;
        end = start + SEGMENT_SECTORS;
    }
}

static int verify(const struct area *area, int fd)
{
    unsigned char actual[STAMP_BYTES], expected[STAMP_BYTES] = "CMFAREA1";
    memcpy(expected + 8, area->id, ID_BYTES);
    memcpy(expected + 44, area->sha, SHA_BYTES);
    put_le(expected + 36, SEGMENT_SECTORS, 8);
    uint32_t bad = 0, first_bad = 0;

    for (uint32_t i = 0; i < area->count; i++) {
        put_le(expected + 24, i, 4);
        put_le(expected + 28, area->starts[i], 8);
        put_le(expected + 76, crc32(expected, 76), 4);
        ssize_t bytes;
        do {
            bytes = pread(fd, actual, sizeof(actual), area->starts[i] * SECTOR_BYTES);
        } while (bytes < 0 && errno == EINTR);
        if (bytes != sizeof(actual) || memcmp(actual, expected, sizeof(actual))) {
            if (bad == 0)
                first_bad = i;
            bad++;
        }
    }

    fprintf(stderr, "cmf-dmarea: stamps ok=%" PRIu32 " bad=%" PRIu32,
            area->count - bad, bad);
    if (bad)
        fprintf(stderr, " first_bad=%" PRIu32, first_bad);
    fputc('\n', stderr);
    return bad != 0;
}

static void init_ioctl(struct dm_ioctl *dm, size_t bytes, const struct area *area)
{
    memset(dm, 0, bytes);
    dm->version[0] = DM_VERSION_MAJOR;
    dm->data_size = bytes;
    dm->data_start = sizeof(*dm);
    if (!area) {
        strcpy(dm->name, "cmf-root");
        return;
    }

    strcpy(dm->uuid, "CMFAREA-");
    for (size_t i = 0; i < ID_BYTES; i++)
        snprintf(dm->uuid + 8 + 2 * i, 3, "%02x", area->id[i]);
    dm->flags = DM_UUID_FLAG;
}

static int remove_mapping(int control, const struct area *area)
{
    struct dm_ioctl dm;
    init_ioctl(&dm, sizeof(dm), area);
    if (ioctl(control, DM_DEV_STATUS, &dm))
        return -1;
    dev_t device = dm.dev;
    init_ioctl(&dm, sizeof(dm), area);
    if (ioctl(control, DM_DEV_REMOVE, &dm))
        return -1;

    struct stat st;
    if (lstat(mapper, &st) == 0 && S_ISBLK(st.st_mode) && st.st_rdev == device &&
        unlink(mapper) && errno != ENOENT)
        return -1;
    return 0;
}

static void up(int control, const struct area *area, dev_t backing, int read_only)
{
    size_t bytes = sizeof(struct dm_ioctl) + area->count * TARGET_BYTES;
    struct dm_ioctl *dm = malloc(bytes);
    if (!dm)
        err(1, "device-mapper table");
    init_ioctl(dm, sizeof(*dm), area);
    dm->flags = 0;
    strcpy(dm->name, "cmf-root");
    if (ioctl(control, DM_DEV_CREATE, dm))
        err(1, "DM_DEV_CREATE");
    dev_t device = dm->dev;

    const char *step = "DM_TABLE_LOAD";
    init_ioctl(dm, bytes, area);
    if (read_only)
        dm->flags |= DM_READONLY_FLAG;
    dm->target_count = area->count;
    for (uint32_t i = 0; i < area->count; i++) {
        struct dm_target_spec *target = (void *)((char *)dm + dm->data_start + i * TARGET_BYTES);
        target->sector_start = (uint64_t)i * (SEGMENT_SECTORS - STAMP_SECTORS);
        target->length = SEGMENT_SECTORS - STAMP_SECTORS;
        target->next = TARGET_BYTES;
        strcpy(target->target_type, "linear");
        char *params = (char *)(target + 1);
        size_t capacity = TARGET_BYTES - sizeof(*target);
        int written = snprintf(params, capacity, "%u:%u %" PRIu64,
                               major(backing), minor(backing), area->starts[i] + STAMP_SECTORS);
        if (written < 0 || (size_t)written >= capacity) {
            errno = EOVERFLOW;
            goto fail;
        }
    }
    if (ioctl(control, DM_TABLE_LOAD, dm))
        goto fail;
    step = "DM_DEV_SUSPEND (resume)";
    init_ioctl(dm, sizeof(*dm), area);
    if (ioctl(control, DM_DEV_SUSPEND, dm))
        goto fail;

    step = "/dev/mapper";
    if (mkdir("/dev/mapper", 0755) && errno != EEXIST)
        goto fail;
    step = mapper;
    if (mknod(mapper, S_IFBLK | 0600, device)) {
        if (errno != EEXIST)
            goto fail;
        struct stat st;
        if (stat(mapper, &st))
            goto fail;
        if (!S_ISBLK(st.st_mode) || st.st_rdev != device) {
            errno = EEXIST;
            goto fail;
        }
    }
    free(dm);
    return;

fail:
    {
        int saved = errno;
        if (remove_mapping(control, area))
            warn("remove incomplete cmf-root mapping");
        free(dm);
        errno = saved;
        err(1, "%s", step);
    }
}

int main(int argc, char **argv)
{
    if (argc < 3)
        errx(2, "usage: cmf-dmarea verify|up area.bin block-device; cmf-dmarea down area.bin");
    int is_up = strcmp(argv[1], "up") == 0;
    int is_down = strcmp(argv[1], "down") == 0;
    if ((is_down && argc != 3) ||
        (!is_down && (argc != 4 || (!is_up && strcmp(argv[1], "verify")))))
        errx(2, "invalid operation or argument count");

    struct area area;
    load_area(&area, argv[2]);

    if (is_down) {
        int control = open("/dev/mapper/control", O_RDWR | O_CLOEXEC);
        if (control < 0 || remove_mapping(control, &area))
            err(1, "remove cmf-root");
        return 0;
    }

    int backing = open(argv[3], O_RDONLY | O_CLOEXEC);
    if (backing < 0)
        err(1, "%s", argv[3]);
    struct stat st;
    if (fstat(backing, &st))
        err(1, "fstat %s", argv[3]);
    if (!S_ISBLK(st.st_mode))
        errx(1, "backing path is not a block device");
    uint64_t size;
    int read_only, logical_block;
    if (ioctl(backing, BLKGETSIZE64, &size) || ioctl(backing, BLKSSZGET, &logical_block) ||
        ioctl(backing, BLKROGET, &read_only))
        err(1, "backing device geometry");
    if (size % SECTOR_BYTES || size / SECTOR_BYTES != area.sectors ||
        logical_block <= 0 || STAMP_BYTES % logical_block)
        errx(1, "unexpected backing device geometry");

    if (verify(&area, backing))
        return 1;
    if (is_up) {
        int control = open("/dev/mapper/control", O_RDWR | O_CLOEXEC);
        if (control < 0)
            err(1, "open /dev/mapper/control");
        up(control, &area, st.st_rdev, read_only);
    }
    return 0;
}
