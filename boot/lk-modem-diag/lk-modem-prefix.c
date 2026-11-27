/* SPDX-License-Identifier: GPL-2.0-only */
#include <libfdt.h>

#define FDT_LIMIT (2U * 1024U * 1024U)
#define PREFIX_SIZE 32
#define RESULT_SIZE 40

struct modem_receipt {
    char magic[4];
    fdt32_t record_size;
    uint8_t prefix[PREFIX_SIZE];
};

_Static_assert(sizeof(struct modem_receipt) == RESULT_SIZE, "receipt layout");

static uint64_t read_be64(const fdt32_t *cells)
{
    return ((uint64_t)fdt32_to_cpu(cells[0]) << 32) | fdt32_to_cpu(cells[1]);
}

static uint64_t ram_available(const void *tree, uintptr_t address)
{
    int node;

    if (fdt_address_cells(tree, 0) != 2 || fdt_size_cells(tree, 0) != 2)
        return 0;
    fdt_for_each_subnode(node, tree, 0) {
        int length;
        const char *type = fdt_getprop(tree, node, "device_type", &length);

        if (length != sizeof("memory") || memcmp(type, "memory", length))
            continue;
        const fdt32_t *reg = fdt_getprop(tree, node, "reg", &length);

        for (int i = 0; reg && i + 16 <= length; i += 16, reg += 4) {
            uint64_t base = read_be64(reg);
            uint64_t size = read_be64(reg + 2);

            if (address >= base && address - base < size)
                return size - (address - base);
        }
    }
    return 0;
}

static const char *inspect(const void *source, const void *target, struct modem_receipt *receipt)
{
    uintptr_t address = (uintptr_t)source;
    uint64_t available = ram_available(target, address);

    if ((address & 7) || available < sizeof(struct fdt_header))
        return "lk-fdt-address-unavailable";
    if (fdt_check_header(source))
        return "lk-fdt-bad-header";
    uint32_t size = fdt_totalsize(source);

    if (size > FDT_LIMIT || size > available)
        return "lk-fdt-invalid";

    static const char paths[][16] = { "/mddriver", "/soc/mddriver", "/chosen" };
    const uint8_t *record = NULL;
    int length;

    for (unsigned int i = 0; i < sizeof(paths) / sizeof(*paths); i++) {
        int node = fdt_path_offset(source, paths[i]);

        if (node >= 0)
            record = fdt_getprop(source, node, "ccci,modem_info_v2", &length);
        if (record)
            break;
    }
    if (!record)
        return "modem-record-missing";
    if (length < PREFIX_SIZE)
        return "modem-record-too-short";

    memcpy(receipt->magic, "CCCI", sizeof(receipt->magic));
    receipt->record_size = cpu_to_fdt32(length);
    memcpy(receipt->prefix, record, sizeof(receipt->prefix));
    return NULL;
}

void cmf_lk_modem_diag(const void *source, void *target)
{
    int chosen = fdt_path_offset(target, "/chosen");
    struct modem_receipt receipt = { 0 };

    if (chosen < 0)
        return;
    const char *error = inspect(source, target, &receipt);

    if (error)
        memcpy(&receipt, error, strlen(error));
    fdt_setprop_inplace(target, chosen, "cmf,lk-modem-diagnostic", &receipt,
                       sizeof(receipt));
}
