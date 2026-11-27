/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Boot menu for the dual-boot image (Linux default, Android optional).
 *
 * Runs in the shim before either kernel, with the MMU and caches off. LK
 * leaves its video-mode display pipeline running: OVL0_2L scans layers 0 and
 * 1 (RGBA8888, 1080x2400) out of RAM, so whatever we write there shows up.
 * The menu is drawn into both layers (layer 1 is on top) using colours that
 * look the same in RGBA and BGRA order.
 *
 * Input is Volume Down only (GPIO 54, active low): it is one register read,
 * while Volume Up and Power sit behind the PMIC. Each press switches the
 * highlighted entry and restarts the countdown; after MENU_SECONDS without a
 * press the highlighted entry boots. Holding Volume Down from power-on still
 * boots Android straight away. If the display registers don't look right, it
 * falls back to that key-only rule without drawing.
 *
 * Returns 1 for Android, 0 for Linux.
 */
#include <stdbool.h>
#include <stdint.h>
#include "menu-font.h"

#ifndef CMF_KEY_ADDR
#define CMF_KEY_ADDR 0x10005210UL      /* GPIO DIN bank 1, Volume Down = bit 22 */
#endif
#ifndef CMF_OVL_BASE
#define CMF_OVL_BASE 0x14002000UL      /* OVL0_2L; QEMU tests override both */
#endif

#define MENU_SECONDS 3
#define WIDTH 1080
#define HEIGHT 2400

#define WHITE   0xffffffffU
#define BLACK   0xff000000U            /* opaque if alpha is the top byte */
#define MAGENTA 0xffff00ffU            /* same colour in RGBA and BGRA */

#define PANEL_X 60
#define PANEL_Y 1440
#define PANEL_W 960
#define PANEL_H 700

struct layer {
    uintptr_t addr;
    uint32_t pitch;
};

/* On the stack: the shim has no zeroed .bss. */
struct screen {
    struct layer layers[2];
    int count;
};

static uint32_t reg(uint32_t offset)
{
    return *(volatile uint32_t *)((uintptr_t)CMF_OVL_BASE + offset);
}

/* Layer n: CON 0x30, SRC_SIZE 0x38, PITCH 0x44 (stride 0x20), ADDR 0xf40. */
static bool layer_usable(int n, struct layer *out)
{
    uint32_t size = reg(0x038 + 0x20 * n), pitch = reg(0x044 + 0x20 * n) & 0xffff;
    uint32_t addr = reg(0xf40 + 0x20 * n);

    if (!(reg(0x02c) & (1U << n)) || (size & 0xffff) != WIDTH || (size >> 16) != HEIGHT ||
        pitch < WIDTH * 4 || pitch > 8192 || (pitch & 3) || addr < 0x40000000U || (addr & 0xfff))
        return false;
    out->addr = addr;
    out->pitch = pitch;
    return true;
}

static void fill(const struct screen *sc, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t colour)
{
    for (int l = 0; l < sc->count; l++)
        for (uint32_t row = y; row < y + h; row++) {
            volatile uint32_t *p = (volatile uint32_t *)(sc->layers[l].addr +
                                   row * sc->layers[l].pitch);

            for (uint32_t col = x; col < x + w; col++)
                p[col] = colour;
        }
}

/* Draw text at (x, y), each font pixel as a scale x scale block. */
static void text(const struct screen *sc, uint32_t x, uint32_t y, uint32_t scale, const char *s,
                 uint32_t fg, uint32_t bg)
{
    for (; *s; s++, x += 8 * scale) {
        unsigned int c = (unsigned char)*s;
        const unsigned char *glyph = menu_font[(c >= 32 && c < 127 ? c : '?') - 32];

        for (int l = 0; l < sc->count; l++)
            for (uint32_t gy = 0; gy < 16 * scale; gy++) {
                volatile uint32_t *p = (volatile uint32_t *)(sc->layers[l].addr +
                                       (y + gy) * sc->layers[l].pitch) + x;
                unsigned char bits = glyph[gy / scale];

                for (uint32_t gx = 0; gx < 8 * scale; gx++)
                    p[gx] = (bits & (0x80 >> (gx / scale))) ? fg : bg;
            }
    }
}

static uint32_t centre(const char *s, uint32_t scale)
{
    uint32_t n = 0;

    while (s[n])
        n++;
    return (WIDTH - n * 8 * scale) / 2;
}

static uint64_t now(void)
{
    uint64_t v;

    __asm__ volatile("isb; mrs %0, cntvct_el0" : "=r"(v));
    return v;
}

static uint64_t frequency(void)
{
    uint64_t f;

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(f));
    return f ? f : 13000000;            /* MediaTek system counter */
}

static bool held(void)
{
    return !((*(volatile uint32_t *)CMF_KEY_ADDR >> 22) & 1);
}

static void wait_ticks(uint64_t ticks)
{
    uint64_t start = now();

    while (now() - start < ticks)
        ;
}

static void draw_entries(const struct screen *sc, int selected)
{
    /* char arrays, not pointers: the helper runs unrelocated */
    static const char names[2][8] = { "Linux", "Android" };

    for (int i = 0; i < 2; i++) {
        uint32_t y = PANEL_Y + 170 + i * 140;
        uint32_t bg = i == selected ? MAGENTA : BLACK;

        fill(sc, PANEL_X + 40, y - 20, PANEL_W - 80, 120, bg);
        text(sc, centre(names[i], 5), y, 5, names[i], WHITE, bg);
    }
}

static void draw_countdown(const struct screen *sc, int selected, unsigned int seconds)
{
    char line[] = "Linux in 0 s";
    char line2[] = "Android in 0 s";
    char *s = selected ? line2 : line;
    char *digit = s;

    while (*digit != '0')
        digit++;
    *digit = (char)('0' + seconds);
    fill(sc, PANEL_X, PANEL_Y + 500, PANEL_W, 80, BLACK);
    text(sc, centre(s, 4), PANEL_Y + 510, 4, s, WHITE, BLACK);
}

int cmf_boot_menu(void)
{
    const uint64_t f = frequency();

    struct screen screen = { .count = 0 };
    const struct screen *sc = &screen;

    for (int n = 0; n < 2; n++)
        if (layer_usable(n, &screen.layers[screen.count]))
            screen.count++;

    /* Volume Down held from power-on: Android, as before the menu existed. */
    if (held()) {
        for (int i = 0; i < 8; i++) {
            if (!held())
                goto menu;
            wait_ticks(f / 200);
        }
        return 1;
    }
menu:
    if (!screen.count)
        return 0;

    int selected = 0;
    unsigned int shown = MENU_SECONDS;
    uint64_t deadline = now() + MENU_SECONDS * f;
    bool was_held = held();

    fill(sc, PANEL_X, PANEL_Y, PANEL_W, PANEL_H, BLACK);
    text(sc, centre("Start which system?", 4), PANEL_Y + 50, 4, "Start which system?", WHITE, BLACK);
    draw_entries(sc, selected);
    text(sc, centre("Volume Down: switch", 3), PANEL_Y + 620, 3, "Volume Down: switch", WHITE, BLACK);
    draw_countdown(sc, selected, shown);

    for (;;) {
        uint64_t t = now();

        if (t >= deadline)
            break;
        unsigned int left = (unsigned int)((deadline - t + f - 1) / f);

        if (left != shown) {
            shown = left;
            draw_countdown(sc, selected, shown);
        }

        /* A press is "held" on two reads 20 ms apart after a release. */
        bool is_held = held();

        if (is_held && !was_held) {
            wait_ticks(f / 50);
            if (held()) {
                selected ^= 1;
                draw_entries(sc, selected);
                deadline = now() + MENU_SECONDS * f;
                shown = MENU_SECONDS;
                draw_countdown(sc, selected, shown);
            }
        }
        was_held = is_held;
        wait_ticks(f / 100);
    }

    /* Leave the screen to the kernel: the panel area becomes black. */
    fill(sc, PANEL_X, PANEL_Y, PANEL_W, PANEL_H, BLACK);
    return selected;
}
