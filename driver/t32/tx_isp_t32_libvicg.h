/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 VIC glue of the ISP library: init / deinit / reset of the two VICs, raw DMA start, raw buffer release.
 *
 *   t32_lib_vic_init       lib_tisp_vic_init 0xadd14: vmalloc the 120-byte VIC config of VIC (ch & 0xff)
 *                          (failure: log, return -1), clear it, keep the 16-byte input size block, config
 *                          width / height (u16 at 116 / 118) = input words 0 / 1, lib_tisp_vic_cfg(ch, attr),
 *                          DMA flag (par byte 4) = (attr word 148 != 0), frame counter (e3f4c) = 0, init the
 *                          frame wait queue (e3f50); returns the lib_tisp_vic_cfg result
 *   t32_lib_vic_deinit     lib_tisp_vic_deinit 0xacfdc: per VIC 0 / 1: register 0x10 = 0, vfree the config
 *   t32_lib_vic_reset      lib_tisp_vic_reset 0xad0dc: read 0x10 of both VICs (values unused), pulse register
 *                          0x200 of each VIC to 0 and back, 0x18 = 0, then 0x10 = 4 (VIC 0, VIC 1); returns
 *                          the last system_reg_write
 *   t32_lib_vic_dma_start  lib_tisp_vic_dma_start 0xae798: e3f48 = par byte 7; DMA control (VIC par[6] + 0x1ac)
 *                          = 0x4410 (par[4] == 1, par[8] == 2), 0x4414 (par[4] == 1, par[8] == 1), else 0x4440;
 *                          returns tisp_vic_mdma_enable(par, e3f28, 1, e3f2c)
 *   t32_lib_raw_free       lib_tisp_raw_free 0xae6b8: raw mode (par byte 5) 0: kfree the raw buffer;
 *                          1: isp_free_buffer(e3f2c); both only when the raw pointer (e3f30) is set, then
 *                          e3f30 = e3f2c = 0.  Mode 2: byte 15 of channel 0's tuning block = 0,
 *                          tisp_top_reg_cfg(0), returns tisp_mdns_start(0).  Other modes: nothing.
 *     The vendor return value of vic_deinit and raw_free (modes 0, 1, other) is whatever is left in v0 (a
 *     register / address constant): those are void here.
 *
 * The vendor .bss block at t32_data_e3f00 is struct t32_vic_glob.  No channel range check in the vendor
 * (vic_init with ch >= 2 writes past e3f00 / e3f20; vendor behaviour, kept).  OS calls are hooks.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15) by tests/t32_oracle (t32_libvicg_oracle).
 */
#ifndef TX_ISP_T32_LIBVICG_H
#define TX_ISP_T32_LIBVICG_H

#ifndef __KERNEL__
#include <stdint.h>
#include <string.h>
#ifndef T32_HOST_U32
#define T32_HOST_U32
typedef uint32_t u32;
#endif
#ifndef T32_HOST_U8
#define T32_HOST_U8
typedef uint8_t u8;
typedef uint16_t u16;
#endif
#endif

#define T32_VICG_BASE(n)	((0x80U + (n)) << 12)
#define T32_VICG_CTRL		0x10U
#define T32_VICG_IRQ_CLR	0x18U
#define T32_VICG_RESET		0x200U
#define T32_VICG_DMA_CTRL	0x1acU
#define T32_VICG_CTRL_START	4U

#define T32_VIC_CFG_SIZE	120U
#define T32_VIC_CFG_W		116U	/* u16 */
#define T32_VIC_CFG_H		118U	/* u16 */
#define T32_VIC_ATTR_DMA	148U	/* u32 in the sensor attributes */

/* VIC parameter bytes (t32_data_e3f34) */
#define T32_VIC_PAR_DMA		4U	/* raw DMA enabled */
#define T32_VIC_PAR_RAW_MODE	5U	/* 0 kmalloc'd, 1 ISP buffer, 2 MDNS */
#define T32_VIC_PAR_IDX		6U	/* VIC of the raw DMA */
#define T32_VIC_PAR_B7		7U
#define T32_VIC_PAR_FMT		8U

#define T32_VIC_DMA_FMT2	0x4410U
#define T32_VIC_DMA_FMT1	0x4414U
#define T32_VIC_DMA_DEFAULT	0x4440U

#define T32_TUNING_B15		15U

/* vendor .bss from t32_data_e3f00 */
struct t32_vic_glob {
	u8 size[2][16];		/* e3f00: input size block per VIC */
	u8 *cfg[2];		/* e3f20: VIC config (vmalloc, 120 bytes) */
	u32 mdma_a1;		/* e3f28 */
	u32 raw_dma;		/* e3f2c: raw buffer DMA address */
	void *raw_buf;		/* e3f30: raw buffer */
	u8 par[20];		/* e3f34 */
	u8 b48;			/* e3f48 */
	u32 frame_cnt;		/* e3f4c */
	u8 wq[16];		/* e3f50: frame wait queue */
};

struct t32_vic_hooks {
	void *ctx;
	u32 (*sys_read)(void *ctx, u32 reg);			/* system_reg_read */
	u32 (*sys_write)(void *ctx, u32 reg, u32 val);		/* system_reg_write */
	void *(*vmalloc)(void *ctx, u32 size);			/* private_vmalloc */
	void (*vfree)(void *ctx, void *p);			/* private_vfree */
	void (*kfree)(void *ctx, void *p);			/* private_kfree */
	void (*free_buffer)(void *ctx, u32 dma);		/* isp_free_buffer */
	u32 (*top_reg_cfg)(void *ctx, u32 a0);			/* tisp_top_reg_cfg */
	u32 (*mdns_start)(void *ctx, u32 a0);			/* tisp_mdns_start */
	u32 (*mdma_enable)(void *ctx, u8 *par, u32 a1, u32 en, u32 dma);	/* tisp_vic_mdma_enable */
	u32 (*vic_cfg)(void *ctx, u32 ch, void *attr);		/* lib_tisp_vic_cfg */
	void (*log)(void *ctx, u32 level, u32 line);		/* isp_printf (level, source line) */
	void (*init_waitqueue)(void *ctx, void *wq);		/* __init_waitqueue_head */
};

static inline u32 t32_lib_vic_init(const struct t32_vic_hooks *h, struct t32_vic_glob *g, u32 ch,
				   void *attr, const u32 *size)
{
	u8 *cfg;
	u32 r;

	ch &= 0xff;
	cfg = h->vmalloc(h->ctx, T32_VIC_CFG_SIZE);
	g->cfg[ch] = cfg;
	if (!cfg) {
		h->log(h->ctx, 2, 115);
		return (u32)-1;
	}
	memset(cfg, 0, T32_VIC_CFG_SIZE);
	memcpy(g->size[ch], size, 16);
	*(u16 *)(cfg + T32_VIC_CFG_W) = (u16)size[0];
	*(u16 *)(cfg + T32_VIC_CFG_H) = (u16)size[1];
	r = h->vic_cfg(h->ctx, ch, attr);
	if (*(const u32 *)((const u8 *)attr + T32_VIC_ATTR_DMA) == 0) {
		h->log(h->ctx, 0, 129);
		g->par[T32_VIC_PAR_DMA] = 0;
	} else {
		h->log(h->ctx, 0, 132);
		g->par[T32_VIC_PAR_DMA] = 1;
	}
	g->frame_cnt = 0;
	h->init_waitqueue(h->ctx, g->wq);
	return r;
}

static inline void t32_lib_vic_deinit(const struct t32_vic_hooks *h, struct t32_vic_glob *g)
{
	unsigned int i;

	for (i = 0; i < 2; i++) {
		h->sys_write(h->ctx, T32_VICG_BASE(i) + T32_VICG_CTRL, 0);
		if (g->cfg[i]) {
			h->vfree(h->ctx, g->cfg[i]);
			g->cfg[i] = NULL;
		}
	}
}

static inline u32 t32_lib_vic_reset(const struct t32_vic_hooks *h)
{
	unsigned int i;
	u32 v;

	h->sys_read(h->ctx, T32_VICG_BASE(0) + T32_VICG_CTRL);
	h->sys_read(h->ctx, T32_VICG_BASE(1) + T32_VICG_CTRL);
	for (i = 0; i < 2; i++) {
		v = h->sys_read(h->ctx, T32_VICG_BASE(i) + T32_VICG_RESET);
		h->sys_write(h->ctx, T32_VICG_BASE(i) + T32_VICG_RESET, 0);
		h->sys_write(h->ctx, T32_VICG_BASE(i) + T32_VICG_RESET, v);
	}
	h->sys_write(h->ctx, T32_VICG_BASE(0) + T32_VICG_IRQ_CLR, 0);
	h->sys_write(h->ctx, T32_VICG_BASE(1) + T32_VICG_IRQ_CLR, 0);
	h->sys_write(h->ctx, T32_VICG_BASE(0) + T32_VICG_CTRL, T32_VICG_CTRL_START);
	return h->sys_write(h->ctx, T32_VICG_BASE(1) + T32_VICG_CTRL, T32_VICG_CTRL_START);
}

static inline u32 t32_lib_vic_dma_start(const struct t32_vic_hooks *h, struct t32_vic_glob *g)
{
	const u8 *par = g->par;
	u32 val = T32_VIC_DMA_DEFAULT;

	g->b48 = par[T32_VIC_PAR_B7];
	if (par[T32_VIC_PAR_DMA] == 1) {
		if (par[T32_VIC_PAR_FMT] == 2)
			val = T32_VIC_DMA_FMT2;
		else if (par[T32_VIC_PAR_FMT] == 1)
			val = T32_VIC_DMA_FMT1;
	}
	h->sys_write(h->ctx, T32_VICG_BASE(par[T32_VIC_PAR_IDX]) + T32_VICG_DMA_CTRL, val);
	return h->mdma_enable(h->ctx, g->par, g->mdma_a1, 1, g->raw_dma);
}

/* tuning0 = t32_data_e1484[0] (channel 0 tuning block); the return value is only meaningful in mode 2 */
static inline u32 t32_lib_raw_free(const struct t32_vic_hooks *h, struct t32_vic_glob *g, u8 *tuning0)
{
	switch (g->par[T32_VIC_PAR_RAW_MODE]) {
	case 0:
		if (g->raw_buf) {
			h->kfree(h->ctx, g->raw_buf);
			g->raw_buf = NULL;
			g->raw_dma = 0;
		}
		return 0;
	case 1:
		if (g->raw_buf) {
			h->free_buffer(h->ctx, g->raw_dma);
			g->raw_buf = NULL;
			g->raw_dma = 0;
		}
		return 0;
	case 2:
		tuning0[T32_TUNING_B15] = 0;
		h->top_reg_cfg(h->ctx, 0);
		return h->mdns_start(h->ctx, 0);
	default:
		return 0;
	}
}

#endif
