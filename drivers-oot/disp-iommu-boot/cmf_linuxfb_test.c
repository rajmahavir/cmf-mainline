// SPDX-License-Identifier: GPL-2.0
/*
 * CMF Phone 1: scan out a Linux-allocated buffer through display IOMMU bank 0.
 *
 * Needs cmf_disp_iommu_boot (PASS) on this boot. Allocates the frame as
 * individual, scattered 4 KiB pages (no contiguous memory), draws eight
 * vertical colour bars with a white border, cleans the CPU caches, maps the
 * pages at IOVA `iova` (default 0x40000000, outside the LK pool), and points
 * OVL0_2L layer 0 at it. OVL registers are shadowed: the new address latches
 * at the next frame, and with READ_WORK_REG set a readback returns the latched
 * value. After hold_ms the original address is restored, but only if layer 0
 * still shows the test buffer (a Phosh commit may have flipped it back
 * already). Once the restore has latched, the pages are unmapped and freed.
 *
 * Writes: OVL_ADDR(0) twice, bank 0 table entries for the test range.
 */
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/highmem.h>
#include <linux/io.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/sizes.h>
#include <linux/slab.h>

#define OVL0_BASE		0x14002000
#define OVL_SRC_SIZE0		0x038
#define OVL_PITCH0		0x044
#define OVL_ADDR0		0xf40

#include "cmf_disp_iommu.h"

static unsigned long iova = 0x40000000;
module_param(iova, ulong, 0400);
static int hold_ms = 8000;
module_param(hold_ms, int, 0400);

static void __iomem *ovl;

/* Wait until layer 0's latched address equals want; returns ms or -1. */
static int wait_latch(u32 want, int ms)
{
	ktime_t t0 = ktime_get();

	do {
		if (readl(ovl + OVL_ADDR0) == want)
			return ktime_ms_delta(ktime_get(), t0);
		usleep_range(500, 1000);
	} while (ktime_ms_delta(ktime_get(), t0) < ms);
	return -1;
}

static const u32 bars[8] = {
	0x00ffffff, 0x00ffff00, 0x0000ffff, 0x0000ff00,
	0x00ff00ff, 0x00ff0000, 0x000000ff, 0x00000000,
};

static const char *run(struct device *dev)
{
	u32 orig, pitch, size, w, h, x, y, now;
	unsigned int npages, i, contiguous = 0;
	struct page **pages;
	dma_addr_t dma;
	size_t bytes;
	int lat, t, ret;
	bool fault = false, flipped_back = false;

	if (!cmf_dispiommu_attached())
		return "SKIP: cmf_disp_iommu_boot did not PASS on this boot";
	orig = readl(ovl + OVL_ADDR0);
	pitch = readl(ovl + OVL_PITCH0) & 0xffff;
	size = readl(ovl + OVL_SRC_SIZE0);
	w = size & 0xffff;
	h = size >> 16;
	pr_info("cmf-linuxfb: layer0 addr=%#x pitch=%u size=%ux%u\n", orig, pitch, w, h);
	if (!pitch || !w || !h || pitch < w * 4)
		return "SKIP: unexpected layer 0 geometry";
	bytes = PAGE_ALIGN((size_t)pitch * h);
	npages = bytes >> PAGE_SHIFT;

	pages = kcalloc(npages, sizeof(*pages), GFP_KERNEL);
	if (!pages)
		return "SKIP: no memory for the page list";
	for (i = 0; i < npages; i++) {
		pages[i] = alloc_page(GFP_KERNEL | __GFP_ZERO);
		if (!pages[i])
			goto free_pages;
		if (i && page_to_pfn(pages[i]) == page_to_pfn(pages[i - 1]) + 1)
			contiguous++;
	}
	pr_info("cmf-linuxfb: %u pages allocated, %u of %u neighbours physically adjacent, first pa %pa\n",
		npages, contiguous, npages - 1, &(phys_addr_t){ page_to_phys(pages[0]) });

	/* Eight vertical bars, 16-pixel white border. */
	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++) {
			size_t off = (size_t)y * pitch + x * 4;
			u32 v = (x < 16 || y < 16 || x >= w - 16 || y >= h - 16) ?
				0x00ffffff : bars[x * 8 / w];
			u32 *p = kmap_local_page(pages[off >> PAGE_SHIFT]);

			p[(off & (PAGE_SIZE - 1)) / 4] = v;
			kunmap_local(p);
		}
	}
	/* Clean the CPU caches for the device (non-coherent streaming map). */
	for (i = 0; i < npages; i++) {
		dma = dma_map_page(dev, pages[i], 0, PAGE_SIZE, DMA_TO_DEVICE);
		if (dma_mapping_error(dev, dma))
			goto free_pages;
		dma_unmap_page(dev, dma, PAGE_SIZE, DMA_TO_DEVICE);
	}

	ret = cmf_dispiommu_map_pages(iova, pages, npages);
	pr_info("cmf-linuxfb: map %#lx +%#zx -> %d; lookup first %#llx (pa %pa) last %#llx (pa %pa)\n",
		iova, bytes, ret, cmf_dispiommu_lookup(iova),
		&(phys_addr_t){ page_to_phys(pages[0]) },
		cmf_dispiommu_lookup(iova + bytes - PAGE_SIZE),
		&(phys_addr_t){ page_to_phys(pages[npages - 1]) });
	if (ret)
		goto free_pages;
	if (cmf_dispiommu_lookup(iova) != page_to_phys(pages[0]) ||
	    cmf_dispiommu_lookup(iova + bytes - PAGE_SIZE) != page_to_phys(pages[npages - 1])) {
		cmf_dispiommu_unmap(iova, bytes);
		pr_err("cmf-linuxfb: GenieZone mirror mismatch\n");
		goto free_pages;
	}

	writel(iova, ovl + OVL_ADDR0);
	lat = wait_latch(iova, 200);
	pr_info("cmf-linuxfb: flip to %#lx latched after %d ms\n", iova, lat);
	for (t = 0; t < hold_ms; t += 50) {
		if (cmf_dispiommu_faults()) {
			fault = true;
			break;
		}
		now = readl(ovl + OVL_ADDR0);
		if (now != iova) {
			flipped_back = true;
			break;
		}
		msleep(50);
	}
	now = readl(ovl + OVL_ADDR0);
	if (now == iova)
		writel(orig, ovl + OVL_ADDR0);
	lat = wait_latch(now == iova ? orig : now, 200);
	pr_info("cmf-linuxfb: held %d ms, fault=%d, compositor flip=%d (addr %#x); restore latched after %d ms, addr now %#x\n",
		t, fault, flipped_back, now, lat, readl(ovl + OVL_ADDR0));
	if (readl(ovl + OVL_ADDR0) == iova) {
		/* Still scanning out the test buffer: keep it mapped and allocated. */
		return "FAIL: layer 0 still on the test buffer; pages kept";
	}
	msleep(50);	/* one more frame before tearing down */
	ret = cmf_dispiommu_unmap(iova, bytes);
	for (i = 0; i < npages; i++)
		__free_page(pages[i]);
	kfree(pages);
	if (fault)
		return "FAIL: bank 0 fault while showing the Linux buffer";
	if (lat < 0)
		return "FAIL: test buffer never latched";
	return ret ? "PASS with unmap error" : "PASS: Linux-allocated scattered buffer scanned out via IOVA";

free_pages:
	for (i = 0; i < npages && pages[i]; i++)
		__free_page(pages[i]);
	kfree(pages);
	return "SKIP: allocation, cache clean or map failed; display unchanged";
}

static int __init linuxfb_init(void)
{
	struct platform_device *pdev;
	const char *result;

	ovl = ioremap(OVL0_BASE, SZ_4K);
	if (!ovl)
		return -ENOMEM;
	pdev = platform_device_register_simple("cmf-linuxfb-test", -1, NULL, 0);
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);
	if (dma_coerce_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64)))
		return -EIO;
	result = run(&pdev->dev);
	pr_info("cmf-linuxfb: %s\n", result);
	return 0;
}
module_init(linuxfb_init);

MODULE_DESCRIPTION("CMF Linux-allocated scanout buffer through display IOMMU bank 0");
MODULE_LICENSE("GPL");
