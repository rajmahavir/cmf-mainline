// SPDX-License-Identifier: GPL-2.0-only
/* Read the firmware tag window only on a verified CMF diagnostic boot. */
#include <linux/debugfs.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/unaligned.h>

#define CMF_RECEIPT_SIZE 40
#define CMF_RECORD_SIZE 48
#define CMF_TAG_HEADER_SIZE 76
#define CMF_TAG_MAX_SIZE 0x10000

static struct debugfs_blob_wrapper snapshot;
static struct dentry *directory;

static int __init cmf_lk_tags_init(void)
{
    struct device_node *chosen, *reserved;
    struct resource range;
    const u8 *receipt;
    void *mapping;
    u64 base;
    u32 size;
    int length, ret;

    chosen = of_find_node_by_path("/chosen");
    if (!chosen)
        return -ENODEV;
    receipt = of_get_property(chosen, "cmf,lk-modem-diagnostic", &length);
    if (!receipt || length != CMF_RECEIPT_SIZE || memcmp(receipt, "CCCI", 4) ||
        get_unaligned_be32(receipt + 4) != CMF_RECORD_SIZE ||
        get_unaligned_le32(receipt + 20) ||
        get_unaligned_le32(receipt + 24) != 3 ||
        !get_unaligned_le32(receipt + 28) ||
        !(get_unaligned_le32(receipt + 32) & 1) ||
        get_unaligned_le32(receipt + 36)) {
        of_node_put(chosen);
        return -EINVAL;
    }
    base = get_unaligned_le64(receipt + 8);
    size = get_unaligned_le32(receipt + 16);
    of_node_put(chosen);

    reserved = of_find_node_by_path("/reserved-memory/mblock-32-ccci_tag_mem");
    if (!reserved)
        return -ENODEV;
    ret = of_address_to_resource(reserved, 0, &range);
    if (!of_device_is_compatible(reserved, "mediatek,ccci_tag_mem") ||
        !of_property_read_bool(reserved, "no-map"))
        ret = -EINVAL;
    of_node_put(reserved);
    if (ret)
        return ret;
    if (base != range.start || resource_size(&range) != CMF_TAG_MAX_SIZE ||
        size < CMF_TAG_HEADER_SIZE || size > resource_size(&range))
        return -ERANGE;

    mapping = memremap(base, size, MEMREMAP_WC);
    if (!mapping)
        return -ENOMEM;
    snapshot.data = kmemdup(mapping, size, GFP_KERNEL);
    memunmap(mapping);
    if (!snapshot.data)
        return -ENOMEM;
    snapshot.size = size;

    directory = debugfs_create_dir("cmf_lk_tags", NULL);
    if (IS_ERR(directory)) {
        ret = PTR_ERR(directory);
        goto free_snapshot;
    }
    struct dentry *blob = debugfs_create_blob("snapshot", 0400, directory, &snapshot);

    if (!IS_ERR(blob))
        return 0;
    ret = PTR_ERR(blob);
    debugfs_remove_recursive(directory);
free_snapshot:
    kfree(snapshot.data);
    return ret;
}

static void __exit cmf_lk_tags_exit(void)
{
    debugfs_remove_recursive(directory);
    kfree(snapshot.data);
}

module_init(cmf_lk_tags_init);
module_exit(cmf_lk_tags_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Read-only snapshot of the CMF bootloader modem tag window");
