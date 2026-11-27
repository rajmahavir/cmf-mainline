// SPDX-License-Identifier: GPL-2.0-only
/*
 * Host test for the RAM copy in lk-modem-diag.c. Build with the helper
 * (-DCMF_MODEM_CONTROL), host libfdt (fdt.c fdt_ro.c fdt_wip.c fdt_rw.c
 * fdt_addresses.c) and run with the device's DTB:  ./test-lk-memory cmf.dtb
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <libfdt.h>

#define CAPACITY (128 * 1024)
#define GIB (1024ULL * 1024ULL * 1024ULL)

void cmf_lk_modem_diag(const void *source, void *target);

static _Alignas(8) unsigned char base[CAPACITY], source[CAPACITY], target[CAPACITY];

static void bank(fdt32_t *cells, uint64_t start, uint64_t size)
{
    cells[0] = cpu_to_fdt32(start >> 32);
    cells[1] = cpu_to_fdt32(start);
    cells[2] = cpu_to_fdt32(size >> 32);
    cells[3] = cpu_to_fdt32(size);
}

static int memory(void *tree)
{
    int node;

    fdt_for_each_subnode(node, tree, 0) {
        int length;
        const char *type = fdt_getprop(tree, node, "device_type", &length);

        if (type && !strcmp(type, "memory"))
            return node;
    }
    return -1;
}

/* source gets the given LK memory banks; target's memory node is set so the
 * helper accepts the (host) address of source, like on the phone. */
static void prepare(const fdt32_t *banks, int count)
{
    assert(!fdt_open_into(base, source, sizeof(source)));
    assert(!fdt_open_into(base, target, 0xF000));
    int node = memory(source);
    assert(node >= 0);
    if (count < 0)
        assert(!fdt_del_node(source, node));
    else
        assert(!fdt_setprop(source, node, "reg", banks, 16 * count));

    fdt32_t host[4];
    bank(host, (uintptr_t)source, sizeof(source));
    node = memory(target);
    assert(!fdt_setprop(target, node, "reg", host, sizeof(host)));
}

static void expect(const void *incoming, const char *status, const fdt32_t *banks, int count)
{
    fdt32_t before[4];
    int length, node = memory(target);
    memcpy(before, fdt_getprop(target, node, "reg", &length), sizeof(before));

    cmf_lk_modem_diag(incoming, target);
    assert(!fdt_check_header(target));
    int chosen = fdt_path_offset(target, "/chosen");
    const char *got = fdt_getprop(target, chosen, "cmf,lk-memory", &length);
    assert(got && !strcmp(got, status));
    node = memory(target);
    const fdt32_t *reg = fdt_getprop(target, node, "reg", &length);
    if (!strcmp(status, "lk")) {
        assert(length == 16 * count && !memcmp(reg, banks, length));
        const fdt32_t *raw = fdt_getprop(target, chosen, "cmf,lk-memory-reg", &length);
        assert(raw && length == 16 * count && !memcmp(raw, banks, length));
    } else {
        assert(length == 16 && !memcmp(reg, before, 16));   /* built-in node kept */
    }
    printf("ok  %-28s %d bank(s)\n", status, count);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    FILE *file = fopen(argv[1], "rb");
    assert(file);
    size_t size = fread(base, 1, sizeof(base), file);
    assert(!ferror(file) && !fclose(file) && !fdt_check_header(base) && size == fdt_totalsize(base));

    fdt32_t b[8];
    bank(b, 0x40000000ULL, 6 * GIB);
    prepare(b, 1);
    expect(source, "lk", b, 1);                                   /* 6 GB model */

    bank(b, 0x40000000ULL, 8 * GIB);
    prepare(b, 1);
    expect(source, "lk", b, 1);                                   /* 8 GB model */

    bank(b, 0x40000000ULL, 3 * GIB);
    bank(b + 4, 0x140000000ULL, 3 * GIB);
    prepare(b, 2);
    expect(source, "lk", b, 2);                                   /* split ranges */

    prepare(NULL, -1);
    expect(source, "kept:lk-memory-missing", NULL, 0);

    bank(b, 0x40000000ULL, 1 * GIB);
    prepare(b, 1);
    expect(source, "kept:lk-memory-implausible", NULL, 0);

    bank(b, 0x40000000ULL, 6 * GIB);
    prepare(b, 1);
    expect(NULL, "kept:lk-fdt-unusable", NULL, 0);
    puts("all memory cases passed");
    return 0;
}
