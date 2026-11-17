/*
 * setfont32.c - Set 16x32 console font on Linux fbcon via KDFONTOP ioctl
 *
 * Compiles to a freestanding static aarch64 binary without libc:
 *   clang -O2 --target=aarch64-linux-gnu -nostdlib -static \
 *     -Wl,-n -Wl,--build-id=none setfont32.c -o setfont32
 *   aarch64-linux-gnu-strip -s setfont32
 */

#define KDFONTOP 0x4B72
#define KD_FONT_OP_SET 0

struct console_font_op {
    unsigned int op;
    unsigned int flags;
    unsigned int width, height;
    unsigned int charcount;
    unsigned char *data;
};

#ifdef EMBED_FONT
__asm__(
    ".section .rodata\n"
    ".global font_data_start\n"
    "font_data_start:\n"
    ".incbin \"raw_font.bin\"\n"
    ".global font_data_end\n"
    "font_data_end:\n"
);
extern unsigned char font_data_start[];
extern unsigned char font_data_end[];
#endif

static inline long sys_openat(int dfd, const char *path, int flags, int mode) {
    register long x0 __asm__("x0") = dfd;
    register long x1 __asm__("x1") = (long)path;
    register long x2 __asm__("x2") = flags;
    register long x3 __asm__("x3") = mode;
    register long x8 __asm__("x8") = 56; /* __NR_openat */
    __asm__ volatile("svc #0" : "=r"(x0) : "r"(x0), "r"(x1), "r"(x2), "r"(x3), "r"(x8) : "memory");
    return x0;
}

static inline long sys_ioctl(int fd, unsigned long req, void *arg) {
    register long x0 __asm__("x0") = fd;
    register long x1 __asm__("x1") = (long)req;
    register long x2 __asm__("x2") = (long)arg;
    register long x8 __asm__("x8") = 29; /* __NR_ioctl */
    __asm__ volatile("svc #0" : "=r"(x0) : "r"(x0), "r"(x1), "r"(x2), "r"(x8) : "memory");
    return x0;
}

static inline long sys_write(int fd, const void *buf, unsigned long count) {
    register long x0 __asm__("x0") = fd;
    register long x1 __asm__("x1") = (long)buf;
    register long x2 __asm__("x2") = count;
    register long x8 __asm__("x8") = 64; /* __NR_write */
    __asm__ volatile("svc #0" : "=r"(x0) : "r"(x0), "r"(x1), "r"(x2), "r"(x8) : "memory");
    return x0;
}

static inline void sys_exit(int code) {
    register long x0 __asm__("x0") = code;
    register long x8 __asm__("x8") = 93; /* __NR_exit */
    __asm__ volatile("svc #0" : : "r"(x0), "r"(x8) : "memory");
    while (1);
}

static void print(const char *str) {
    unsigned long len = 0;
    while (str[len]) len++;
    sys_write(1, str, len);
}

void _start(void) {
    int fd = sys_openat(-100, "/dev/tty0", 2, 0);
    if (fd < 0) {
        fd = sys_openat(-100, "/dev/tty1", 2, 0);
    }
    if (fd < 0) {
        print("Error: unable to open console tty\n");
        sys_exit(1);
    }

#ifdef EMBED_FONT
    struct console_font_op op;
    op.op = KD_FONT_OP_SET;
    op.flags = 0;
    op.width = 16;
    op.height = 32;
    op.charcount = 256;
    op.data = font_data_start;

    long ret = sys_ioctl(fd, KDFONTOP, &op);
    if (ret < 0) {
        print("Error: ioctl KDFONTOP failed\n");
        sys_exit(2);
    }
    print("Console font set to 16x32.\n");
#endif
    sys_exit(0);
}
