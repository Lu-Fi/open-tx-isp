/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 library functions without a block of their own (one block per vendor function).
 *
 * - tisp_ae_deinit: free the per channel AE buffers.
 * - tisp_intp_static_clear: clear the interrupt statistics snapshot of lib_tisp_debug_info.
 * - lib_tisp_save_specified_frame_raw: arm the VIC raw frame capture (stride, size, DMA buffer).
 * - tisp_s_wdr_en: switch a channel between linear and WDR (tuning bank, top masks, blocks).
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

/* ---- tisp_s_wdr_en (vendor 0xb0fb8), oracle t32_swdr_oracle ---- */

#define T32_SWDR_BANKS		6U		/* tuning banks per channel (t32_data_dd0b4[ch * 6 + bank]) */
#define T32_SWDR_TUNE_SIZE	0xf238U		/* tuning block copied from the bank to dd0a4[ch] */
/* top control registers: direct address / shadow index (d03a0 != 0) */
#define T32_SWDR_TOP_RST	0x200U		/* system register: bit 0 starts the top reset */
#define T32_SWDR_TOP_RST_DONE	0x204U		/* bit 0: reset done (polled without limit) */
#define T32_SWDR_TOP_CLK	0x24U		/* bit 2 pulsed */
#define T32_SWDR_TOP_CLK_PULSE	0x4U
#define T32_SWDR_BYPASS		64U		/* tisp_reg: block bypass bits */
#define T32_SWDR_BYPASS_SH	2U
#define T32_SWDR_MASK2		108U		/* tisp_reg: second block mask */
#define T32_SWDR_MASK2_SH	6U
#define T32_SWDR_MASK3		96U		/* tisp_reg: third block mask */
#define T32_SWDR_MASK3_SH	4U
#define T32_SWDR_MASK3_DEIR	0x4U		/* set when the DE-IR flag of channel 0 is 1 */
#define T32_SWDR_BYPASS_BIT3	0x8U		/* cleared when WDR is switched on */
#define T32_SWDR_TOP		0x88U		/* system register: WDR mode (bits 0..4), enable bit 16 */
#define T32_SWDR_TOP_SH		0x4000U
#define T32_SWDR_TOP_EN		0x10000U
#define T32_SWDR_TOP_KEEP_ON	0x300U		/* (config >> 8): config bits 16..17 kept on enable */
#define T32_SWDR_TOP_KEEP_OFF	0x200U		/* (top >> 8): top bit 17 kept on disable */
#define T32_SWDR_REG20		0x20U
#define T32_SWDR_REG20_VAL	0xc7ff1000U
#define T32_SWDR_MODE_MAX	20U		/* channel config modes 4 .. 19 are written as mode + 4 */
/* tuning block bytes (dd0a4[ch]): +4 .. +35 bypass bits, +36 .. +67 mask 2 bits; +68 .. +99 mask 3 bits
 * (the vendor reads the mask 3 bits from channel 0's tuning block for every channel) */
#define T32_SWDR_TUNE_BYPASS	4U
#define T32_SWDR_TUNE_MASK2	36U
#define T32_SWDR_TUNE_MASK3	68U
/* per channel top block (t32_data_da850 + 336 * ch) */
#define T32_SWDR_TOP_SIZE	336U
#define T32_SWDR_TOP_BANK	296U		/* u32 selected bank */
#define T32_SWDR_TOP_BANKW	300U		/* u32 copy of the bank word */
#define T32_SWDR_TOP_BANKWS	304U		/* u32[] per bank word */
#define T32_SWDR_TOP_COPY	128U		/* bytes from the channel state bank table to +0 */
/* per channel state (t32_data_dc284 + 904 * ch) */
#define T32_SWDR_ST_SIZE	904U
#define T32_SWDR_ST_BANKTAB	4U		/* 128 B per bank, the first at +4 + 128 */
#define T32_SWDR_ST_CFG		8U		/* u32: bits 0..4 WDR mode, bits 16..17 kept on enable */
#define T32_SWDR_ST_WDR		36U		/* u8: WDR on */

struct t32_swdr_hooks {
	void *ctx;
	u32 (*sys_read)(void *ctx, u32 reg);					/* system_reg_read */
	void (*sys_write)(void *ctx, u32 reg, u32 val);				/* system_reg_write */
	u32 (*reg_read)(void *ctx, u32 ch, u32 reg);				/* tisp_reg_read */
	void (*reg_write)(void *ctx, u32 ch, u32 reg, u32 val);			/* tisp_reg_write */
	void (*copy)(void *ctx, void *dst, const void *src, u32 n);		/* memcpy */
	void (*lut_params_copy)(void *ctx, u32 ch, u32 bank);			/* tisp_lut_params_copy */
	/* tisp_{lsc,gib,ccm,bcsh,gamma,mdns,lce,defog,dpc}_wdr_en */
	void (*lsc)(void *ctx, u32 ch, u32 en);
	void (*gib)(void *ctx, u32 ch, u32 en);
	void (*ccm)(void *ctx, u32 ch, u32 en);
	void (*bcsh)(void *ctx, u32 ch, u32 en);
	void (*gamma)(void *ctx, u32 ch, u32 en);
	void (*mdns)(void *ctx, u32 ch, u32 en);
	void (*lce)(void *ctx, u32 ch, u32 en);
	void (*defog)(void *ctx, u32 ch, u32 en);
	void (*dpc)(void *ctx, u32 ch, u32 en);
	/* isp_printf(level, source line, values of the message) */
	void (*log)(void *ctx, u32 level, u32 line, u32 a, u32 b, u32 c);
};

struct t32_swdr_data {
	const u8 *shadow;	/* t32_data_d03a0 */
	const u32 *banks;	/* t32_data_dd0b4[4 * 6]: tuning bank blocks */
	const u32 *tune;	/* t32_data_dd0a4[4]: current tuning block per channel */
	u8 *top;		/* t32_data_da850 */
	u8 *state;		/* t32_data_dc284 */
	u32 *deir;		/* t32_data_e1474[4] */
	u32 *wdr_on;		/* t32_data_f0918[4] */
};

/*
 * The 32 bytes at p as bits 0..31, bit i = byte i & 1.  The vendor builds
 * it bit by bit on top of an old value, v = (v & ~(1 << i)) + (byte << i);
 * later steps only touch bits >= i + 1, so the old value and the upper
 * byte bits all drop out.
 */
static inline u32 t32_swdr_bits(const u8 *p)
{
	u32 i, v = 0;

	for (i = 0; i < 32; i++)
		v |= (u32)(p[i] & 1) << i;
	return v;
}

/* mask 3: the 32 bytes ORed in at bit i (whole bytes: a byte > 1 also sets bits above i) */
static inline u32 t32_swdr_or_bits(const u8 *p)
{
	u32 i, v = 0;

	for (i = 0; i < 32; i++)
		v |= (u32)p[i] << i;
	return v;
}

/*
 * tisp_s_wdr_en(ch, en, bank): always 0.  en 1 = WDR, 0 = linear, other: no
 * bank table copy (logged), the rest as linear except the WDR byte and
 * the top register as for 0.  Vendor UB, kept: no range check of ch / bank
 * (the bank table holds 6 per channel), the top reset done bit is polled
 * without a limit.
 */
static inline u32 t32_s_wdr_en(const struct t32_swdr_hooks *h, const struct t32_swdr_data *d, u32 ch, u32 en,
			       u32 bank)
{
	u32 src, ctl, mask2, mask3, cfg, mode, top, v;
	u8 *tb, *st;
	int sh;

	bank &= 0xff;
	src = d->banks[ch * T32_SWDR_BANKS + bank];
	if (!src) {
		h->log(h->ctx, 2, 640, bank, 0, 0);
		return 0;
	}
	v = h->sys_read(h->ctx, T32_SWDR_TOP_RST);
	h->sys_write(h->ctx, T32_SWDR_TOP_RST, v | 1);
	while (!(h->sys_read(h->ctx, T32_SWDR_TOP_RST_DONE) & 1))
		;
	v = h->sys_read(h->ctx, T32_SWDR_TOP_CLK);
	h->sys_write(h->ctx, T32_SWDR_TOP_CLK, v | T32_SWDR_TOP_CLK_PULSE);
	h->sys_write(h->ctx, T32_SWDR_TOP_CLK, v & ~T32_SWDR_TOP_CLK_PULSE);

	sh = *d->shadow != 0;
	ctl = h->reg_read(h->ctx, ch, sh ? T32_SWDR_BYPASS_SH : T32_SWDR_BYPASS);
	h->log(h->ctx, 0, 656, ch, en, ctl);

	tb = d->top + T32_SWDR_TOP_SIZE * ch;
	st = d->state + T32_SWDR_ST_SIZE * ch;
	t32_lr_wr32(tb + T32_SWDR_TOP_BANKW, t32_lr_rd32(tb + T32_SWDR_TOP_BANKWS + 4 * bank));
	h->copy(h->ctx, t32_lr_ptr(d->tune[ch]), t32_lr_ptr(src), T32_SWDR_TUNE_SIZE);
	h->lut_params_copy(h->ctx, ch & 0xff, bank);
	t32_lr_wr32(tb + T32_SWDR_TOP_BANK, bank);
	if (en == 1 || en == 0)
		h->copy(h->ctx, tb, st + T32_SWDR_ST_BANKTAB + (bank + 1) * T32_SWDR_TOP_COPY, T32_SWDR_TOP_COPY);
	else
		h->log(h->ctx, 2, 671, 0, 0, 0);

	/* the bypass register value read above is only printed: every bit is replaced */
	ctl = t32_swdr_bits(t32_lr_ptr(d->tune[ch]) + T32_SWDR_TUNE_BYPASS);
	mask2 = t32_swdr_bits(t32_lr_ptr(d->tune[ch]) + T32_SWDR_TUNE_MASK2);
	mask3 = t32_swdr_or_bits(t32_lr_ptr(d->tune[0]) + T32_SWDR_TUNE_MASK3);
	if (d->deir[0] == 1)
		mask3 |= T32_SWDR_MASK3_DEIR;

	top = sh ? T32_SWDR_TOP_SH : T32_SWDR_TOP;
	cfg = t32_lr_rd32(st + T32_SWDR_ST_CFG);
	mode = cfg & 0x1f;
	if (mode <= 3) {
		h->sys_write(h->ctx, top, mode);
		d->deir[ch] = 0;
	} else if (mode < T32_SWDR_MODE_MAX) {
		h->sys_write(h->ctx, top, mode + 4);
		d->deir[ch] = 1;
	} else {
		h->log(h->ctx, 2, 706, 0, 0, 0);
	}

	if (en == 1) {
		ctl &= ~T32_SWDR_BYPASS_BIT3;
		st[T32_SWDR_ST_WDR] = 1;
		v = (cfg >> 8 & T32_SWDR_TOP_KEEP_ON) | T32_SWDR_TOP_EN | h->sys_read(h->ctx, top);
	} else {
		st[T32_SWDR_ST_WDR] = 0;
		v = h->sys_read(h->ctx, top) >> 8 & T32_SWDR_TOP_KEEP_OFF;
		v |= h->sys_read(h->ctx, top);
	}
	h->sys_write(h->ctx, top, v);
	h->reg_write(h->ctx, ch, sh ? T32_SWDR_BYPASS_SH : T32_SWDR_BYPASS, ctl);
	h->reg_write(h->ctx, ch, sh ? T32_SWDR_MASK2_SH : T32_SWDR_MASK2, mask2);
	h->reg_write(h->ctx, ch, sh ? T32_SWDR_MASK3_SH : T32_SWDR_MASK3, mask3);

	h->lsc(h->ctx, ch, en);
	h->gib(h->ctx, ch, en);
	h->ccm(h->ctx, ch, en);
	h->bcsh(h->ctx, ch, en);
	h->gamma(h->ctx, ch, en);
	h->mdns(h->ctx, ch, en);
	h->lce(h->ctx, ch, en);
	h->defog(h->ctx, ch, en);
	h->sys_write(h->ctx, sh ? T32_SWDR_TOP_SH : T32_SWDR_REG20, T32_SWDR_REG20_VAL);
	h->dpc(h->ctx, ch, en);
	d->wdr_on[ch] = en;
	h->log(h->ctx, 0, 748, en, ctl, 0);
	return 0;
}

#endif
