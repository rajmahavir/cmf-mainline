/* Stage 95c QEMU fixture: ext4 shutdown with log flush (like xfstests godown).
 * Leaves committed transactions in the journal without checkpointing them,
 * so the next mount must replay the journal. */
#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <stdint.h>

#define EXT4_IOC_SHUTDOWN _IOR('X', 125, uint32_t)
#define EXT4_GOING_FLAGS_LOGFLUSH 0x1

int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    int fd = open(argv[1], O_RDONLY | O_DIRECTORY);
    if (fd < 0) { perror("open"); return 1; }
    uint32_t flags = EXT4_GOING_FLAGS_LOGFLUSH;
    if (ioctl(fd, EXT4_IOC_SHUTDOWN, &flags)) { perror("EXT4_IOC_SHUTDOWN"); return 1; }
    return 0;
}
