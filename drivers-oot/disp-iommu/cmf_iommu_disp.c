// SPDX-License-Identifier: GPL-2.0-only
/*
 * CMF Phone 1 (MT6878) display driver with IOMMU-backed buffers (Stage 52,
 * path A).
 *
 * Same pipeline handling as the built-in cmf-lk-handoff driver (LK keeps the
 * panel, DSI and DSC running; Linux drives OVL0_2L layer 0 only), but every
 * GEM object is a drm_gem_shmem object: scattered system pages, write-combined
 * CPU mappings, PRIME export of the real page list, and PRIME import of
 * foreign buffers. Before an object is first scanned out, its pages are mapped
 * into display IOMMU bank 0 at an IOVA from a private allocator, through the
 * cmf_disp_iommu_boot API, and OVL is programmed with that IOVA.
 *
 * It has no OF match table: it is bound explicitly, with driver_override,
 * after the built-in driver has been unbound from the same platform device.
 * It refuses to probe unless cmf_disp_iommu_boot reported PASS on this boot.
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/ktime.h>
#include <linux/genalloc.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>

#include <drm/clients/drm_client_setup.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_fbdev_shmem.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_mode_config.h>
#include <drm/drm_modes.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/drm_vblank.h>

#include "cmf_disp_iommu.h"

/* OVL0_2L registers, same layout as vendor/mtk_disp_ovl.c. */
#define OVL_INTEN		0x004
#define OVL_INTSTA		0x008
#define OVL_IRQ_REG_CMT		BIT(0)	/* shadow registers committed (vendor REG_CMT_INTEN) */
#define OVL_IRQ_FRAME_DONE	BIT(1)
#define OVL_READ_WORK_REG	BIT(20)
#define OVL_EN			0x00c
#define OVL_SRC_CON		0x02c
#define OVL_SRC_SIZE(n)		(0x038 + 0x20 * (n))
#define OVL_PITCH(n)		(0x044 + 0x20 * (n))
#define OVL_ADDR(n)		(0x0f40 + 0x20 * (n))
#define OVL_PITCH_CONST_BLEND	BIT(28)
#define OVL_CON_EN		BIT(0)
#define OVL_SRC_CON_L0_EN	BIT(0)
#define OVL_SRC_CON_L1_EN	BIT(1)
#define OVL_L_CON(n)		(0x030 + 0x20 * (n))
#define OVL_OFFSET(n)		(0x03c + 0x20 * (n))

/* Refresh/frame-rate OSD on OVL0_2L layer 1, like Android's "show refresh rate". */
#define CMF_OSD_W		384
#define CMF_OSD_H		48
#define CMF_OSD_X		24
#define CMF_OSD_Y		150
#define CMF_OSD_SCALE		5
#define CMF_OSD_PITCH		(CMF_OSD_W * 4)

static int osd;
module_param(osd, int, 0400);
MODULE_PARM_DESC(osd, "1: show panel Hz and completed flips per second on OVL layer 1");

#define CMF_HDISPLAY		1080
#define CMF_HFP			173
#define CMF_HSYNC		16
#define CMF_HBP			24
#define CMF_VDISPLAY		2400
#define CMF_VFP			2452
#define CMF_VFP_120		20	/* vendor ext_params_120hz: same clock, half vtotal */
#define CMF_VSYNC		2
#define CMF_VBP			10

/*
 * DSI0, owned by LK (burst video, DSC). v2 touches only the video-mode
 * command path and VFP_NL, exactly as the vendor dyn-fps code and the
 * Stage 53 trial: panel DCS F0 5A 5A, 60 01|21, F7 0B, F0 A5 A5, then VFP.
 */
#define DSI0_BASE		0x1401a000
#define DSI_START		0x000
#define  DSI_START_VIDEO	BIT(0)
#define  DSI_VM_CMD_START	BIT(16)
#define DSI_INTSTA		0x00c
#define  DSI_VM_CMD_DONE	BIT(5)
#define DSI_MODE_CTRL		0x014
#define DSI_VFP_NL		0x028
#define DSI_VM_CMD_CON		0x200
#define DSI_VM_CMD_DATA0	0x208
#define DSI_SHADOW		0xc00
#define  DSI_BYPASS_SHADOW	BIT(1)
#define DSI_VM_CON_LK		0x21

static int prefer_fps = 60;
module_param(prefer_fps, int, 0400);
MODULE_PARM_DESC(prefer_fps, "refresh rate of the preferred mode: 60 (default) or 120");

/* IOVA window: below the 32-bit OVL limit and below the LK pool identity map. */
#define CMF_IOVA_BASE		0x40000000UL
#define CMF_IOVA_END		0xfe000000UL
#define CMF_IOVA_ORDER		16		/* 64 KiB granules */
#define CMF_IOVA_GUARD		SZ_64K

struct cmf_disp {
	struct drm_device drm;
	struct drm_simple_display_pipe pipe;
	struct drm_connector connector;
	void __iomem *ovl;
	void __iomem *dsi;
	int dsi_fps;		/* rate the panel and DSI currently run at */
	struct drm_display_mode mode;
	struct gen_pool *iova_pool;
	struct mutex map_lock;
	struct mutex dsi_lock;		/* serialises DSI0 VM command sends */
	struct backlight_device *bl;
	int irq;
	/* Protected by drm.event_lock; each pending event owns a vblank ref. */
	struct drm_pending_vblank_event *pending_event;
	u64 pending_sequence;
	u32 pending_addr;
	u32 pending_pitch;
	u32 pending_size;
	/* OSD (osd=1) */
	atomic_t flips;			/* flip events completed since the last OSD update */
	struct page **osd_pages;
	dma_addr_t *osd_dma;
	unsigned int osd_npages;
	u32 *osd_vaddr;
	unsigned long osd_iova;
	struct delayed_work osd_work;
	ktime_t osd_last;
};

struct cmf_gem {
	struct drm_gem_shmem_object shmem;
	unsigned long iova;	/* 0 until first scanout */
	size_t iova_size;	/* mapped bytes (no guard) */
};

static struct cmf_gem *to_cmf_gem(struct drm_gem_object *obj)
{
	return container_of(to_drm_gem_shmem_obj(obj), struct cmf_gem, shmem);
}

static struct cmf_disp *drm_to_cmf(struct drm_device *drm)
{
	return container_of(drm, struct cmf_disp, drm);
}

static struct cmf_disp *pipe_to_cmf(struct drm_simple_display_pipe *pipe)
{
	return container_of(pipe, struct cmf_disp, pipe);
}

/* ---- GEM: shmem objects with a lazily assigned display IOVA ---- */

static void cmf_gem_free(struct drm_gem_object *obj)
{
	struct cmf_disp *priv = drm_to_cmf(obj->dev);
	struct cmf_gem *bo = to_cmf_gem(obj);

	if (bo->iova) {
		/*
		 * The atomic helpers drop the last framebuffer reference only after
		 * the flip away from it completed, and this driver completes a flip
		 * only once OVL's latched address has changed.
		 */
		if (cmf_dispiommu_unmap(bo->iova, bo->iova_size))
			drm_err(obj->dev, "IOVA %#lx unmap failed; range leaked\n", bo->iova);
		else
			gen_pool_free(priv->iova_pool, bo->iova, bo->iova_size + CMF_IOVA_GUARD);
		bo->iova = 0;
	}
	drm_gem_shmem_object_free(obj);
}

static const struct drm_gem_object_funcs cmf_gem_funcs = {
	.free		= cmf_gem_free,
	.print_info	= drm_gem_shmem_object_print_info,
	.pin		= drm_gem_shmem_object_pin,
	.unpin		= drm_gem_shmem_object_unpin,
	.get_sg_table	= drm_gem_shmem_object_get_sg_table,
	.vmap		= drm_gem_shmem_object_vmap,
	.vunmap		= drm_gem_shmem_object_vunmap,
	.mmap		= drm_gem_shmem_object_mmap,
	.vm_ops		= &drm_gem_shmem_vm_ops,
};

static struct drm_gem_object *cmf_gem_create_object(struct drm_device *drm, size_t size)
{
	struct cmf_gem *bo = kzalloc(sizeof(*bo), GFP_KERNEL);

	if (!bo)
		return ERR_PTR(-ENOMEM);
	bo->shmem.map_wc = true;	/* OVL is not cache coherent */
	bo->shmem.base.funcs = &cmf_gem_funcs;
	return &bo->shmem.base;
}

/* Map every page of the object into bank 0, once. Sleeps. */
static int cmf_gem_map_iova(struct cmf_disp *priv, struct cmf_gem *bo)
{
	struct drm_gem_object *obj = &bo->shmem.base;
	unsigned int npages = obj->size >> PAGE_SHIFT, i = 0;
	struct sg_page_iter it;
	struct sg_table *sgt;
	struct page **pages;
	unsigned long iova;
	int ret;

	mutex_lock(&priv->map_lock);
	if (bo->iova) {
		mutex_unlock(&priv->map_lock);
		return 0;
	}
	/* Pins the pages (or returns the imported table) and cleans CPU caches. */
	sgt = drm_gem_shmem_get_pages_sgt(&bo->shmem);
	if (IS_ERR(sgt)) {
		ret = PTR_ERR(sgt);
		goto out;
	}
	pages = kvmalloc_array(npages, sizeof(*pages), GFP_KERNEL);
	if (!pages) {
		ret = -ENOMEM;
		goto out;
	}
	for_each_sgtable_page(sgt, &it, 0) {
		if (i == npages)
			break;
		pages[i++] = sg_page_iter_page(&it);
	}
	if (i != npages) {
		ret = -EINVAL;
		goto free_pages;
	}
	iova = gen_pool_alloc(priv->iova_pool, obj->size + CMF_IOVA_GUARD);
	if (!iova) {
		ret = -ENOSPC;
		goto free_pages;
	}
	ret = cmf_dispiommu_map_pages(iova, pages, npages);
	if (ret) {
		cmf_dispiommu_unmap(iova, obj->size);
		gen_pool_free(priv->iova_pool, iova, obj->size + CMF_IOVA_GUARD);
		goto free_pages;
	}
	bo->iova = iova;
	bo->iova_size = obj->size;
	drm_dbg_driver(obj->dev, "mapped %zu bytes (%u pages) at IOVA %#lx\n",
		       obj->size, npages, iova);
free_pages:
	kvfree(pages);
out:
	mutex_unlock(&priv->map_lock);
	if (ret)
		drm_err(obj->dev, "display IOVA map failed: %d\n", ret);
	return ret;
}

static u32 cmf_fb_addr(struct drm_framebuffer *fb)
{
	return to_cmf_gem(fb->obj[0])->iova + fb->offsets[0];
}

/* ---- Scanout ---- */

static const struct drm_display_mode cmf_disp_mode = {
	.name		= "1080x2400",
	.type		= DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
	.clock		= 377349,
	.hdisplay	= CMF_HDISPLAY,
	.hsync_start	= CMF_HDISPLAY + CMF_HFP,
	.hsync_end	= CMF_HDISPLAY + CMF_HFP + CMF_HSYNC,
	.htotal		= CMF_HDISPLAY + CMF_HFP + CMF_HSYNC + CMF_HBP,
	.vdisplay	= CMF_VDISPLAY,
	.vsync_start	= CMF_VDISPLAY + CMF_VFP,
	.vsync_end	= CMF_VDISPLAY + CMF_VFP + CMF_VSYNC,
	.vtotal		= CMF_VDISPLAY + CMF_VFP + CMF_VSYNC + CMF_VBP,
};

/* ---- 60/120 Hz on the LK DSI0 link (Stage 53) ---- */

struct cmf_dcs {
	u32 con;	/* VM_CMD_CON */
	u32 data;	/* VM_CMD_DATA0, long packets only */
	bool lng;
};

/* mtk_dsi_vm_cmdq encodings, MT6878 vm_rgb_time_interval (config bit 2). */
static const struct cmf_dcs cmf_dcs_120[] = {
	{ 0x00033927, 0x005A5AF0, true },	/* F0 5A 5A */
	{ 0x01601525, 0, false },		/* 60 01 */
	{ 0x0BF71525, 0, false },		/* F7 0B */
	{ 0x00033927, 0x00A5A5F0, true },	/* F0 A5 A5 */
};
static const struct cmf_dcs cmf_dcs_60[] = {
	{ 0x00033927, 0x005A5AF0, true },
	{ 0x21601525, 0, false },		/* 60 21 */
	{ 0x0BF71525, 0, false },
	{ 0x00033927, 0x00A5A5F0, true },
};

/* One packet through the video-mode command path; sent during VFP. */
static int cmf_dsi_vm_cmd(struct cmf_disp *priv, const struct cmf_dcs *p)
{
	void __iomem *dsi = priv->dsi;
	ktime_t t0;

	if (p->lng)
		writel(p->data, dsi + DSI_VM_CMD_DATA0);
	writel(p->con, dsi + DSI_VM_CMD_CON);
	writel(readl(dsi + DSI_INTSTA) & ~DSI_VM_CMD_DONE, dsi + DSI_INTSTA);
	writel(DSI_START_VIDEO, dsi + DSI_START);
	writel(DSI_START_VIDEO | DSI_VM_CMD_START, dsi + DSI_START);
	t0 = ktime_get();
	while (!(readl(dsi + DSI_INTSTA) & DSI_VM_CMD_DONE)) {
		if (ktime_ms_delta(ktime_get(), t0) > 50) {
			writel(DSI_START_VIDEO, dsi + DSI_START);
			return -ETIMEDOUT;
		}
		udelay(20);
	}
	writel(DSI_START_VIDEO, dsi + DSI_START);
	return 0;
}

/* LK left DSI0 in burst video with the VM command path armed and shadow bypassed. */
static bool cmf_dsi_ready(struct cmf_disp *priv)
{
	void __iomem *dsi = priv->dsi;

	return dsi && readl(dsi + DSI_MODE_CTRL) == 0x3 &&
	       readl(dsi + DSI_START) == DSI_START_VIDEO &&
	       readl(dsi + DSI_VM_CMD_CON) == DSI_VM_CON_LK &&
	       (readl(dsi + DSI_SHADOW) & DSI_BYPASS_SHADOW);
}

/* Send n packets in order; call with dsi_lock held. */
static int cmf_dsi_send_locked(struct cmf_disp *priv, const struct cmf_dcs *seq, int n)
{
	int i, ret = 0;

	lockdep_assert_held(&priv->dsi_lock);
	if (!cmf_dsi_ready(priv))
		return -EBUSY;
	for (i = 0; i < n && !ret; i++)
		ret = cmf_dsi_vm_cmd(priv, &seq[i]);
	writel(DSI_VM_CON_LK, priv->dsi + DSI_VM_CMD_CON);
	return ret ? ret : 0;
}

/* Switch the panel and DSI0 VFP to fps (60 or 120). Sleeps; process context. */
static int cmf_dsi_set_fps(struct cmf_disp *priv, int fps)
{
	const struct cmf_dcs *seq = fps == 120 ? cmf_dcs_120 : cmf_dcs_60;
	u32 vfp = fps == 120 ? CMF_VFP_120 : CMF_VFP;
	int ret;

	if (!priv->dsi || fps == priv->dsi_fps)
		return 0;
	mutex_lock(&priv->dsi_lock);
	ret = cmf_dsi_send_locked(priv, seq, ARRAY_SIZE(cmf_dcs_120));
	if (!ret)
		writel(vfp, priv->dsi + DSI_VFP_NL);
	mutex_unlock(&priv->dsi_lock);
	if (ret) {
		drm_err(&priv->drm, "refresh switch to %d Hz failed (%d); VFP unchanged, staying at %d Hz\n",
			fps, ret, priv->dsi_fps);
		return ret;
	}
	drm_info(&priv->drm, "panel and DSI0 switched %d -> %d Hz (VFP %u)\n",
		 priv->dsi_fps, fps, readl(priv->dsi + DSI_VFP_NL));
	priv->dsi_fps = fps;
	return 0;
}

/* Panel display on/off: DCS 29 / 28 (short, no parameter, type 0x05). */
static const struct cmf_dcs cmf_dcs_display_on = { 0x00290525, 0, false };
static const struct cmf_dcs cmf_dcs_display_off = { 0x00280525, 0, false };

static void cmf_panel_display(struct cmf_disp *priv, bool on)
{
	int ret;

	if (!priv->dsi)
		return;
	mutex_lock(&priv->dsi_lock);
	ret = cmf_dsi_send_locked(priv, on ? &cmf_dcs_display_on : &cmf_dcs_display_off, 1);
	mutex_unlock(&priv->dsi_lock);
	if (ret)
		drm_warn(&priv->drm, "panel display %s failed (%d)\n", on ? "on" : "off", ret);
	else
		drm_dbg_driver(&priv->drm, "panel display %s\n", on ? "on" : "off");
}

/* ---- Backlight: DCS 51 hi lo (vendor bl_tb0), normal range 0..0x7ff ---- */

#define CMF_BL_MAX		0x7ff	/* above this the vendor switches to HBM tables */
#define CMF_BL_DEFAULT		0x500

static int cmf_bl_update_status(struct backlight_device *bd)
{
	struct cmf_disp *priv = bl_get_data(bd);
	int level = backlight_get_brightness(bd);
	struct cmf_dcs p = {
		.con = 0x00033927,			/* long 0x39, 3 bytes */
		.data = 0x51 | (((level >> 8) & 0x07) << 8) | ((level & 0xff) << 16),
		.lng = true,
	};
	int ret;

	if (level > CMF_BL_MAX)
		return -EINVAL;
	mutex_lock(&priv->dsi_lock);
	ret = cmf_dsi_send_locked(priv, &p, 1);
	mutex_unlock(&priv->dsi_lock);
	if (ret)
		drm_warn(&priv->drm, "backlight %d failed (%d)\n", level, ret);
	return ret;
}

static const struct backlight_ops cmf_bl_ops = {
	.options	= BL_CORE_SUSPENDRESUME,
	.update_status	= cmf_bl_update_status,
};

static u32 cmf_disp_fb_pitch(struct drm_framebuffer *fb)
{
	u32 pitch = fb->pitches[0];

	if (fb->format->format == DRM_FORMAT_XRGB8888)
		pitch |= OVL_PITCH_CONST_BLEND;
	return pitch;
}

static void cmf_disp_program_fb(struct cmf_disp *priv, struct drm_framebuffer *fb)
{
	u32 addr;

	if (!fb || !to_cmf_gem(fb->obj[0])->iova)
		return;
	addr = cmf_fb_addr(fb);
	writel_relaxed((fb->height << 16) | fb->width, priv->ovl + OVL_SRC_SIZE(0));
	writel_relaxed(cmf_disp_fb_pitch(fb), priv->ovl + OVL_PITCH(0));
	writel_relaxed(addr, priv->ovl + OVL_ADDR(0));
	wmb();
}

static bool cmf_disp_send_pending(struct cmf_disp *priv)
{
	struct drm_pending_vblank_event *event = priv->pending_event;

	assert_spin_locked(&priv->drm.event_lock);
	if (!event)
		return false;
	priv->pending_event = NULL;
	drm_crtc_send_vblank_event(&priv->pipe.crtc, event);
	atomic_inc(&priv->flips);
	return true;
}

/* ---- OSD ---- */

static const char cmf_osd_chars[] = "0123456789Hzfps ";
static const u8 cmf_osd_font[][7] = {	/* 5x7, bit 4 = leftmost column */
	{ 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E }, { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E },
	{ 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F }, { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E },
	{ 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 }, { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E },
	{ 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E }, { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },
	{ 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E }, { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C },
	{ 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 }, { 0x00, 0x00, 0x1F, 0x02, 0x04, 0x08, 0x1F },
	{ 0x06, 0x09, 0x08, 0x1C, 0x08, 0x08, 0x08 }, { 0x00, 0x00, 0x1E, 0x11, 0x1E, 0x10, 0x10 },
	{ 0x00, 0x00, 0x0F, 0x10, 0x0E, 0x01, 0x1E }, { 0, 0, 0, 0, 0, 0, 0 },
};

static void cmf_osd_draw(struct cmf_disp *priv, const char *text, u32 split, u32 c1, u32 c2)
{
	const int cw = 6 * CMF_OSD_SCALE, top = (CMF_OSD_H - 7 * CMF_OSD_SCALE) / 2;
	u32 *px = priv->osd_vaddr;
	int i, x, y, r, c;

	for (i = 0; i < CMF_OSD_W * CMF_OSD_H; i++)
		px[i] = 0x00202020;
	for (i = 0; text[i] && (i + 1) * cw <= CMF_OSD_W; i++) {
		const char *p = strchr(cmf_osd_chars, text[i]);
		const u8 *g = cmf_osd_font[p ? p - cmf_osd_chars : 15];
		u32 col = i < split ? c1 : c2;

		for (r = 0; r < 7; r++)
			for (c = 0; c < 5; c++)
				if (g[r] & (0x10 >> c))
					for (y = 0; y < CMF_OSD_SCALE; y++)
						for (x = 0; x < CMF_OSD_SCALE; x++)
							px[(top + r * CMF_OSD_SCALE + y) * CMF_OSD_W +
							   6 + i * cw + c * CMF_OSD_SCALE + x] = col;
	}
	for (i = 0; i < priv->osd_npages; i++)
		dma_sync_single_for_device(priv->drm.dev, priv->osd_dma[i], PAGE_SIZE,
					   DMA_TO_DEVICE);
}

static void cmf_osd_work(struct work_struct *work)
{
	struct cmf_disp *priv = container_of(to_delayed_work(work), struct cmf_disp, osd_work);
	ktime_t now = ktime_get();
	s64 ms = ktime_ms_delta(now, priv->osd_last);
	int n = atomic_xchg(&priv->flips, 0);
	char text[16];
	int hz = priv->dsi_fps ? priv->dsi_fps : 60;

	priv->osd_last = now;
	snprintf(text, sizeof(text), "%dHz %dfps", hz, ms > 0 ? (int)((n * 1000 + ms / 2) / ms) : 0);
	cmf_osd_draw(priv, text, strchr(text, ' ') - text, 0x00ffffff, 0x0000ff00);
	schedule_delayed_work(&priv->osd_work, msecs_to_jiffies(1000));
}

/* Allocate, map and start the OSD; any failure just leaves it off. */
static void cmf_osd_init(struct cmf_disp *priv)
{
	unsigned int n = DIV_ROUND_UP(CMF_OSD_PITCH * CMF_OSD_H, PAGE_SIZE), i;
	struct device *dev = priv->drm.dev;

	priv->osd_pages = kcalloc(n, sizeof(*priv->osd_pages), GFP_KERNEL);
	priv->osd_dma = kcalloc(n, sizeof(*priv->osd_dma), GFP_KERNEL);
	if (!priv->osd_pages || !priv->osd_dma)
		goto fail;
	for (i = 0; i < n; i++) {
		priv->osd_pages[i] = alloc_page(GFP_KERNEL | __GFP_ZERO);
		if (!priv->osd_pages[i])
			goto fail;
		priv->osd_dma[i] = dma_map_page(dev, priv->osd_pages[i], 0, PAGE_SIZE, DMA_TO_DEVICE);
		if (dma_mapping_error(dev, priv->osd_dma[i])) {
			__free_page(priv->osd_pages[i]);
			priv->osd_pages[i] = NULL;
			goto fail;
		}
		priv->osd_npages = i + 1;
	}
	priv->osd_vaddr = vmap(priv->osd_pages, n, VM_MAP, PAGE_KERNEL);
	if (!priv->osd_vaddr)
		goto fail;
	priv->osd_iova = gen_pool_alloc(priv->iova_pool, n * PAGE_SIZE + CMF_IOVA_GUARD);
	if (!priv->osd_iova)
		goto fail;
	if (cmf_dispiommu_map_pages(priv->osd_iova, priv->osd_pages, n)) {
		cmf_dispiommu_unmap(priv->osd_iova, n * PAGE_SIZE);
		gen_pool_free(priv->iova_pool, priv->osd_iova, n * PAGE_SIZE + CMF_IOVA_GUARD);
		priv->osd_iova = 0;
		goto fail;
	}
	cmf_osd_draw(priv, "  Hz  fps", 4, 0x00ffffff, 0x0000ff00);
	priv->osd_last = ktime_get();
	INIT_DELAYED_WORK(&priv->osd_work, cmf_osd_work);
	schedule_delayed_work(&priv->osd_work, msecs_to_jiffies(1000));
	drm_info(&priv->drm, "OSD on OVL layer 1 at IOVA %#lx (%u pages)\n", priv->osd_iova, n);
	return;
fail:
	drm_warn(&priv->drm, "OSD setup failed; continuing without it\n");
	if (priv->osd_vaddr)
		vunmap(priv->osd_vaddr);
	priv->osd_vaddr = NULL;
	for (i = 0; i < priv->osd_npages; i++) {
		dma_unmap_page(dev, priv->osd_dma[i], PAGE_SIZE, DMA_TO_DEVICE);
		__free_page(priv->osd_pages[i]);
	}
	priv->osd_npages = 0;
	kfree(priv->osd_pages);
	kfree(priv->osd_dma);
	priv->osd_pages = NULL;
	priv->osd_dma = NULL;
}

static void cmf_osd_fini(struct cmf_disp *priv)
{
	unsigned int i;

	if (!priv->osd_iova)
		return;
	cancel_delayed_work_sync(&priv->osd_work);
	cmf_dispiommu_unmap(priv->osd_iova, priv->osd_npages * PAGE_SIZE);
	gen_pool_free(priv->iova_pool, priv->osd_iova, priv->osd_npages * PAGE_SIZE + CMF_IOVA_GUARD);
	priv->osd_iova = 0;
	vunmap(priv->osd_vaddr);
	for (i = 0; i < priv->osd_npages; i++) {
		dma_unmap_page(priv->drm.dev, priv->osd_dma[i], PAGE_SIZE, DMA_TO_DEVICE);
		__free_page(priv->osd_pages[i]);
	}
	kfree(priv->osd_pages);
	kfree(priv->osd_dma);
}

/* Layer 1 mirrors layer 0's LK format (CON 0x21FF + const blend): opaque box. */
static u32 cmf_osd_layers(struct cmf_disp *priv)
{
	if (!priv->osd_iova)
		return OVL_SRC_CON_L0_EN;
	writel_relaxed(readl(priv->ovl + OVL_L_CON(0)), priv->ovl + OVL_L_CON(1));
	writel_relaxed((CMF_OSD_H << 16) | CMF_OSD_W, priv->ovl + OVL_SRC_SIZE(1));
	writel_relaxed((CMF_OSD_Y << 16) | CMF_OSD_X, priv->ovl + OVL_OFFSET(1));
	writel_relaxed(OVL_PITCH_CONST_BLEND | CMF_OSD_PITCH, priv->ovl + OVL_PITCH(1));
	writel_relaxed(priv->osd_iova, priv->ovl + OVL_ADDR(1));
	return OVL_SRC_CON_L0_EN | OVL_SRC_CON_L1_EN;
}

static irqreturn_t cmf_disp_irq(int irq, void *data)
{
	struct cmf_disp *priv = data;
	struct drm_crtc *crtc = &priv->pipe.crtc;
	unsigned long flags;
	u64 sequence;
	bool put = false;

	u32 status = readl(priv->ovl + OVL_INTSTA) & (OVL_IRQ_FRAME_DONE | OVL_IRQ_REG_CMT);

	if (!status)
		return IRQ_NONE;
	/* MT6878 status bits are write-zero-to-clear; clear only the ones handled. */
	writel(~status, priv->ovl + OVL_INTSTA);
	/* Vblank counting stays on frame completion, one per frame. */
	if ((status & OVL_IRQ_FRAME_DONE) && !drm_crtc_handle_vblank(crtc))
		return IRQ_HANDLED;
	sequence = drm_crtc_vblank_count(crtc);

	spin_lock_irqsave(&priv->drm.event_lock, flags);
	/*
	 * Complete a flip as soon as its registers are latched. REG_CMT fires at
	 * the shadow commit at frame start, which leaves the client a whole frame
	 * for its next flip; at 120 Hz frame-done is only about 110 us before the
	 * next latch. Frame-done stays as the fallback. READ_WORK_REG: these
	 * reads return the latched registers.
	 */
	if (priv->pending_event && sequence > priv->pending_sequence &&
	    readl(priv->ovl + OVL_ADDR(0)) == priv->pending_addr &&
	    readl(priv->ovl + OVL_PITCH(0)) == priv->pending_pitch &&
	    readl(priv->ovl + OVL_SRC_SIZE(0)) == priv->pending_size)
		put = cmf_disp_send_pending(priv);
	spin_unlock_irqrestore(&priv->drm.event_lock, flags);
	if (put)
		drm_crtc_vblank_put(crtc);
	return IRQ_HANDLED;
}

static int cmf_disp_enable_vblank(struct drm_simple_display_pipe *pipe)
{
	struct cmf_disp *priv = pipe_to_cmf(pipe);

	writel(~(u32)(OVL_IRQ_FRAME_DONE | OVL_IRQ_REG_CMT), priv->ovl + OVL_INTSTA);
	writel(readl(priv->ovl + OVL_INTEN) | OVL_IRQ_FRAME_DONE | OVL_IRQ_REG_CMT,
	       priv->ovl + OVL_INTEN);
	return 0;
}

static void cmf_disp_disable_vblank(struct drm_simple_display_pipe *pipe)
{
	struct cmf_disp *priv = pipe_to_cmf(pipe);

	writel(readl(priv->ovl + OVL_INTEN) & ~(OVL_IRQ_FRAME_DONE | OVL_IRQ_REG_CMT),
	       priv->ovl + OVL_INTEN);
	readl(priv->ovl + OVL_INTEN);
}

static void cmf_disp_irq_stop(void *data)
{
	struct cmf_disp *priv = data;

	cmf_disp_disable_vblank(&priv->pipe);
	synchronize_irq(priv->irq);
}

static int cmf_disp_connector_get_modes(struct drm_connector *connector)
{
	struct cmf_disp *priv = container_of(connector, struct cmf_disp, connector);
	int fps_list[2] = { 60, 120 }, i, n = 0;

	for (i = 0; i < 2; i++) {
		struct drm_display_mode *mode = drm_mode_duplicate(connector->dev, &priv->mode);

		if (!mode)
			continue;
		if (fps_list[i] == 120) {
			/* Same clock and horizontal timing; VFP 2452 -> 20. */
			mode->vsync_start = CMF_VDISPLAY + CMF_VFP_120;
			mode->vsync_end = mode->vsync_start + CMF_VSYNC;
			mode->vtotal = mode->vsync_end + CMF_VBP;
		}
		drm_mode_set_name(mode);
		mode->type = DRM_MODE_TYPE_DRIVER;
		if (fps_list[i] == (prefer_fps == 120 ? 120 : 60))
			mode->type |= DRM_MODE_TYPE_PREFERRED;
		drm_mode_probed_add(connector, mode);
		n++;
	}
	return n;
}

static const struct drm_connector_helper_funcs cmf_disp_connector_helper_funcs = {
	.get_modes = cmf_disp_connector_get_modes,
};

static enum drm_connector_status
cmf_disp_connector_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static const struct drm_connector_funcs cmf_disp_connector_funcs = {
	.detect			= cmf_disp_connector_detect,
	.fill_modes		= drm_helper_probe_single_connector_modes,
	.destroy		= drm_connector_cleanup,
	.reset			= drm_atomic_helper_connector_reset,
	.atomic_duplicate_state	= drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_connector_destroy_state,
};

static enum drm_mode_status
cmf_disp_pipe_mode_valid(struct drm_simple_display_pipe *pipe,
			 const struct drm_display_mode *mode)
{
	if (mode->hdisplay != CMF_HDISPLAY || mode->vdisplay != CMF_VDISPLAY)
		return MODE_BAD;
	/* Only the two vertical timings the panel supports. */
	if (mode->vsync_start != CMF_VDISPLAY + CMF_VFP &&
	    mode->vsync_start != CMF_VDISPLAY + CMF_VFP_120)
		return MODE_BAD;
	return MODE_OK;
}

static int cmf_disp_pipe_check(struct drm_simple_display_pipe *pipe,
			       struct drm_plane_state *plane_state,
			       struct drm_crtc_state *crtc_state)
{
	struct drm_framebuffer *fb = plane_state->fb;

	if (fb && (fb->width != CMF_HDISPLAY || fb->height != CMF_VDISPLAY ||
		   plane_state->src_x || plane_state->src_y))
		return -EINVAL;
	return 0;
}

/* Map the buffer before the commit that first shows it; then the fences. */
static int cmf_disp_prepare_fb(struct drm_simple_display_pipe *pipe,
			       struct drm_plane_state *plane_state)
{
	struct drm_framebuffer *fb = plane_state->fb;
	int ret;

	if (fb) {
		ret = cmf_gem_map_iova(pipe_to_cmf(pipe), to_cmf_gem(fb->obj[0]));
		if (ret)
			return ret;
	}
	return drm_gem_plane_helper_prepare_fb(&pipe->plane, plane_state);
}

static void cmf_disp_pipe_enable(struct drm_simple_display_pipe *pipe,
				 struct drm_crtc_state *crtc_state,
				 struct drm_plane_state *plane_state)
{
	struct cmf_disp *priv = pipe_to_cmf(pipe);
	int fps = crtc_state->mode.vsync_start == CMF_VDISPLAY + CMF_VFP_120 ? 120 : 60;

	/* A failed switch leaves the panel at its old rate; scanout still works. */
	cmf_dsi_set_fps(priv, fps);
	cmf_disp_program_fb(priv, plane_state->fb);
	writel_relaxed(cmf_osd_layers(priv), priv->ovl + OVL_SRC_CON);
	writel_relaxed(OVL_CON_EN | OVL_READ_WORK_REG, priv->ovl + OVL_EN);
	wmb();
	drm_crtc_vblank_on(&pipe->crtc);
	/* Let one frame of real content reach the panel, then turn its display on. */
	msleep(20);
	cmf_panel_display(priv, true);
}

static void cmf_disp_pipe_disable(struct drm_simple_display_pipe *pipe)
{
	struct cmf_disp *priv = pipe_to_cmf(pipe);
	unsigned long flags;
	bool put;

	/* Real blanking: panel display off (DCS 28) before the overlay stops. */
	cmf_panel_display(priv, false);
	msleep(20);
	drm_crtc_vblank_off(&pipe->crtc);
	spin_lock_irqsave(&priv->drm.event_lock, flags);
	put = cmf_disp_send_pending(priv);
	spin_unlock_irqrestore(&priv->drm.event_lock, flags);
	if (put)
		drm_crtc_vblank_put(&pipe->crtc);
	writel_relaxed(0, priv->ovl + OVL_EN);
	wmb();
}

static void cmf_disp_pipe_update(struct drm_simple_display_pipe *pipe,
				 struct drm_plane_state *old_state)
{
	struct cmf_disp *priv = pipe_to_cmf(pipe);
	struct drm_crtc *crtc = &pipe->crtc;
	struct drm_framebuffer *fb = pipe->plane.state->fb;
	struct drm_pending_vblank_event *event = crtc->state->event;
	unsigned long flags;
	bool put = false;

	spin_lock_irqsave(&priv->drm.event_lock, flags);
	cmf_disp_program_fb(priv, fb);
	if (event) {
		crtc->state->event = NULL;
		if (WARN_ON_ONCE(priv->pending_event))
			put = cmf_disp_send_pending(priv);
		if (fb && crtc->state->active && !drm_crtc_vblank_get(crtc)) {
			priv->pending_addr = cmf_fb_addr(fb);
			priv->pending_pitch = cmf_disp_fb_pitch(fb);
			priv->pending_size = (fb->height << 16) | fb->width;
			priv->pending_sequence = drm_crtc_vblank_count(crtc);
			priv->pending_event = event;
		} else {
			drm_crtc_send_vblank_event(crtc, event);
		}
	}
	spin_unlock_irqrestore(&priv->drm.event_lock, flags);
	if (put)
		drm_crtc_vblank_put(crtc);
}

static const struct drm_simple_display_pipe_funcs cmf_disp_pipe_funcs = {
	.mode_valid	= cmf_disp_pipe_mode_valid,
	.check		= cmf_disp_pipe_check,
	.prepare_fb	= cmf_disp_prepare_fb,
	.enable		= cmf_disp_pipe_enable,
	.disable	= cmf_disp_pipe_disable,
	.update		= cmf_disp_pipe_update,
	.enable_vblank	= cmf_disp_enable_vblank,
	.disable_vblank	= cmf_disp_disable_vblank,
};

static const u32 cmf_disp_formats[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
};

static const struct drm_mode_config_funcs cmf_disp_mode_config_funcs = {
	.fb_create	= drm_gem_fb_create,
	.atomic_check	= drm_atomic_helper_check,
	.atomic_commit	= drm_atomic_helper_commit,
};

DEFINE_DRM_GEM_FOPS(cmf_disp_fops);

static const struct drm_driver cmf_disp_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	.gem_create_object	= cmf_gem_create_object,
	DRM_GEM_SHMEM_DRIVER_OPS,
	DRM_FBDEV_SHMEM_DRIVER_OPS,
	.fops			= &cmf_disp_fops,
	.name			= "cmf-iommu-disp",
	.desc			= "CMF Phone 1 display (IOMMU-backed buffers)",
	.major			= 1,
	.minor			= 0,
};

static void cmf_iova_cleanup(struct drm_device *drm, void *data)
{
	struct cmf_disp *priv = data;

	/* Only empty if every mapped object was freed and unmapped. */
	if (gen_pool_avail(priv->iova_pool) == gen_pool_size(priv->iova_pool))
		gen_pool_destroy(priv->iova_pool);
	else
		drm_warn(drm, "IOVA pool not empty at teardown; leaking it\n");
}

static int cmf_disp_probe(struct platform_device *pdev)
{
	struct cmf_disp *priv;
	struct drm_device *drm;
	int ret;

	if (!cmf_dispiommu_attached())
		return dev_err_probe(&pdev->dev, -ENODEV,
				     "display IOMMU not attached (cmf_disp_iommu_boot did not PASS)\n");

	priv = devm_drm_dev_alloc(&pdev->dev, &cmf_disp_driver, struct cmf_disp, drm);
	if (IS_ERR(priv))
		return PTR_ERR(priv);
	drm = &priv->drm;
	priv->mode = cmf_disp_mode;
	ret = drmm_mutex_init(drm, &priv->map_lock);
	if (ret)
		return ret;
	ret = drmm_mutex_init(drm, &priv->dsi_lock);
	if (ret)
		return ret;

	priv->ovl = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->ovl))
		return PTR_ERR(priv->ovl);

	/* DSI0 is not in this device's resources; it stays shared with LK's setup. */
	priv->dsi = devm_ioremap(&pdev->dev, DSI0_BASE, SZ_4K);
	if (priv->dsi) {
		u32 vfp = readl(priv->dsi + DSI_VFP_NL);

		priv->dsi_fps = vfp == CMF_VFP_120 ? 120 : vfp == CMF_VFP ? 60 : 0;
		if (!priv->dsi_fps) {
			drm_warn(drm, "DSI0 VFP %u unexpected; refresh switching disabled\n", vfp);
			devm_iounmap(&pdev->dev, priv->dsi);
			priv->dsi = NULL;
		}
	}

	/*
	 * The device itself does no direct DMA; the mask only lets the shmem
	 * helper's streaming map clean caches for pages above 4 GiB without
	 * bouncing them.
	 */
	ret = dma_set_mask_and_coherent(drm->dev, DMA_BIT_MASK(64));
	if (ret)
		return ret;

	priv->iova_pool = gen_pool_create(CMF_IOVA_ORDER, -1);
	if (!priv->iova_pool)
		return -ENOMEM;
	ret = gen_pool_add(priv->iova_pool, CMF_IOVA_BASE, CMF_IOVA_END - CMF_IOVA_BASE, -1);
	if (ret) {
		gen_pool_destroy(priv->iova_pool);
		return ret;
	}
	ret = drmm_add_action_or_reset(drm, cmf_iova_cleanup, priv);
	if (ret)
		return ret;

	ret = drmm_mode_config_init(drm);
	if (ret)
		return ret;
	drm->mode_config.min_width = CMF_HDISPLAY;
	drm->mode_config.min_height = CMF_HDISPLAY;
	drm->mode_config.max_width = CMF_VDISPLAY;
	drm->mode_config.max_height = CMF_VDISPLAY;
	drm->mode_config.preferred_depth = 32;
	drm->mode_config.funcs = &cmf_disp_mode_config_funcs;

	drm_connector_helper_add(&priv->connector, &cmf_disp_connector_helper_funcs);
	ret = drm_connector_init(drm, &priv->connector, &cmf_disp_connector_funcs,
				 DRM_MODE_CONNECTOR_VIRTUAL);
	if (ret)
		return ret;
	ret = drm_simple_display_pipe_init(drm, &priv->pipe, &cmf_disp_pipe_funcs,
					   cmf_disp_formats, ARRAY_SIZE(cmf_disp_formats),
					   NULL, &priv->connector);
	if (ret)
		return ret;

	ret = drm_vblank_init(drm, 1);
	if (ret)
		return ret;
	priv->irq = platform_get_irq(pdev, 0);
	if (priv->irq < 0)
		return priv->irq;
	cmf_disp_disable_vblank(&priv->pipe);
	ret = devm_request_irq(drm->dev, priv->irq, cmf_disp_irq, 0, "cmf-iommu-disp-vblank", priv);
	if (ret)
		return dev_err_probe(drm->dev, ret, "frame IRQ request failed\n");
	ret = devm_add_action_or_reset(drm->dev, cmf_disp_irq_stop, priv);
	if (ret)
		return ret;

	drm_mode_config_reset(drm);
	platform_set_drvdata(pdev, priv);
	if (osd)
		cmf_osd_init(priv);	/* before the first modeset enables layer 1 */
	ret = drm_dev_register(drm, 0);
	if (ret) {
		cmf_osd_fini(priv);
		return ret;
	}
	drm_client_setup(drm, NULL);
	if (priv->dsi) {
		struct backlight_properties props = {
			.type = BACKLIGHT_RAW,
			.max_brightness = CMF_BL_MAX,
			/* LK's level is unknown; nothing is sent until the first write. */
			.brightness = CMF_BL_DEFAULT,
		};

		priv->bl = devm_backlight_device_register(drm->dev, "cmf-panel", drm->dev, priv,
							  &cmf_bl_ops, &props);
		if (IS_ERR(priv->bl)) {
			drm_warn(drm, "backlight registration failed (%ld)\n", PTR_ERR(priv->bl));
			priv->bl = NULL;
		}
	}
	drm_info(drm, "CMF IOMMU display ready (1080x2400 @60/120, preferred %d, panel at %d Hz, backlight %s, IOVA %#lx-%#lx)\n",
		 prefer_fps == 120 ? 120 : 60, priv->dsi_fps, priv->bl ? "cmf-panel" : "none",
		 CMF_IOVA_BASE, CMF_IOVA_END);
	return 0;
}

static void cmf_disp_remove(struct platform_device *pdev)
{
	struct cmf_disp *priv = platform_get_drvdata(pdev);

	drm_dev_unregister(&priv->drm);
	drm_atomic_helper_shutdown(&priv->drm);
	writel(OVL_SRC_CON_L0_EN, priv->ovl + OVL_SRC_CON);
	cmf_osd_fini(priv);
	/* Hand the link back as LK left it, for the built-in driver's 60 Hz mode. */
	cmf_dsi_set_fps(priv, 60);
	/* The shutdown above sent display-off; the built-in driver never sends display-on. */
	cmf_panel_display(priv, true);
}

static struct platform_driver cmf_disp_platform_driver = {
	.probe	= cmf_disp_probe,
	.remove	= cmf_disp_remove,
	.driver	= {
		.name	= "cmf-iommu-disp",
	},
};
module_platform_driver(cmf_disp_platform_driver);

MODULE_DESCRIPTION("CMF Phone 1 display with IOMMU-backed shmem buffers");
MODULE_LICENSE("GPL");
