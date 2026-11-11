/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 library functions without a block of their own (one block per vendor function).
 *
 * - tisp_ae_deinit: free the per channel AE buffers.
 * - tisp_intp_static_clear: clear the interrupt statistics snapshot of lib_tisp_debug_info.
 * - lib_tisp_save_specified_frame_raw: arm the VIC raw frame capture (stride, size, DMA buffer).
 *
 * OS calls are hooks.  Pointers the vendor keeps in its tables are 32-bit
 * words (MIPS32 kernel), read and cleared in place.
 * Checked against Matt Davis' recovered C (PR #15 @ 2e2e06bc) by
 * tests/t32_oracle (one oracle per block, named at each block).
 */
#ifndef TX_ISP_T32_LIBREST_H
#define TX_ISP_T32_LIBREST_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/string.h>
#else
#include <stdint.h>
#include <string.h>
#ifndef T32_HOST_U32
#define T32_HOST_U32
typedef uint32_t u32;
#endif
#ifndef T32_HOST_U8
#define T32_HOST_U8
typedef uint8_t u8;
#endif
#ifndef T32_HOST_U16
#define T32_HOST_U16
typedef uint16_t u16;
#endif
#endif
#include "tx_isp_t32_libvicg.h"

static inline u8 *t32_lr_ptr(u32 addr) { return (u8 *)(uintptr_t)addr; }
static inline u32 t32_lr_rd32(const u8 *p) { u32 v; memcpy(&v, p, 4); return v; }
static inline void t32_lr_wr32(u8 *p, u32 v) { memcpy(p, &v, 4); }

/* ---- tisp_ae_deinit (vendor 0x319a8), oracle t32_aedein_oracle ---- */

/* t32_data_e1598[ch]: AE statistics block, +4 its buffer (kmalloc) */
#define T32_AED_ST_BUF		4
/* the vendor clears these two words after freeing the buffer at +4 (which stays set) */
#define T32_AED_ST_CLR0		16
#define T32_AED_ST_CLR1		20
/* t32_data_e15a8[ch]: AE context, +4 its tuning block (kmalloc) */
#define T32_AED_CTX_TUNE	4

struct t32_aed {
	void (*kfree)(void *p);
	u32 *state_p;	/* t32_data_e15d8[4]: AE state block P (kmalloc) */
	u32 *state_q;	/* t32_data_e15c8[4]: AE state block Q (kmalloc) */
	u32 *stats;	/* t32_data_e1598[4] */
	u32 *ctx;	/* t32_data_e15a8[4] */
};

/*
 * tisp_ae_deinit.  The vendor has no channel check (ch indexes the four
 * entry tables unbounded) and returns no value (v0 is whatever the last
 * call / address load left; lib_tisp_deinit ignores it).
 */
static inline void t32_ae_deinit(const struct t32_aed *a, u32 ch)
{
	if (a->state_p[ch]) {
		a->kfree(t32_lr_ptr(a->state_p[ch]));
		a->state_p[ch] = 0;
	}
	if (a->state_q[ch]) {
		a->kfree(t32_lr_ptr(a->state_q[ch]));
		a->state_q[ch] = 0;
	}
	if (a->stats[ch]) {
		u8 *s = t32_lr_ptr(a->stats[ch]);
		u32 buf = t32_lr_rd32(s + T32_AED_ST_BUF);

		if (buf) {
			a->kfree(t32_lr_ptr(buf));
			t32_lr_wr32(s + T32_AED_ST_CLR0, 0);
			t32_lr_wr32(s + T32_AED_ST_CLR1, 0);
		}
	}
	if (a->stats[ch]) {
		a->kfree(t32_lr_ptr(a->stats[ch]));
		a->stats[ch] = 0;
	}
	if (a->ctx[ch]) {
		u8 *c = t32_lr_ptr(a->ctx[ch]);
		u32 tune = t32_lr_rd32(c + T32_AED_CTX_TUNE);

		if (tune) {
			a->kfree(t32_lr_ptr(tune));
			t32_lr_wr32(c + T32_AED_CTX_TUNE, 0);
		}
	}
	if (a->ctx[ch]) {
		a->kfree(t32_lr_ptr(a->ctx[ch]));
		a->ctx[ch] = 0;
	}
}

/* ---- tisp_intp_static_clear (vendor 0xacd50), oracle t32_intpclr_oracle ---- */

/*
 * The interrupt statistics snapshot that lib_tisp_debug_info prints
 * (vendor .bss t32_data_e3cf0..e3ef8).
 */
#define T32_IPS_BLK_SIZE	128
#define T32_IPS_PAIR_SIZE	8

struct t32_ips {
	void *(*memcpy)(void *dst, const void *src, size_t n);
	u32 *count;	/* t32_data_e3ef8 */
	u8 *blk_e78;	/* t32_data_e3e78[128] */
	u8 *blk_df8;	/* t32_data_e3df8[128] */
	u8 *pair_df0;	/* t32_data_e3df0[8] */
	u8 *blk_d70;	/* t32_data_e3d70[128] */
	u8 *blk_cf0;	/* t32_data_e3cf0[128] */
};

/*
 * tisp_intp_static_clear.  Vendor UB, kept: the clears are memcpy calls
 * with a NULL source (memset was meant), so on the device the first copy
 * reads address 0 and oopses; the vendor has no caller of this function.
 * Returns what the last memcpy returns (the vendor tail call).
 */
static inline void *t32_intp_static_clear(const struct t32_ips *s)
{
	*s->count = 0;
	s->memcpy(s->blk_e78, NULL, T32_IPS_BLK_SIZE);
	s->memcpy(s->blk_df8, NULL, T32_IPS_BLK_SIZE);
	s->memcpy(s->pair_df0, NULL, T32_IPS_PAIR_SIZE);
	s->memcpy(s->blk_d70, NULL, T32_IPS_BLK_SIZE);
	return s->memcpy(s->blk_cf0, NULL, T32_IPS_BLK_SIZE);
}

/* ---- lib_tisp_save_specified_frame_raw (vendor 0xaf184), oracle t32_sfraw_oracle ---- */

/* the request (argument a1) */
struct t32_sfr_req {
	u32 mode;		/* +0: VIC output mode (0 raw, 1 unsupported, 2 YUV 4:2:0, other 16 bit) */
	u32 paddr;		/* +4: physical address of the capture buffer */
	u32 rsvd;
	u8 frame_num;		/* +12: frames to capture */
	u8 fmt;			/* +13: DMA format (VIC parameter byte 8) */
	u8 frame_index;		/* +14: 1 .. T32_SFR_MAX_INDEX */
};

#define T32_SFR_MAX_INDEX	10U
/* further VIC parameter bytes (t32_data_e3f34, see tx_isp_t32_libvicg.h) */
#define T32_VIC_PAR_INDEX	10U	/* frame index */
#define T32_VIC_PAR_PENDING	12U	/* e3f40: capture armed but the DMA not started */
#define T32_VIC_PAR_FSIZE	16U	/* e3f44: u32 frame size in bytes */
#define T32_VIC_CFG_BUSY	8U	/* u32 in the VIC config: DMA in use */
#define T32_SFR_KSEG0		0x80000000U	/* MIPS32 unmapped cached segment: paddr -> kernel pointer */

/* vendor printk / isp_printf texts, "[ISP][%s, %d]: " / "[%s, %d]: " with the function name and source line */
enum t32_sfr_msg {
	T32_SFR_MSG_FRAMENUM,	/* "frameNum error !!! frameNum is %d" (vendor prints frame_index), line 964 */
	T32_SFR_MSG_PADDR,	/* "Snap raw failed because paddr is NULL!", line 969 */
	T32_SFR_MSG_INDEX,	/* "frameIndex error !!! frameIndex is %d" (the old index), line 974 */
	T32_SFR_MSG_FORMAT,	/* "The format is not supported" (isp_printf level 1), line 990 */
	T32_SFR_MSG_STRIDE,	/* "stride is %d", line 994 */
	T32_SFR_MSG_INFO,	/* "vinum = %d, paddr = 0x%x, frame index = %d, frameNum = %d", line 1005 */
};

struct t32_sfr_hooks {
	void *ctx;
	/* printk(msg, line, up to 4 values); returns the printk result */
	u32 (*printk)(void *ctx, u32 msg, u32 line, u32 a, u32 b, u32 c, u32 d);
	void (*isp_printf)(void *ctx, u32 level, u32 msg, u32 line);
	void (*sys_write)(void *ctx, u32 reg, u32 val);			/* system_reg_write */
	u32 (*mdma_enable)(void *ctx, u8 *par, u32 stride, u32 en, u32 dma);	/* tisp_vic_mdma_enable */
};

static inline u32 t32_sfr_align8(u32 v) { return (v + 7) >> 3 << 3; }

/*
 * lib_tisp_save_specified_frame_raw.  The error paths return the printk
 * result (vendor tail calls), a started DMA the tisp_vic_mdma_enable result;
 * when the capture is only armed (frame index != 1 or the VIC DMA busy) the
 * vendor returns a left-over address constant: 0 here.
 * Vendor UB, kept: no range check of ch (& 0xff) for the VIC config table
 * (2 entries) and no NULL check of that config.  The DMA control write
 * always goes to VIC 0, the stride / size use VIC (ch & 1).
 */
static inline u32 t32_lib_save_frame_raw(const struct t32_sfr_hooks *h, struct t32_vic_glob *g, u32 ch,
					 const struct t32_sfr_req *r)
{
	u8 *par = g->par;
	u32 vic, width, height, val;

	ch &= 0xff;
	if (!r->frame_num)
		return h->printk(h->ctx, T32_SFR_MSG_FRAMENUM, 964, r->frame_index, 0, 0, 0);
	if (!r->paddr)
		return h->printk(h->ctx, T32_SFR_MSG_PADDR, 969, 0, 0, 0, 0);
	if ((u32)r->frame_index - 1 >= T32_SFR_MAX_INDEX)
		return h->printk(h->ctx, T32_SFR_MSG_INDEX, 974, par[T32_VIC_PAR_INDEX], 0, 0, 0);

	vic = ch & 1;
	par[T32_VIC_PAR_B7] = r->frame_num;
	par[T32_VIC_PAR_INDEX] = r->frame_index;
	par[T32_VIC_PAR_FMT] = r->fmt;
	par[T32_VIC_PAR_IDX] = vic;
	g->b48 = r->frame_num;
	t32_lr_wr32(par, r->mode);

	width = t32_lr_rd32(g->size[vic]);
	height = t32_lr_rd32(g->size[vic] + 4);
	switch (r->mode) {
	case 0:
		g->mdma_a1 = t32_sfr_align8(width);
		break;
	case 1:
		h->isp_printf(h->ctx, 1, T32_SFR_MSG_FORMAT, 990);
		break;
	case 2:
		g->mdma_a1 = t32_sfr_align8(width * 3 >> 1);
		h->printk(h->ctx, T32_SFR_MSG_STRIDE, 994, g->mdma_a1, 0, 0, 0);
		break;
	default:
		g->mdma_a1 = t32_sfr_align8(width << 1);
		break;
	}
	t32_lr_wr32(par + T32_VIC_PAR_FSIZE, height * g->mdma_a1);
	g->raw_buf = (void *)(uintptr_t)(r->paddr + T32_SFR_KSEG0);
	h->printk(h->ctx, T32_SFR_MSG_INFO, 1005, ch, r->paddr, r->frame_index, r->frame_num);
	g->raw_dma = r->paddr;

	if (par[T32_VIC_PAR_DMA] == 1) {
		if (par[T32_VIC_PAR_FMT] == 2)
			val = T32_VIC_DMA_FMT2;
		else if (par[T32_VIC_PAR_FMT] == 1)
			val = T32_VIC_DMA_FMT1;
		else
			val = T32_VIC_DMA_DEFAULT;
		h->sys_write(h->ctx, T32_VICG_BASE(0) + T32_VICG_DMA_CTRL, val);
	}
	if (par[T32_VIC_PAR_INDEX] == 1 && t32_lr_rd32(g->cfg[ch] + T32_VIC_CFG_BUSY) == 0)
		return h->mdma_enable(h->ctx, par, g->mdma_a1, 1, g->raw_dma);
	par[T32_VIC_PAR_PENDING] = 1;
	return 0;
}

#endif
