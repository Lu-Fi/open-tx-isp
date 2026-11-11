/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 block glue, remaining functions (one block per vendor group).
 *
 * - tisp_awb_deinit: free the per channel AWB buffers.
 * - tisp_vic_mdma_enable: VIC memory DMA size, strides, buffer addresses, enable.
 * - tisp_ae_algo_init: fill the AE algorithm init block from tuning / parameters.
 * - tisp_ae_ai_interrupt_static: AE statistics latch, window area, ROI point.
 * - tisp_ae_par_update_trig: AE parameter refresh from tuning / sensor attributes.
 * - tisp_lce_par: per-frame LCE calculation (locked statistics copy, soft path, curve).
 * - tisp_lce_interrupt_static: LCE statistics bank, histogram rearm, curve roll.
 * - tisp_api_param_array_get: ISP API parameter read by id.
 * - tisp_day_or_night_par_refresh: day / night refresh of every block, waiter wake-up.
 * - tisp_day_or_night_s_ctrl: day / night switch (tuning bin, register pair, refresh).
 *
 * OS calls are hooks.  Pointers the vendor keeps in its tables are 32-bit
 * words (MIPS32 kernel), read and cleared in place.
 * Checked against Matt Davis' recovered C (PR #15 @ 2e2e06bc) by
 * tests/t32_oracle (one oracle per block, named at each block).
 */
#ifndef TX_ISP_T32_BLKREST_H
#define TX_ISP_T32_BLKREST_H

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

static inline u8 *t32_br_ptr(u32 addr) { return (u8 *)(uintptr_t)addr; }
static inline u32 t32_br_rd32(const u8 *p) { u32 v; memcpy(&v, p, 4); return v; }
static inline void t32_br_wr32(u8 *p, u32 v) { memcpy(p, &v, 4); }
static inline u16 t32_br_rd16(const u8 *p) { u16 v; memcpy(&v, p, 2); return v; }

/* ---- tisp_awb_deinit (vendor 0x3b89c), oracle t32_awbdein_oracle ---- */

/* t32_data_e16b4[ch]: AWB work block (kmalloc) */
#define T32_AWBD_WK_BUF1	4	/* kmalloc */
#define T32_AWBD_WK_BUF2	8	/* kmalloc */
#define T32_AWBD_WK_BUF3	12	/* kmalloc */
#define T32_AWBD_WK_VBUF	28	/* vmalloc */
/* t32_data_e1694[ch]: AWB state (vmalloc) */
#define T32_AWBD_ST_BUF		4	/* kmalloc */

struct t32_awbd_ops {
	void (*kfree)(void *p);
	void (*vfree)(void *p);
};

struct t32_awbd {
	const struct t32_awbd_ops *ops;
	u32 *zone;	/* t32_data_e16a4[4]: zone buffer (kmalloc) */
	u32 *work;	/* t32_data_e16b4[4] */
	u32 *state;	/* t32_data_e1694[4] */
};

/* free *slot through fn when it is set, then clear it */
static inline void t32_awbd_free(u8 *slot, void (*fn)(void *p))
{
	u32 p = t32_br_rd32(slot);

	if (p) {
		fn(t32_br_ptr(p));
		t32_br_wr32(slot, 0);
	}
}

/* tisp_awb_deinit: always 0 */
static inline int t32_awb_deinit(struct t32_awbd *a, u32 ch)
{
	if (a->zone[ch]) {
		a->ops->kfree(t32_br_ptr(a->zone[ch]));
		a->zone[ch] = 0;
	}
	if (a->work[ch]) {
		u8 *w = t32_br_ptr(a->work[ch]);

		t32_awbd_free(w + T32_AWBD_WK_VBUF, a->ops->vfree);
		t32_awbd_free(w + T32_AWBD_WK_BUF1, a->ops->kfree);
		t32_awbd_free(w + T32_AWBD_WK_BUF2, a->ops->kfree);
		t32_awbd_free(w + T32_AWBD_WK_BUF3, a->ops->kfree);
		a->ops->kfree(w);
		a->work[ch] = 0;
	}
	if (a->state[ch]) {
		u8 *s = t32_br_ptr(a->state[ch]);

		t32_awbd_free(s + T32_AWBD_ST_BUF, a->ops->kfree);
		a->ops->vfree(s);
		a->state[ch] = 0;
	}
	return 0;
}

/* ---- tisp_vic_mdma_enable (vendor 0xacde0), oracle t32_vicmdma_oracle ---- */

#define T32_VMD_BASE(vi)	(((vi) + 128) << 12)	/* VIC n register page */
#define T32_VMD_CTRL		768
#define T32_VMD_SIZE		772	/* width << 16 | height */
#define T32_VMD_STRIDE0		784
#define T32_VMD_STRIDE1		788
#define T32_VMD_ADDR_Y		800	/* + 4 per buffer (vendor writes only +0) */
#define T32_VMD_ADDR_UV		816
#define T32_VMD_ADDR_RAW	832
#define T32_VMD_MAX_BUFS	4
#define T32_VMD_CTRL_EN		0x80010000u
#define T32_VMD_CTRL_MODE0	0x80010001u
#define T32_VMD_CTRL_MODE2	0x80010008u

struct t32_vmd_sensor {		/* argument a0 */
	u32 mode;		/* +0: 0, 1 (no enable write), 2, other */
	u8 rsvd[2];
	u8 vic;			/* +6 */
};

struct t32_vmd_dim {		/* t32_data_e3f00[vic]: 16 B per VIC */
	u32 width, height;
	u32 rsvd[2];
};

struct t32_vmd_fmt {		/* t32_data_e3f34 */
	u32 rsvd0;
	u8 out;			/* +4: 1 = YUV output */
	u8 rsvd1[3];
	u8 planes;		/* +8: 1 = UV plane only, 2 = Y + UV (NV12) */
};

/*
 * tisp_vic_mdma_enable: program the VIC memory DMA for n (<= 4) buffers at
 * base, each width x height x stride bytes (NV12: twice that, Y then UV).
 * Mode 1 returns 1 without the enable write; the other modes end with the
 * enable write and return its result (0).
 */
static inline u32 t32_vic_mdma_enable(const struct t32_vmd_sensor *sen, u32 stride, u32 n, u32 base,
				      const struct t32_vmd_dim *dim, const struct t32_vmd_fmt *fmt,
				      void (*reg_write)(u32 reg, u32 val))
{
	const struct t32_vmd_dim *d = &dim[sen->vic];
	u32 page = T32_VMD_BASE(sen->vic), frame = d->height * stride;
	u8 i;

	reg_write(page + T32_VMD_SIZE, d->width << 16 | d->height);
	reg_write(page + T32_VMD_STRIDE0, stride);
	reg_write(page + T32_VMD_STRIDE1, stride);
	if (n > T32_VMD_MAX_BUFS)
		n = T32_VMD_MAX_BUFS;
	for (i = 0; i < n; i++) {
		if (fmt->out != 1) {
			reg_write(page + T32_VMD_ADDR_Y, frame * i + base);
			reg_write(page + T32_VMD_ADDR_RAW, frame * i + base);
		} else if (fmt->planes == 2) {
			reg_write(page + T32_VMD_ADDR_Y, i * (frame * 2) + base);
			reg_write(page + T32_VMD_ADDR_UV, (2 * i + 1) * frame + base);
		} else if (fmt->planes == 1) {
			reg_write(page + T32_VMD_ADDR_UV, frame * i + base);
		} else {
			reg_write(page + T32_VMD_ADDR_Y, frame * i + base);
		}
	}
	switch (sen->mode) {
	case 1:
		return 1;
	case 0:
		reg_write(page + T32_VMD_CTRL, T32_VMD_CTRL_MODE0);
		break;
	case 2:
		reg_write(page + T32_VMD_CTRL, T32_VMD_CTRL_MODE2);
		break;
	default:
		reg_write(page + T32_VMD_CTRL, T32_VMD_CTRL_EN);
		break;
	}
	return 0;
}

/* ---- tisp_ae_algo_init (vendor 0x3597c), oracle t32_aealgo_oracle ---- */

#define T32_AEA_CTX_DONE	2008	/* completion in the AE context */
#define T32_AEA_TUN_A		8608	/* 4 tuning words -> info +12 */
#define T32_AEA_TUN_B		8624	/* 4 tuning words -> info +60 */
#define T32_AEA_PAR_A		448	/* 8 parameter words -> info +28 */
#define T32_AEA_PAR_B		480	/* 8 parameter words -> info +76 */
#define T32_AEA_REG_EXP		12376	/* read when tuning +8620 is 0 (shadow index 491) */
#define T32_AEA_REG_EXP_SH	491
#define T32_AEA_REG_GAIN	12808	/* read when tuning +8636 is 0 (shadow index 572) */
#define T32_AEA_REG_GAIN_SH	572
#define T32_AEA_ZONE_STRIDE	1052	/* t32_data_d0950 per channel */
#define T32_AEA_ZONE_WORD	1044

struct t32_aea_info {		/* argument a2, the algorithm's init block */
	u32 rsvd0[2];
	u32 zero;		/* +8 */
	u32 tun_a[4];		/* +12: tuning +8608 .. +8620 */
	u32 par_a[8];		/* +28: parameters +448 .. +476 */
	u32 tun_b[4];		/* +60: tuning +8624 .. +8636 */
	u32 par_b[8];		/* +76: parameters +480 .. +508 */
	u32 rsvd1;		/* +108 */
	u32 one[3];		/* +112 */
	u8 w0, w1;		/* +124, +125: 15 */
	u8 undef[6];		/* +126: vendor stack left-over, 0 here */
	u8 zone[4];		/* +132: t32_data_d0950[ch] +1044 */
};

struct t32_aea_ops {
	void (*init_completion)(void *c);
	u32 (*reg_read)(u32 ch, u32 reg);
};

struct t32_aea {
	const struct t32_aea_ops *ops;
	u32 *enable;		/* t32_data_e15b8[4] */
	const u32 *ctx;		/* t32_data_e15a8[4]: AE context, +0 parameter block, +4 tuning block */
	const u8 *shadow;	/* t32_data_d03a0 */
	const u8 *zone;		/* t32_data_d0950 */
};

/* tisp_ae_algo_init: the vendor's result is a left-over register (none here) */
static inline void t32_ae_algo_init(struct t32_aea *a, u32 ch, u32 enable, struct t32_aea_info *info)
{
	u8 *ctx = t32_br_ptr(a->ctx[ch]);
	const u8 *par, *tun;
	u32 i;

	a->enable[ch] = enable;
	if (enable != 1)
		return;
	a->ops->init_completion(ctx + T32_AEA_CTX_DONE);
	par = t32_br_ptr(t32_br_rd32(ctx));
	tun = t32_br_ptr(t32_br_rd32(ctx + 4));
	info->zero = 0;
	for (i = 0; i < 4; i++) {
		info->tun_a[i] = t32_br_rd32(tun + T32_AEA_TUN_A + 4 * i);
		info->tun_b[i] = t32_br_rd32(tun + T32_AEA_TUN_B + 4 * i);
	}
	for (i = 0; i < 8; i++) {
		info->par_a[i] = t32_br_rd32(par + T32_AEA_PAR_A + 4 * i);
		info->par_b[i] = t32_br_rd32(par + T32_AEA_PAR_B + 4 * i);
	}
	if (!info->tun_a[3])
		info->tun_a[3] = a->ops->reg_read(ch, *a->shadow ? T32_AEA_REG_EXP_SH : T32_AEA_REG_EXP);
	if (!info->tun_b[3])
		info->tun_b[3] = a->ops->reg_read(ch, *a->shadow ? T32_AEA_REG_GAIN_SH : T32_AEA_REG_GAIN);
	info->one[0] = info->one[1] = info->one[2] = 1;
	info->w0 = info->w1 = 15;
	memset(info->undef, 0, sizeof(info->undef));
	memcpy(info->zone, a->zone + T32_AEA_ZONE_STRIDE * ch + T32_AEA_ZONE_WORD, 4);
}

/* ---- tisp_ae_ai_interrupt_static (vendor 0x30df0), oracle t32_aeirq_oracle ---- */

#define T32_AEI_R_STAT0		0x1709c	/* 3 statistics words, compared with tuning +7448 */
#define T32_AEI_R_STAT3		0x170a8	/* 3 more words, stored at tuning +7460 */
#define T32_AEI_R_WIN_X		0x17090	/* window start (lo) / end (hi) */
#define T32_AEI_R_WIN_Y		0x17094
#define T32_AEI_TUN_LAST	7448	/* last 3 statistics words */
#define T32_AEI_TUN_NEXT	7460
#define T32_AEI_PAR_AREA	1288	/* window area / 4 */
#define T32_AEI_PAR_ROI		1500	/* 4 u16 roi words, 4 bytes apart */

struct t32_aeirq_ops {
	u32 (*reg_read)(u32 reg);
	void (*roi_point)(u32 ch, u32 r0, u32 r1, u32 r2, u32 r3);	/* tisp_ae_roi_point */
};

struct t32_aeirq {
	const struct t32_aeirq_ops *ops;
	const u32 *par;		/* t32_data_e1588[4]: AE parameter block */
	const u32 *ctx;		/* t32_data_e15a8[4]: AE context, +4 tuning block */
};

static inline u8 *t32_aeirq_tun(const struct t32_aeirq *a, u32 ch)
{
	return t32_br_ptr(t32_br_rd32(t32_br_ptr(a->ctx[ch]) + 4));
}

/*
 * tisp_ae_ai_interrupt_static: latch the 6 statistics words; when the first
 * three changed, store them and redo the window area (each window register
 * is read twice, as in the vendor); then the ROI point.  Always 1.
 */
static inline int t32_ae_ai_interrupt_static(struct t32_aeirq *a, u32 ch)
{
	u8 *par = t32_br_ptr(a->par[ch]), *tun;
	u32 st[3], i, xs, xe, ys, ye;

	for (i = 0; i < 3; i++)
		st[i] = a->ops->reg_read(T32_AEI_R_STAT0 + 4 * i);
	for (i = 0; i < 3; i++) {
		u32 v = a->ops->reg_read(T32_AEI_R_STAT3 + 4 * i);

		t32_br_wr32(t32_aeirq_tun(a, ch) + T32_AEI_TUN_NEXT + 4 * i, v);
	}
	tun = t32_aeirq_tun(a, ch);
	if (st[0] != t32_br_rd32(tun + T32_AEI_TUN_LAST) || st[1] != t32_br_rd32(tun + T32_AEI_TUN_LAST + 4) ||
	    st[2] != t32_br_rd32(tun + T32_AEI_TUN_LAST + 8)) {
		for (i = 0; i < 3; i++)
			t32_br_wr32(tun + T32_AEI_TUN_LAST + 4 * i, st[i]);
		xs = a->ops->reg_read(T32_AEI_R_WIN_X) & 0xffff;
		xe = a->ops->reg_read(T32_AEI_R_WIN_X) >> 16;
		ys = a->ops->reg_read(T32_AEI_R_WIN_Y) & 0xffff;
		ye = a->ops->reg_read(T32_AEI_R_WIN_Y) >> 16;
		t32_br_wr32(par + T32_AEI_PAR_AREA, (xe - xs) * (ye - ys) >> 2);
	}
	par = t32_br_ptr(a->par[ch]);
	a->ops->roi_point(ch & 0xff, t32_br_rd16(par + T32_AEI_PAR_ROI),
			  t32_br_rd16(par + T32_AEI_PAR_ROI + 4),
			  t32_br_rd16(par + T32_AEI_PAR_ROI + 8),
			  t32_br_rd16(par + T32_AEI_PAR_ROI + 12));
	return 1;
}

/* ---- tisp_ae_par_update_trig (vendor 0x30a8c), oracle t32_aeupd_oracle ---- */

#define T32_AEU_TUN_SRC		12833	/* 9 bytes tuning -> parameter +1276 */
#define T32_AEU_PAR_DST		1276
#define T32_AEU_COPY		9
#define T32_AEU_CTX_WDR		8	/* AE context word: 1 = WDR (short frame too) */
#define T32_AEU_BLK_SRC		908	/* 256 bytes of the context's first block -> parameter +928 */
#define T32_AEU_PAR_SHORT	928
#define T32_AEU_SHORT_COPY	256
#define T32_AEU_SENSOR_STRIDE	232	/* t32_data_dd2d4: sensor attributes per channel, passed by value */

struct t32_aeu_ops {
	void (*sensor_update)(u32 ch, const u8 *attr);		/* tisp_ae_sensor_par_ctrls_update */
	void (*short_sensor_update)(u32 ch, const u8 *attr);	/* tisp_ae_short_sensor_par_ctrls_update */
	void (*par_calc)(u32 ch);				/* tisp_ae_par_calc */
	void (*deflicker_cfg)(u32 ch);				/* tisp_ae_clac_deflicker_cfg */
	u32 (*set_sensor_fps)(u32 ch);				/* tisp_set_ae_sensor_fps */
};

struct t32_aeu {
	const struct t32_aeu_ops *ops;
	const u32 *ctx;		/* t32_data_e15a8[4]: +0 block, +4 tuning block, +8 WDR word */
	const u32 *par;		/* t32_data_e1588[4]: AE parameter block */
	const u8 *sensor;	/* t32_data_dd2d4 */
	const u8 *shadow;	/* t32_data_d03a0 */
};

/*
 * tisp_ae_par_update_trig: refresh the AE parameters from the tuning and
 * sensor attributes, recalculate, then (direct register mode only) the
 * sensor fps.  Returns the fps result, or in shadow mode the shadow byte
 * (vendor left-over register).
 */
static inline u32 t32_ae_par_update_trig(struct t32_aeu *a, u32 ch)
{
	const u8 *ctx = t32_br_ptr(a->ctx[ch]);
	const u8 *attr = a->sensor + T32_AEU_SENSOR_STRIDE * ch;
	u8 shadow;

	memcpy(t32_br_ptr(a->par[ch]) + T32_AEU_PAR_DST, t32_br_ptr(t32_br_rd32(ctx + 4)) + T32_AEU_TUN_SRC,
	       T32_AEU_COPY);
	a->ops->sensor_update(ch & 0xff, attr);
	ctx = t32_br_ptr(a->ctx[ch]);
	if (t32_br_rd32(ctx + T32_AEU_CTX_WDR) == 1) {
		memcpy(t32_br_ptr(a->par[ch]) + T32_AEU_PAR_SHORT, t32_br_ptr(t32_br_rd32(ctx)) + T32_AEU_BLK_SRC,
		       T32_AEU_SHORT_COPY);
		a->ops->short_sensor_update(ch & 0xff, attr);
	}
	a->ops->par_calc(ch);
	a->ops->deflicker_cfg(ch);
	shadow = *a->shadow;
	if (shadow)
		return shadow;
	return a->ops->set_sensor_fps(ch & 0xff);
}

/* ---- tisp_lce_par (vendor 0x5709c), oracle t32_lcepar_oracle ---- */

/* t32_data_e1da8[ch]: LCE state (offsets as in tx_isp_t32_lcem.h) */
#define T32_LCP_IN		164	/* statistics written by the interrupt */
#define T32_LCP_IN_LEN		0x9000
#define T32_LCP_IN2		0x90a4	/* directly after: 2304 bytes more */
#define T32_LCP_IN2_LEN		2304
#define T32_LCP_WORK		0x99a4	/* work copy of both parts */
#define T32_LCP_WORK2		0x129a4
#define T32_LCP_PARAMS		0x226cc	/* LCE parameters */
#define T32_LCP_SEL		0x22737	/* u8: 0 = v4 path, else T41 path */
#define T32_LCP_UPDATE		0x22830	/* u8 flag handed to the soft path, cleared after */
#define T32_LCP_COUNT		0x22834	/* frame counter, wraps to 15 at 1 << 29 */
#define T32_LCP_LOCK		0x2284d
#define T32_LCP_COUNT_WRAP	0x20000000u
#define T32_LCP_COUNT_RESTART	15

struct t32_lcep_ops {
	u32 (*lock)(void *lock);			/* __private_spin_lock_irqsave, returns flags */
	void (*unlock)(void *lock, u32 flags);		/* private_spin_unlock_irqrestore */
	void (*mxu_begin)(u32 zero);			/* kernel_mxu_begin_partial */
	void (*mxu_end)(void);				/* kernel_mxu_end */
	void (*soft_v4)(u32 ch, u8 *params, u8 *work, u32 update);	/* Tisp_lce_soft_simd_v4 */
	void (*soft_t41)(u32 ch, u8 *params, u8 *work, u32 update);	/* Tisp_lce_soft_t41_simd */
	void (*write_curve)(u32 ch);			/* tisp_lce_write_curve */
};

/* tisp_lce_par: per-frame LCE calculation on a locked copy of the statistics; always 0 */
static inline int t32_lce_par(const struct t32_lcep_ops *ops, const u32 *lce_st, u32 ch)
{
	u8 *st = t32_br_ptr(lce_st[ch]);
	u32 flags, n;

	flags = ops->lock(st + T32_LCP_LOCK);
	memcpy(st + T32_LCP_WORK, st + T32_LCP_IN, T32_LCP_IN_LEN);
	memcpy(st + T32_LCP_WORK2, st + T32_LCP_IN2, T32_LCP_IN2_LEN);
	ops->unlock(st + T32_LCP_LOCK, flags);
	ops->mxu_begin(0);
	if (!st[T32_LCP_SEL])
		ops->soft_v4(ch, st + T32_LCP_PARAMS, st + T32_LCP_WORK, st[T32_LCP_UPDATE]);
	else
		ops->soft_t41(ch, st + T32_LCP_PARAMS, st + T32_LCP_WORK, st[T32_LCP_UPDATE]);
	ops->mxu_end();
	st = t32_br_ptr(lce_st[ch]);
	st[T32_LCP_UPDATE] = 0;
	ops->write_curve(ch);
	n = t32_br_rd32(st + T32_LCP_COUNT) + 1;
	t32_br_wr32(st + T32_LCP_COUNT, n < T32_LCP_COUNT_WRAP ? n : T32_LCP_COUNT_RESTART);
	return 0;
}

/* ---- tisp_lce_interrupt_static (vendor 0x583fc), oracle t32_lceirq_oracle ---- */

#define T32_LCI_R_BANK		0xc088	/* low 2 bits: statistics bank the hardware filled */
#define T32_LCI_BANK_LEN	0x9900
#define T32_LCI_HIST_LEN	0x9000
#define T32_LCI_CURVE_LEN	11520	/* curve buffer: next half copied over the first */
/* register (direct) / shadow index pairs */
#define T32_LCI_R_STAT_ADDR	0x2504c
#define T32_LCI_S_STAT_ADDR	2025
#define T32_LCI_R_HIST_ADDR	0x25050
#define T32_LCI_S_HIST_ADDR	2026
#define T32_LCI_R_CLR		0x25048
#define T32_LCI_S_CLR		2024
#define T32_LCI_R_START		0x25040
#define T32_LCI_S_START		2022
#define T32_LCI_R_CURVE_ADDR	0xc0a4
#define T32_LCI_S_CURVE_ADDR	2020

struct t32_lci_buf {		/* t32_data_e1db8[ch], 36 B */
	u32 rsvd[3];
	u32 curve;		/* +12: curve buffer (virtual) */
	u32 stat;		/* +16: statistics banks (virtual) */
	u32 hist;		/* +20: histogram buffer (virtual) */
	u32 curve_phys;		/* +24 */
	u32 stat_phys;		/* +28 */
	u32 hist_phys;		/* +32 */
};

struct t32_lci_ops {
	u32 (*reg_read)(u32 reg);				/* system_reg_read */
	void (*reg_write)(u32 ch, u32 reg, u32 val);		/* tisp_reg_write */
	void (*cache_sync)(void *p, u32 len);			/* private_dma_cache_sync(0, p, len, 0) */
	void (*get_sum_data)(u32 ch, u8 *stat);			/* tisp_lce_get_sum_data */
};

struct t32_lci {
	const struct t32_lci_ops *ops;
	const struct t32_lci_buf *buf;	/* t32_data_e1db8 */
	const u8 *shadow;		/* t32_data_d03a0 */
	u32 *bank;			/* t32_data_e1d7c */
	u8 *last_ch;			/* t32_data_e1d78 */
};

static inline void t32_lci_wr(struct t32_lci *l, u32 ch, u32 reg, u32 sidx, u32 val)
{
	l->ops->reg_write(ch, *l->shadow ? sidx : reg, val);
}

/*
 * tisp_lce_interrupt_static: take the filled statistics bank, summarise it,
 * point the hardware at it again, rearm the histogram, roll the curve
 * buffer and hand its address to the hardware.  Always 1.
 */
static inline int t32_lce_interrupt_static(struct t32_lci *l, u32 ch)
{
	const struct t32_lci_buf *b = &l->buf[ch];
	u32 off, bank = l->ops->reg_read(T32_LCI_R_BANK) & 3;
	u8 *stat;

	*l->bank = bank;
	off = bank * T32_LCI_BANK_LEN;
	stat = t32_br_ptr(b->stat + off);
	l->ops->cache_sync(stat, T32_LCI_BANK_LEN);
	l->ops->get_sum_data(ch, stat);
	l->ops->cache_sync(stat, T32_LCI_BANK_LEN);
	t32_lci_wr(l, ch, T32_LCI_R_STAT_ADDR, T32_LCI_S_STAT_ADDR, off + b->stat_phys);
	l->ops->cache_sync(t32_br_ptr(b->hist), T32_LCI_HIST_LEN);
	t32_lci_wr(l, ch, T32_LCI_R_HIST_ADDR, T32_LCI_S_HIST_ADDR, b->hist_phys);
	t32_lci_wr(l, ch, T32_LCI_R_CLR, T32_LCI_S_CLR, 0);
	t32_lci_wr(l, ch, T32_LCI_R_START, T32_LCI_S_START, 1);
	memcpy(t32_br_ptr(b->curve), t32_br_ptr(b->curve) + T32_LCI_CURVE_LEN, T32_LCI_CURVE_LEN);
	l->ops->cache_sync(t32_br_ptr(b->curve), T32_LCI_CURVE_LEN);
	t32_lci_wr(l, ch, T32_LCI_R_CURVE_ADDR, T32_LCI_S_CURVE_ADDR, b->curve_phys);
	*l->last_ch = ch;
	return 1;
}

/* ---- tisp_api_param_array_get (vendor 0xb2a54), oracle t32_apiget_oracle ---- */

enum t32_api_par_id {		/* jump table t32_data_c2f00, 17 entries */
	T32_API_MASK_ATTR = 0,	/* 32 B per channel, t32_data_f3258 */
	T32_API_PIPE_EN = 1,	/* channel state (t32_data_dc284, 904 B) byte +37, as a u32 */
	T32_API_MASK_OUT = 2,	/* 24 B via tisp_msca_api_get_mask_out_cfg */
	T32_API_MSCA_PAR3 = 3,	/* 3 .. 6 and 8: the 2636 B MSCA parameters */
	T32_API_FCROP = 7,	/* 20 B via tisp_g_fcrop_control */
	T32_API_AUTOZOOM = 9,	/* 60 B via tisp_msca_api_get_autozoom */
	T32_API_BRIGHT = 10,	/* 1 B via tisp_bcsh_api_get_brightness */
	T32_API_ZERO11 = 11,	/* 11 .. 16: one 0 byte */
	T32_API_IDS = 17,
};

#define T32_API_MASK_ATTR_LEN	32
#define T32_API_PIPE_STRIDE	904
#define T32_API_PIPE_BYTE	37
#define T32_API_MASK_OUT_LEN	24
#define T32_API_MSCA_PAR_LEN	2636
#define T32_API_FCROP_LEN	20
#define T32_API_AUTOZOOM_LEN	60

struct t32_apig_ops {
	void (*mask_out_cfg)(u32 ch, u8 *cfg);		/* tisp_msca_api_get_mask_out_cfg */
	void (*fcrop)(u32 ch, u8 *buf);			/* tisp_g_fcrop_control */
	void (*msca_par)(u32 ch, u8 *par);		/* tisp_msca_api_get_par */
	void (*brightness)(u32 ch, u8 *b);		/* tisp_bcsh_api_get_brightness */
	void (*autozoom)(u32 ch, u8 *buf);		/* tisp_msca_api_get_autozoom */
};

struct t32_apig {
	const struct t32_apig_ops *ops;
	const u8 *mask_attr;	/* t32_data_f3258 */
	const u8 *pipe;		/* t32_data_dc284 */
	u8 *mask_out;		/* t32_data_e4000 (one 24 B block for all channels) */
	u8 *msca_par;		/* t32_data_f0928, 2636 B per channel */
};

/*
 * tisp_api_param_array_get: copy parameter id of channel ch & 0xff into out
 * and its length into *len.  The vendor's result is a left-over register:
 * the length, except for the MSCA parameters (the out pointer) and an
 * unknown id (no copy, no length, debug print); 0 is returned for those.
 */
static inline u32 t32_api_param_array_get(struct t32_apig *g, u32 ch, u8 *out, u32 *len, u32 id)
{
	u8 buf[T32_API_AUTOZOOM_LEN];
	u32 n, w;

	ch &= 0xff;
	id &= 0xffff;
	memset(buf, 0, sizeof(buf));
	switch (id) {
	case T32_API_MASK_ATTR:
		n = T32_API_MASK_ATTR_LEN;
		memcpy(out, g->mask_attr + T32_API_MASK_ATTR_LEN * ch, n);
		break;
	case T32_API_PIPE_EN:
		n = 4;
		w = g->pipe[T32_API_PIPE_STRIDE * ch + T32_API_PIPE_BYTE];
		memcpy(out, &w, n);
		break;
	case T32_API_MASK_OUT:
		n = T32_API_MASK_OUT_LEN;
		g->ops->mask_out_cfg(ch, g->mask_out);
		memcpy(out, g->mask_out, n);
		break;
	case T32_API_MSCA_PAR3: case 4: case 5: case 6: case 8:
		g->ops->msca_par(ch, g->msca_par + T32_API_MSCA_PAR_LEN * ch);
		memcpy(out, g->msca_par + T32_API_MSCA_PAR_LEN * ch, T32_API_MSCA_PAR_LEN);
		*len = T32_API_MSCA_PAR_LEN;
		return 0;
	case T32_API_FCROP:
		n = T32_API_FCROP_LEN;
		g->ops->fcrop(ch, buf);
		memcpy(out, buf, n);
		break;
	case T32_API_AUTOZOOM:
		n = T32_API_AUTOZOOM_LEN;
		g->ops->autozoom(ch, buf);
		memcpy(out, buf, n);
		break;
	case T32_API_BRIGHT:
		n = 1;
		g->ops->brightness(ch, buf);
		memcpy(out, buf, n);
		break;
	default:
		if (id >= T32_API_IDS)
			return 0;
		n = 1;			/* 11 .. 16 */
		memcpy(out, buf, n);
		break;
	}
	*len = n;
	return n;
}

/* ---- tisp_day_or_night_par_refresh (vendor 0xafefc), oracle t32_dnref_oracle ---- */

enum t32_dn_blk {		/* the vendor's refresh order */
	T32_DN_TOP, T32_DN_DPC, T32_DN_GIB, T32_DN_AE, T32_DN_AWB, T32_DN_LSC, T32_DN_TMO, T32_DN_DMSC,
	T32_DN_GAMMA, T32_DN_CCM, T32_DN_CLM, T32_DN_BCSH, T32_DN_YSP, T32_DN_MDNS, T32_DN_YDNS,
	T32_DN_SDNS, T32_DN_CDNS, T32_DN_DEFOG, T32_DN_LCE, T32_DN_RDNS, T32_DN_DECMP, T32_DN_CSCCR,
	T32_DN_BLKS,
};

#define T32_DN_PIPE_STRIDE	904	/* t32_data_dc284: defog gets the first two words */

struct t32_dnr_ops {
	/* tisp_<blk>_dn_params_refresh(ch, dn); AE and SDNS take the channel only (dn is passed as 0
	 * here); DEFOG also gets w0 / w1 of the channel state */
	void (*refresh)(enum t32_dn_blk blk, u32 ch, u32 dn, u32 w0, u32 w1);
	u32 (*wake_up)(void *wq);		/* __wake_up(wq, 1, 1, 0) */
};

struct t32_dnr {
	const struct t32_dnr_ops *ops;
	const u8 *pipe;		/* t32_data_dc284 */
	u8 *done;		/* t32_data_e2764[ch] */
	u32 *count;		/* t32_data_f0908[ch] */
	u8 *ev;			/* t32_data_dad90: +0 flag, +2 day / night */
	void *wq;		/* t32_data_dad94 */
};

/* tisp_day_or_night_par_refresh: every block's day / night refresh, then signal the waiter */
static inline u32 t32_day_or_night_par_refresh(struct t32_dnr *d, u32 ch, u32 dn)
{
	const u8 *pipe;
	u32 b;

	ch &= 0xff;
	for (b = T32_DN_TOP; b < T32_DN_BLKS; b++) {
		if (b == T32_DN_AE || b == T32_DN_SDNS) {
			d->ops->refresh(b, ch, 0, 0, 0);
		} else if (b == T32_DN_DEFOG) {
			pipe = d->pipe + T32_DN_PIPE_STRIDE * ch;
			d->ops->refresh(b, ch, dn, t32_br_rd32(pipe), t32_br_rd32(pipe + 4));
		} else {
			d->ops->refresh(b, ch, dn, 0, 0);
		}
	}
	d->done[ch] = 1;
	d->ev[0] = 1;
	d->count[ch] = 0;
	d->ev[2] = dn;
	return d->ops->wake_up(d->wq);
}

/* ---- tisp_day_or_night_s_ctrl (vendor 0xb0144), oracle t32_dnctl_oracle ---- */

#define T32_DNC_BINS		6	/* t32_data_dd0b4: tuning bin per channel and index */
#define T32_DNC_TUNING_LEN	0xf238	/* copied into the working tuning t32_data_dd0a4[ch] */
#define T32_DNC_TUN_BYTE	22	/* tuning byte cleared for night */
#define T32_DNC_ST_STRIDE	336	/* t32_data_da850 per channel */
#define T32_DNC_ST_IDX		296
#define T32_DNC_ST_CUR		300
#define T32_DNC_ST_TAB		304	/* one word per index */
#define T32_DNC_PIPE_STRIDE	904	/* t32_data_dc284 */
#define T32_DNC_PIPE_NIGHT	37
#define T32_DNC_PIPE_SET	4	/* + (index + 1) * 128: 128 B set copied to da850[ch] */
#define T32_DNC_SET_LEN		128
#define T32_DNC_R_A		0x14054	/* night: 0 (shadow index 777) */
#define T32_DNC_S_A		777
#define T32_DNC_R_B		0x14010	/* night: 0xffffffff (shadow index 771) */
#define T32_DNC_S_B		771
#define T32_DNC_SEQ_MASK	0xffffff

struct t32_dnc_ops {
	void (*lut_params_copy)(u32 ch, u32 idx);		/* tisp_lut_params_copy */
	void (*reg_write)(u32 ch, u32 reg, u32 val);		/* tisp_reg_write */
	void (*par_refresh)(u32 ch, u32 dn);			/* tisp_day_or_night_par_refresh */
	void (*no_bin)(u32 idx);				/* isp_printf (missing bin) */
	void (*bad_mode)(void);					/* isp_printf (mode not 0 / 1) */
};

struct t32_dnc {
	const struct t32_dnc_ops *ops;
	const u32 *bins;	/* t32_data_dd0b4[ch * 6 + idx] */
	const u32 *tuning;	/* t32_data_dd0a4[ch] */
	u8 *st;			/* t32_data_da850 */
	u8 *pipe;		/* t32_data_dc284 */
	u8 *keep;		/* t32_data_e401c: 1 = keep da850's set once */
	const u8 *shadow;	/* t32_data_d03a0 */
	const u32 *seq;		/* t32_data_f32d8[ch] */
	u32 *seq1, *seq2;	/* t32_data_f32f8[ch] / f32e8[ch]: seq + 1 / + 2 (24 bit) */
};

/* tisp_day_or_night_s_ctrl: switch channel ch & 0xff to day (mode 0) or night (1) with bin idx & 0xff; 0 */
static inline int t32_day_or_night_s_ctrl(struct t32_dnc *d, u32 ch, u32 mode, u32 idx)
{
	u8 *st, *pipe;
	u32 bin;

	ch &= 0xff;
	idx &= 0xff;
	bin = d->bins[ch * T32_DNC_BINS + idx];
	if (!bin) {
		d->ops->no_bin(idx);
		return 0;
	}
	memcpy(t32_br_ptr(d->tuning[ch]), t32_br_ptr(bin), T32_DNC_TUNING_LEN);
	d->ops->lut_params_copy(ch, idx);
	st = d->st + T32_DNC_ST_STRIDE * ch;
	t32_br_wr32(st + T32_DNC_ST_IDX, idx);
	t32_br_wr32(st + T32_DNC_ST_CUR, t32_br_rd32(st + T32_DNC_ST_TAB + 4 * idx));
	pipe = d->pipe + T32_DNC_PIPE_STRIDE * ch;
	if (mode == 0) {
		pipe[T32_DNC_PIPE_NIGHT] = 0;
		if (!*d->keep)
			memcpy(st, pipe + T32_DNC_PIPE_SET + (idx + 1) * T32_DNC_SET_LEN, T32_DNC_SET_LEN);
		*d->keep = 0;
		d->ops->par_refresh(ch, 0);
		d->seq1[ch] = (d->seq[ch] + 1) & T32_DNC_SEQ_MASK;
		d->seq2[ch] = (d->seq[ch] + 2) & T32_DNC_SEQ_MASK;
	} else if (mode == 1) {
		pipe[T32_DNC_PIPE_NIGHT] = 1;
		d->ops->reg_write(ch, *d->shadow ? T32_DNC_S_A : T32_DNC_R_A, 0);
		d->ops->reg_write(ch, *d->shadow ? T32_DNC_S_B : T32_DNC_R_B, 0xffffffffu);
		if (!*d->keep)
			memcpy(st, pipe + T32_DNC_PIPE_SET + (idx + 1) * T32_DNC_SET_LEN, T32_DNC_SET_LEN);
		t32_br_ptr(d->tuning[ch])[T32_DNC_TUN_BYTE] = 0;
		*d->keep = 0;
		d->ops->par_refresh(ch, 1);
	} else {
		d->ops->bad_mode();
	}
	return 0;
}

#endif /* TX_ISP_T32_BLKREST_H */
