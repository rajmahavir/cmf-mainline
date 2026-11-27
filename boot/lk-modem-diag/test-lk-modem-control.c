// SPDX-License-Identifier: GPL-2.0-only
/* Link with lk-modem-diag.c (-DCMF_MODEM_CONTROL) and host libfdt. */
#include <assert.h>
#include <stdio.h>
#include <libfdt.h>

/* build the helper with -DCMF_HOST_TEST: it reads this instead of the GPIO */
volatile uint32_t cmf_test_din = 0x0043C800;
volatile uint32_t cmf_test_ovl[0x1000 / 4];   /* display registers: all 0, nothing drawn */

#define MODEM_PATH "/soc@0/mddriver@d180000"
#define RECORD_PROPERTY "ccci,modem_info_v2"
#define CAPACITY (128 * 1024)

void cmf_lk_modem_diag(const void *source, void *target);

static _Alignas(8) unsigned char base[CAPACITY], source[CAPACITY], target[CAPACITY];

/* 48-byte LK record with the observed CMF fields and a distinct extension. */
static const uint8_t record[48] = {
    0x00, 0x00, 0xdf, 0xbf, 0, 0, 0, 0,
    0x18, 0x18, 0, 0, 0, 0, 0, 0,
    3, 0, 0, 0, 28, 0, 0, 0,
    1, 0, 0, 0, 0, 0, 0, 0,
    0xa5,
};

static void prepare(void)
{
    assert(!fdt_open_into(base, source, sizeof(source)));
    assert(!fdt_open_into(base, target, sizeof(target)));
    int modem = fdt_path_offset(source, MODEM_PATH);
    assert(modem >= 0);
    assert(!fdt_setprop(source, modem, RECORD_PROPERTY, record, sizeof(record)));

    uintptr_t address = (uintptr_t)source;
    const fdt32_t bank[] = {
        cpu_to_fdt32(address >> 32), cpu_to_fdt32(address),
        0, cpu_to_fdt32(sizeof(source)),
    };
    int memory = fdt_path_offset(target, "/memory");
    assert(memory >= 0);
    assert(!fdt_setprop(target, memory, "reg", bank, sizeof(bank)));
}

static void expect(const void *incoming, const char *result, uint32_t size, bool enabled)
{
    cmf_lk_modem_diag(incoming, target);
    int length;
    int chosen = fdt_path_offset(target, "/chosen");
    assert(chosen >= 0);
    const char *status = fdt_getprop(target, chosen, "cmf,lk-modem-diagnostic", &length);
    assert(length == 40 && !strcmp(status, result));
    const fdt32_t *record_size = fdt_getprop(target, chosen, "cmf,lk-modem-record-length", &length);
    assert(length == 4 && fdt32_to_cpu(*record_size) == size);

    int modem = fdt_path_offset(target, MODEM_PATH);
    assert(modem >= 0);
    status = fdt_getprop(target, modem, "status", &length);
    assert(length == 5 && !strcmp(status, enabled ? "okay" : "fail"));
    const void *forwarded = fdt_getprop(target, modem, RECORD_PROPERTY, &length);
    const uint8_t empty[32] = { 0 };
    assert(length == sizeof(empty));
    assert(!memcmp(forwarded, enabled ? record : empty, length));
    puts(result);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    FILE *file = fopen(argv[1], "rb");
    assert(file);
    size_t size = fread(base, 1, sizeof(base), file);
    assert(!ferror(file) && !fclose(file));
    assert(size >= sizeof(struct fdt_header) && !fdt_check_header(base));
    assert(size == fdt_totalsize(base));

    prepare();
    expect(source, "modem-control-record-forwarded", 48, true);

    prepare();
    expect(NULL, "lk-fdt-address-unavailable", 0, false);

    prepare();
    int modem = fdt_path_offset(source, MODEM_PATH);
    int length;
    uint8_t *incoming = fdt_getprop_w(source, modem, RECORD_PROPERTY, &length);
    assert(length == sizeof(record));
    incoming[24] = 0;
    expect(source, "modem-not-loaded", 48, false);

    prepare();
    int shared = fdt_node_offset_by_compatible(source, -1, "mediatek,ap_md_nc_smem");
    assert(shared >= 0 && !fdt_nop_property(source, shared, "no-map"));
    expect(source, "modem-reservations-differ", 48, false);

    prepare();
    modem = fdt_path_offset(source, MODEM_PATH);
    assert(!fdt_setprop(source, modem, RECORD_PROPERTY, record, 31));
    expect(source, "modem-record-size-invalid", 31, false);

    prepare();
    modem = fdt_path_offset(target, MODEM_PATH);
    assert(!fdt_setprop_string(target, modem, "compatible", "test,wrong-device"));
    expect(source, "modem-control-target-invalid", 48, false);
    return 0;
}
