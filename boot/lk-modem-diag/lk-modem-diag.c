/* SPDX-License-Identifier: GPL-2.0-only */
#include <libfdt.h>

#define FDT_LIMIT (2U * 1024U * 1024U)
#define TAG_LIMIT 0x10000U
#define RESULT_SIZE 40
#define RECORD_PREFIX_SIZE 32

static const char reservations[][24] = {
    "mediatek,ccci_tag_mem",
    "mediatek,ap_md_nc_smem",
    "mediatek,ap_md_c_smem",
    "mediatek,md_mem_usage",
};

static uint64_t read_be64(const fdt32_t *cells)
{
    return ((uint64_t)fdt32_to_cpu(cells[0]) << 32) | fdt32_to_cpu(cells[1]);
}

static uint32_t read_le32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
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

static const fdt32_t *reservation_reg(const void *tree, int node)
{
    int length;

    if (!fdt_getprop(tree, node, "no-map", &length) || length != 0)
        return NULL;
    const fdt32_t *reg = fdt_getprop(tree, node, "reg", &length);

    return length == 16 && read_be64(reg + 2) ? reg : NULL;
}

static bool matching_reservation(const void *tree, int parent,
                                 const char *compatible, const fdt32_t *reg)
{
    int node;

    fdt_for_each_subnode(node, tree, parent) {
        if (fdt_node_check_compatible(tree, node, compatible))
            continue;
        const fdt32_t *other = reservation_reg(tree, node);

        if (other && !memcmp(reg, other, 16))
            return true;
    }
    return false;
}

static bool matching_reservations(const void *source, const void *target,
                                  uint64_t tags_base, uint32_t tags_size)
{
    int from = fdt_path_offset(source, "/reserved-memory");
    int to = fdt_path_offset(target, "/reserved-memory");
    bool tags_inside = false;

    if (from < 0 || to < 0 || fdt_address_cells(source, from) != 2 ||
        fdt_size_cells(source, from) != 2 || fdt_address_cells(target, to) != 2 ||
        fdt_size_cells(target, to) != 2)
        return false;

    for (unsigned int i = 0; i < sizeof(reservations) / sizeof(*reservations); i++) {
        unsigned int from_count = 0, to_count = 0;
        int node;

        fdt_for_each_subnode(node, source, from) {
            if (fdt_node_check_compatible(source, node, reservations[i]))
                continue;
            const fdt32_t *reg = reservation_reg(source, node);

            if (!reg || !matching_reservation(target, to, reservations[i], reg))
                return false;
            from_count++;
            if (i == 0) {
                uint64_t base = read_be64(reg), size = read_be64(reg + 2);

                tags_inside = tags_base >= base && tags_size <= size &&
                              tags_base - base <= size - tags_size;
            }
        }
        fdt_for_each_subnode(node, target, to) {
            if (!fdt_node_check_compatible(target, node, reservations[i])) {
                const fdt32_t *reg = reservation_reg(target, node);

                if (!reg || !matching_reservation(source, from, reservations[i], reg))
                    return false;
                to_count++;
            }
        }
        if (!from_count || from_count != to_count || (i < 3 && from_count != 1))
            return false;
    }
    return tags_inside;
}

static const char *inspect(const void *source, const void *target, int *record_size,
                           const uint8_t **validated_record)
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
    *record_size = length;
    if (length < RECORD_PREFIX_SIZE)
        return "modem-record-size-invalid";

    uint64_t base = read_le32(record) | ((uint64_t)read_le32(record + 4) << 32);
    uint32_t used = read_le32(record + 8);

    if (!(read_le32(record + 24) & 1) || read_le32(record + 12) ||
        read_le32(record + 28))
        return "modem-not-loaded";
    if (read_le32(record + 16) < 2 || !base || used < 76 || used > TAG_LIMIT)
        return "modem-record-invalid";
    if (!matching_reservations(source, target, base, used))
        return "modem-reservations-differ";
    *validated_record = record;
    return "modem-record-and-reservations-ok";
}

#ifdef CMF_MODEM_CONTROL
static bool enable_control(void *target, const uint8_t *record)
{
    int node = fdt_path_offset(target, "/soc@0/mddriver@d180000");
    int length;

    if (node < 0 || fdt_node_check_compatible(target, node, "mediatek,mt6878-ccci"))
        return false;
    const char *status = fdt_getprop(target, node, "status", &length);

    if (length != sizeof("fail") || memcmp(status, "fail", sizeof("fail")))
        return false;
    if (fdt_setprop_inplace(target, node, "ccci,modem_info_v2", record,
                            RECORD_PREFIX_SIZE))
        return false;
    return !fdt_setprop_inplace(target, node, "status", "okay", sizeof("okay"));
}
#endif

/*
 * RAM size: the shim replaces LK's device tree with ours, and ours has a fixed
 * memory node (8 GiB). Copy LK's /memory ranges instead, so the 6 GB model
 * (and any other layout) boots with the RAM it really has. The embedded DTB
 * may grow up to the helper code at 0x10000. The outcome is recorded in
 * /chosen/cmf,lk-memory ("lk" or the reason the built-in node was kept) and the
 * raw ranges LK passed in /chosen/cmf,lk-memory-reg.
 */
void cmf_lk_modem_diag_modem(const void *source, void *target);

#define DTB_ROOM (0x10000U - 0x1000U)
#define MAX_BANKS 8
#define GIB (1024ULL * 1024ULL * 1024ULL)

static bool source_usable(const void *source, const void *target)
{
    uintptr_t address = (uintptr_t)source;
    uint64_t available = ram_available(target, address);

    return !(address & 7) && available >= sizeof(struct fdt_header) &&
           !fdt_check_header(source) && fdt_totalsize(source) <= FDT_LIMIT &&
           fdt_totalsize(source) <= available;
}

static int memory_node(const void *tree)
{
    int node;

    fdt_for_each_subnode(node, tree, 0) {
        int length;
        const char *type = fdt_getprop(tree, node, "device_type", &length);

        if (length == sizeof("memory") && !memcmp(type, "memory", length))
            return node;
    }
    return -1;
}

/* Returns the number of banks copied into out (4 cells each), or -1. */
static int lk_memory(const void *source, fdt32_t *out, uint64_t *total)
{
    int node, count = 0;

    *total = 0;
    if (fdt_address_cells(source, 0) != 2 || fdt_size_cells(source, 0) != 2)
        return -1;
    fdt_for_each_subnode(node, source, 0) {
        int length;
        const char *type = fdt_getprop(source, node, "device_type", &length);

        if (length != sizeof("memory") || memcmp(type, "memory", length))
            continue;
        const fdt32_t *reg = fdt_getprop(source, node, "reg", &length);

        if (!reg || length % 16)
            return -1;
        for (int i = 0; i < length; i += 16, reg += 4) {
            uint64_t base = read_be64(reg), size = read_be64(reg + 2);

            if (!size)
                continue;
            if (count == MAX_BANKS || base < 0x40000000ULL || (base | size) & 0xfff ||
                base + size < base)
                return -1;
            memcpy(out + 4 * count, reg, 16);
            *total += size;
            count++;
        }
    }
    return count;
}

static const char *copy_memory(const void *source, void *target)
{
    fdt32_t banks[4 * MAX_BANKS];
    uint64_t total;
    int count, chosen, node;

    if (fdt_open_into(target, target, DTB_ROOM))
        return "kept:dtb-no-room";
    if (!source_usable(source, target))
        return "kept:lk-fdt-unusable";
    count = lk_memory(source, banks, &total);
    chosen = fdt_path_offset(target, "/chosen");
    if (count > 0 && chosen >= 0)
        fdt_setprop(target, chosen, "cmf,lk-memory-reg", banks, 16 * count);
    if (count <= 0)
        return "kept:lk-memory-missing";
    if (total < 2 * GIB || total > 16 * GIB)
        return "kept:lk-memory-implausible";
    node = memory_node(target);
    if (node < 0 || fdt_setprop(target, node, "reg", banks, 16 * count))
        return "kept:dtb-memory-node";
    return "lk";
}

/*
 * Boot key: Volume Down is GPIO 54 (active low), input bit 22 of GPIO DIN
 * bank 1. The raw register is recorded in /chosen/cmf,boot-key-din, so we
 * can see the pin as LK leaves it before using it to pick an OS.
 */
#ifdef CMF_HOST_TEST
extern volatile uint32_t cmf_test_din;
#define CMF_GPIO_DIN1 ((uintptr_t)&cmf_test_din)
#else
#define CMF_GPIO_DIN1 0x10005210UL
#endif

void cmf_lk_modem_diag(const void *source, void *target)
{
    fdt32_t din = cpu_to_fdt32(*(volatile uint32_t *)CMF_GPIO_DIN1);

    cmf_lk_modem_diag_modem(source, target);
    const char *memory = copy_memory(source, target);
    int chosen = fdt_path_offset(target, "/chosen");

    if (chosen >= 0) {
        fdt_setprop_string(target, chosen, "cmf,lk-memory", memory);
        fdt_setprop(target, chosen, "cmf,boot-key-din", &din, sizeof(din));
    }
}

void cmf_lk_modem_diag_modem(const void *source, void *target)
{
    int chosen = fdt_path_offset(target, "/chosen");
    char result[RESULT_SIZE] = { 0 };
    int record_size = 0;

    if (chosen < 0)
        return;
    const uint8_t *record = NULL;
    const char *status = inspect(source, target, &record_size, &record);
    fdt32_t length = cpu_to_fdt32(record_size);

#ifdef CMF_MODEM_CONTROL
    if (record)
        status = enable_control(target, record) ? "modem-control-record-forwarded" :
                                                  "modem-control-target-invalid";
#endif
    memcpy(result, status, strlen(status));
    fdt_setprop_inplace(target, chosen, "cmf,lk-modem-diagnostic", result,
                       sizeof(result));
    fdt_setprop_inplace(target, chosen, "cmf,lk-modem-record-length",
                       &length, sizeof(length));
}
