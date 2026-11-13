/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 block glue: readable C of the vendor's tisp_clm_ct_update,
 * tisp_clm_refresh_by_csc, tisp_lce_remove and tisp_ae_get_info.
 *
 * - tisp_clm_ct_update: CT interpolation of the CLM LUT, then the LUT write
 *   (direct, or the register-list variant in shadow mode, t32_data_d03a0).
 * - tisp_clm_refresh_by_csc: fetch the CSC attributes into the CLM state
 *   (t32_data_e28d0[ch] + 4) and redo the CLM parameters and registers.
 * - tisp_lce_remove: free the three LCE buffers of each channel
 *   (t32_data_e1db8, 36 B per channel, pointers at +12 .. +20) and the
 *   channel state (t32_data_e1da8, vmalloc).
 * - tisp_ae_get_info: copy the AE parameter words +576 .. +607 into the AE
 *   tuning block (+8608) and fill the 30-byte AE info reply.
 *
 * The block functions and OS calls are hooks (struct t32_blkglue_ops).
 * tisp_lce_remove has no result in the vendor (v0 left-over).
 * Checked against Matt Davis' recovered C (PR #15 @ 2e2e06bc) by
 * tests/t32_oracle (t32_blkglue_oracle).
 */
#ifndef TX_ISP_T32_BLKGLUE_H
#define TX_ISP_T32_BLKGLUE_H

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

#define T32_BG_CHANS		4
#define T32_BG_LCE_BUF_STRIDE	36
#define T32_BG_LCE_BUF0		12
#define T32_BG_LCE_BUFS		3
#define T32_BG_CLM_CSC		4	/* CSC attributes in the CLM state */

/* AE blocks (see tx_isp_t32_aetrig.h): tuning words / parameter words */
#define T32_BG_AE_PAR_COPY	576
#define T32_BG_AE_COPY_LEN	32
#define T32_BG_AE_TUN_COPY	8608
#define T32_BG_AE_TUN_EXP_LO	7472	/* 64-bit exposure sum, reported >> 10 */
#define T32_BG_AE_TUN_EXP_HI	7476
#define T32_BG_AE_TUN_W4	8620
#define T32_BG_AE_TUN_H0	8570
#define T32_BG_AE_TUN_H1	8572
#define T32_BG_AE_PAR_H		1698
#define T32_BG_AE_ST_W		1740

struct t32_ae_info {		/* AE info reply */
	u32 exp_lo, exp_hi;	/* +0, +4: exposure sum >> 10 */
	u32 w0, w1, w2;		/* +8 .. +16: tuning +8608, +8612, +8620 */
	u32 st;			/* +20: state +1740 */
	u16 h0, h1, h2;		/* +24 tuning +8572, +26 tuning +8570, +28 parameter +1698 */
};

struct t32_blkglue_ops {
	u32 (*clm_ct_interp)(u32 ch, u32 ct, u32 zero);	/* tisp_clm_ct_interp */
	void (*clm_write_lut)(u32 ch);			/* tisp_clm_write_lut */
	void (*clm_lut_write_lut)(u32 ch);		/* tisp_clm_lut_write_lut (shadow mode) */
	void (*csc_api_get)(u32 ch, void *attr);	/* tisp_csc_api_get */
	void (*clm_write_csc_para)(u32 ch);
	void (*clm_calc_para_refresh)(u32 ch);
	void (*clm_write_reg)(u32 ch);
	void (*kfree)(void *p);
	void (*vfree)(void *p);
};

struct t32_blkglue {
	const struct t32_blkglue_ops *ops;
	const u8 *shadow;		/* t32_data_d03a0 */
	u32 *clm_st;			/* t32_data_e28d0 */
	u32 *lce_st;			/* t32_data_e1da8 */
	u8 *lce_buf;			/* t32_data_e1db8 */
	const u32 *ae_ctx;		/* t32_data_e15a8 */
	const u32 *ae_par;		/* t32_data_e1588 */
};

static inline u8 *t32_bg_ptr(u32 addr) { return (u8 *)(uintptr_t)addr; }
static inline u32 t32_bg_rd32(const u8 *p) { u32 v; memcpy(&v, p, 4); return v; }
static inline u16 t32_bg_rd16(const u8 *p) { u16 v; memcpy(&v, p, 2); return v; }

/* tisp_clm_ct_update */
static inline u32 t32_clm_ct_update(struct t32_blkglue *g, u32 ch, u32 ct)
{
	u32 r = g->ops->clm_ct_interp(ch, ct, 0);

	if (r)
		return r;
	if (*g->shadow)
		g->ops->clm_lut_write_lut(ch);
	else
		g->ops->clm_write_lut(ch);
	return 0;
}

/* tisp_clm_refresh_by_csc */
static inline u32 t32_clm_refresh_by_csc(struct t32_blkglue *g, u32 ch)
{
	g->ops->csc_api_get(ch & 0xff, t32_bg_ptr(g->clm_st[ch] + T32_BG_CLM_CSC));
	g->ops->clm_write_csc_para(ch);
	g->ops->clm_calc_para_refresh(ch);
	g->ops->clm_write_reg(ch);
	return 0;
}

/* tisp_lce_remove */
static inline void t32_lce_remove(struct t32_blkglue *g)
{
	u32 ch, i;

	for (ch = 0; ch < T32_BG_CHANS; ch++) {
		if (!g->lce_st[ch])
			continue;
		for (i = 0; i < T32_BG_LCE_BUFS; i++) {
			u8 *slot = g->lce_buf + T32_BG_LCE_BUF_STRIDE * ch + T32_BG_LCE_BUF0 + 4 * i;
			u32 p = t32_bg_rd32(slot);

			if (p) {
				g->ops->kfree(t32_bg_ptr(p));
				memset(slot, 0, 4);
			}
		}
		g->ops->vfree(t32_bg_ptr(g->lce_st[ch]));
		g->lce_st[ch] = 0;
	}
}

/* tisp_ae_get_info */
static inline u32 t32_ae_get_info(struct t32_blkglue *g, u32 ch, struct t32_ae_info *info)
{
	const u8 *ctx = t32_bg_ptr(g->ae_ctx[ch]);
	const u8 *par = t32_bg_ptr(g->ae_par[ch]);
	u8 *tun = t32_bg_ptr(t32_bg_rd32(ctx + 4));
	const u8 *st = t32_bg_ptr(t32_bg_rd32(ctx));
	u32 lo = t32_bg_rd32(tun + T32_BG_AE_TUN_EXP_LO), hi = t32_bg_rd32(tun + T32_BG_AE_TUN_EXP_HI);

	memcpy(tun + T32_BG_AE_TUN_COPY, par + T32_BG_AE_PAR_COPY, T32_BG_AE_COPY_LEN);
	info->exp_lo = hi << 22 | lo >> 10;
	info->exp_hi = hi >> 10;
	info->w0 = t32_bg_rd32(tun + T32_BG_AE_TUN_COPY);
	info->w1 = t32_bg_rd32(tun + T32_BG_AE_TUN_COPY + 4);
	info->w2 = t32_bg_rd32(tun + T32_BG_AE_TUN_W4);
	info->h0 = t32_bg_rd16(tun + T32_BG_AE_TUN_H1);
	info->h2 = t32_bg_rd16(par + T32_BG_AE_PAR_H);
	info->h1 = t32_bg_rd16(tun + T32_BG_AE_TUN_H0);
	info->st = t32_bg_rd32(st + T32_BG_AE_ST_W);
	return 0;
}

#endif
