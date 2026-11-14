/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) LCE / LSF per-frame path (openimp #444): the state and the
 * functions behind the vendor's
 *
 *  - tisp_lce_interrupt_static (system irq 8, LCE statistics done):
 *    tisp_lce_get_lce_stat_data from the statistics part named by 0xc088,
 *    list 37 slot 4 = that part, then (not in the LSF mode) slot 5 = the
 *    histogram buffer, curve buffer half 2 -> half 1, list 16 slot 0x19 =
 *    the curve buffer, slot 1 = 1; list 16 started with one sensor; list 37
 *    slot 0x45 = 1 and list 37 started when 0x25040 reads 0;
 *  - tisp_lsf_interrupt_static (system irq 30, LSF histogram done):
 *    tisp_lce_get_hist_data, event 0xe;
 *  - tisp_lce_soft_process (event 0xe): the locked copies, Tisp_lce_soft
 *    (per zone tisp_lce_thres_search, tisp_lce_hist_method, the cdf,
 *    tisp_lce_hist_adjust, tisp_lce_16bit_data_converge), the debug zone
 *    copies, tisp_lce_write_all_reg (tisp_lce_write_curve into half 2);
 *  - the parts of tisp_lce_init the per-frame path reads:
 *    tisp_lce_bin_para_update, the init's EV / BV interpolations,
 *    tisp_lce_gausswgt_calc, tisp_lce_curve_init_default, and
 *    tisp_lsf_write_curve (the LSF buffer).
 *
 * struct t33_lce_pf is the vendor's lceInfo block in its layout (0x23dc8
 * bytes; the offsets are checked below), so that the host oracle
 * (tests/t33_lcepf_host.sh) can compare all of it.  Not filled here: the
 * list mirror func_lce_para_update keeps at 0x52c .. 0x62f4 (our lists are
 * built by tx_isp_t33_lce.h / tx_isp_t33_lsf.h) and the section pointer at
 * 0x62f8 (the section is an argument).
 *
 * Vendor behaviour kept: tisp_lce_hist_method's final spread of the rest
 * up to 0x8000 writes 4 x 8 halfwords at a stride of 9, i.e. bins 0..7,
 * 9..16, 18..25, 27..34: bins 8, 17 and 26 get nothing and the three
 * halfwords after the 32 bins (the next zone's bins 0..2, after the last
 * zone the first six bytes of the zone sums) get the share (spill).
 * The 0xab9c histogram the temporal blend of get_hist_data uses
 * (sec u16 0x354 == 1) starts at 0x400 per bin like the vendor's.
 *
 * Not ported here (vendor callers outside the irq / event path):
 * tisp_lce_ev_update (EV changes), tisp_lce_param_array_set / switch_para
 * (tuning API, day / night).
 */
#ifndef TX_ISP_T33_LCEPF_H
#define TX_ISP_T33_LCEPF_H

#include "tx_isp_t33_lsf.h"

#define T33_LCEPF_ZONES		288		/* 12 rows x 24 columns */
#define T33_LCEPF_ROWS		12
#define T33_LCEPF_COLS		24
#define T33_LCEPF_BYTES		0x23dc8U
#define T33_LCE_STAT_PART	0x4d08U		/* one of the four statistics parts */
#define T33_LCE_CURVE_HALF	0x1680U
#define T33_LCEPF_EV_LCE	0xe		/* event number of tisp_lce_soft_process */
#define T33_LCE_IDX_REG		0xc088U		/* bits 0..1: statistics part */
#define T33_LSF_BUSY_REG	0x25040U	/* LSF start word: 0 = idle */
#define T33_LSF_DATAOUT_REG	0x25220U	/* bit 0: the LSF writes curves (else histograms) */
#define T33_LCE_SLOT_CURVE	0x19		/* list 16: 0xc0a4 */
#define T33_LCE_SLOT_EN		1		/* list 16: 0xc010 */

struct t33_lce_pf {
	u32 p[0x14b];			/* 0x00000 tisp_lce_bin_para_update words, EV / BV values, gauss weights */
	u8 mirror[0x62f4 - 0x52c];	/* 0x0052c func_lce_para_update's list mirror (not used here) */
	u32 r62f4;			/* 0x062f4 */
	u32 secp;			/* 0x062f8 the vendor's section pointer (unused) */
	u8 show_cdf[16];		/* 0x062fc debug zone: cdf, every other byte */
	u8 show_cur[16];		/* 0x0630c debug zone: curve, every other byte */
	u16 show_h0[32];		/* 0x0631c debug zone: histogram before hist_method */
	u16 show_h1[32];		/* 0x0635c debug zone: histogram after it */
	u16 hist[T33_LCEPF_ZONES * 32];	/* 0x0639c LSF histograms (irq 30) */
	u16 prev[T33_LCEPF_ZONES * 32];	/* 0x0ab9c the blend's previous histograms */
	u32 zsum[T33_LCEPF_ZONES];	/* 0x0f39c zone sums (irq 8) */
	u16 stat[T33_LCEPF_ZONES * 32];	/* 0x0f81c LCE zone histograms (irq 8) */
	u32 glb[32];			/* 0x1401c global histogram (irq 8) */
	u32 sumy;			/* 0x1409c global luma sum (irq 8) */
	u16 lsfgl[32];			/* 0x140a0 LSF global histogram (irq 30) */
	u8 lsfcur[T33_LCE_CURVE_HALF];	/* 0x140e0 LSF curves (irq 30, dataout 1) */
	u16 w_hist[T33_LCEPF_ZONES * 32];	/* 0x15760 soft_process copies */
	u32 w_zsum[T33_LCEPF_ZONES];	/* 0x19f60 */
	u8 gap[0x1ebe0 - 0x1a3e0];	/* 0x1a3e0 */
	u8 cur20[T33_LCEPF_ZONES * 20];	/* 0x1ebe0 the curve (20 points per zone) */
	u8 cur32[T33_LCEPF_ZONES * 32];	/* 0x20260 the 32 point curves of the last frame */
	u32 w_glb[32];			/* 0x22660 */
	u32 w_sumy;			/* 0x226e0 */
	u16 w_lsfgl[32];		/* 0x226e4 */
	u8 w_lsfcur[T33_LCE_CURVE_HALF];	/* 0x22724 */
	u32 frames;			/* 0x23da4 soft_process runs */
	u32 r23da8;
	u32 bv;				/* 0x23dac */
	u32 ev;				/* 0x23db0 */
	u8 r23db4[0x23dc2 - 0x23db4];	/* 0x23db4 (0x23dc1: the vendor's lock) */
	u16 h;				/* 0x23dc2 */
	u16 w;				/* 0x23dc4 */
	u16 r23dc6;
};

#define T33_LCEPF_AT(m, o) _Static_assert(__builtin_offsetof(struct t33_lce_pf, m) == (o), "lceInfo layout " #m)
T33_LCEPF_AT(r62f4, 0x62f4);
T33_LCEPF_AT(show_cdf, 0x62fc);
T33_LCEPF_AT(hist, 0x639c);
T33_LCEPF_AT(prev, 0xab9c);
T33_LCEPF_AT(zsum, 0xf39c);
T33_LCEPF_AT(stat, 0xf81c);
T33_LCEPF_AT(glb, 0x1401c);
T33_LCEPF_AT(lsfgl, 0x140a0);
T33_LCEPF_AT(lsfcur, 0x140e0);
T33_LCEPF_AT(w_hist, 0x15760);
T33_LCEPF_AT(w_zsum, 0x19f60);
T33_LCEPF_AT(cur20, 0x1ebe0);
T33_LCEPF_AT(cur32, 0x20260);
T33_LCEPF_AT(w_glb, 0x22660);
T33_LCEPF_AT(w_lsfgl, 0x226e4);
T33_LCEPF_AT(w_lsfcur, 0x22724);
T33_LCEPF_AT(frames, 0x23da4);
T33_LCEPF_AT(bv, 0x23dac);
T33_LCEPF_AT(h, 0x23dc2);
_Static_assert(sizeof(struct t33_lce_pf) == T33_LCEPF_BYTES, "lceInfo size");

/* the locked part of soft_process; the includer may define these (kernel: a spinlock) */
#ifndef T33_LCEPF_LOCK
#define T33_LCEPF_LOCK(c)	do { } while (0)
#define T33_LCEPF_UNLOCK(c)	do { } while (0)
#endif

static inline u32 t33_lcepf_le32(const u8 *p)
{
	return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

/* tisp_lce_bin_para_update: section bytes into the parameter words */
static inline void t33_lcepf_bin_para(struct t33_lce_pf *c, const u8 *s)
{
	u32 *p = c->p, i;

	for (i = 0; i < 6; i++)
		p[i] = s[4 + i];
	for (i = 0; i < 9; i++)
		p[6 + i] = t32_tp_le16(s + 10 + 2 * i);
	for (i = 0; i < 3; i++)
		p[0xf + i] = s[0x1c + i];
	for (i = 0; i < 32; i++) {
		p[0x19 + i] = s[0x6c + i];
		p[0x39 + i] = s[0x8c + i];
		p[0x7e + i] = s[0x117 + i];
		p[0x9e + i] = s[0x137 + i];
		p[0xce + i] = s[0x1a3 + i];
		p[0xee + i] = s[0x1c3 + i];
	}
	for (i = 0; i < 12; i++)
		p[0x59 + i] = s[0xac + i];
	for (i = 0; i < 8; i++)
		p[0x66 + i] = s[0xc3 + i];
	p[0x6e] = s[0xcb];
	for (i = 0; i < 3; i++) {
		p[0x70 + i] = s[0xd7 + i];
		p[0xc0 + i] = s[0x163 + i];
		p[0x118 + i] = s[0x1f8 + i];
	}
	for (i = 0; i < 6; i++) {
		p[0x78 + i] = s[0x111 + i];
		p[200 + i] = s[0x19d + i];
	}
	p[0xbe] = s[0x157];
	for (i = 0; i < 10; i++)
		p[0x10e + i] = t32_tp_le16(s + 0x1e4 + 2 * i);
	for (i = 0; i < 4; i++)
		p[0x124 + i] = t32_tp_le16(s + 0x2c2 + 2 * i);
	p[0x128] = s[0x2ca];
	for (i = 0; i < 21; i++)
		p[0x129 + i] = s[0x2cb + i];
	p[0x13e] = s[0x2e0];
	p[0x13f] = t33_lcepf_le32(s + 0x2e4);
	p[0x140] = s[0x2e8];
	p[0x141] = s[0x2e9];
	for (i = 0; i < 8; i++)
		p[0x142 + i] = t32_tp_le16(s + 0x2ea + 2 * i);
	p[0x14a] = s[0x2fa];
}

/*
 * The init's EV (sec[0x3d9] == 0, knots 0x2fc) or BV (knots 0x328)
 * interpolations of the 11-point tables: 20 u8 tables (word, table) and
 * the nine u16 curve point tables at 0x1fc (words 0x46c ..).
 */
static inline void t33_lcepf_ev_par(struct t33_lce_pf *c, const u8 *s)
{
	static const u16 tab[20][2] = {
		{ 0x48, 0x1f }, { 0x4c, 0x2a }, { 0x50, 0x35 }, { 0x54, 0x40 }, { 0x58, 0x4b },
		{ 0x5c, 0x56 }, { 0x60, 0x61 }, { 0x194, 0xb8 }, { 0x1bc, 0xcc }, { 0x1cc, 0xda },
		{ 0x1d0, 0xe5 }, { 0x1d4, 0xf0 }, { 0x1d8, 0xfb }, { 0x1dc, 0x106 }, { 0x2fc, 0x158 },
		{ 0x30c, 0x166 }, { 0x310, 0x171 }, { 0x314, 0x17c }, { 0x318, 0x187 }, { 0x31c, 0x192 },
	};
	int bv = s[0x3d9] != 0;
	u32 x = bv ? c->bv : c->ev, k;
	const u8 *e = s + (bv ? 0x328 : 0x2fc);

	for (k = 0; k < 20; k++)
		c->p[tab[k][0] / 4] = t33_intp_u8(x, e, s + tab[k][1], bv);
	for (k = 0; k < 9; k++)
		c->p[0x46c / 4 + k] = t33_intp_u16(x, e, s + 0x1fc + 0x16 * k, bv);
}

/* tisp_lce_gausswgt_calc: the up / down weights (words 0x64 / 0xe4), also into the section (0x6c / 0x8c) */
static inline void t33_lcepf_gausswgt(struct t33_lce_pf *c, u8 *s)
{
	u8 up[32], dn[32];
	u32 i;

	t33_lce_gauss_wgt(c->p[0x4c / 4] & 0xff, c->p[0x50 / 4] & 0xff, c->p[0x54 / 4] & 0xff, up);
	t33_lce_gauss_wgt(c->p[0x58 / 4] & 0xff, c->p[0x5c / 4] & 0xff, c->p[0x60 / 4] & 0xff, dn);
	for (i = 0; i < 32; i++) {
		c->p[0x64 / 4 + i] = up[i];
		c->p[0xe4 / 4 + i] = dn[i];
		s[0x6c + i] = up[i];
		s[0x8c + i] = dn[i];
	}
}

/* tisp_lce_curve_init_default, the lceInfo part: default curves, 32 point identity, 0x400 blend histograms */
static inline void t33_lcepf_curve_default(struct t33_lce_pf *c)
{
	u32 i;

	for (i = 0; i < sizeof(c->cur20); i++) {	/* 0, 8 .. 0x38, 0x40, 0x50 .. 0xf0 per zone */
		u32 k = i % 20;

		c->cur20[i] = (u8)(k < 8 ? 8 * k : 0x40 + 16 * (k - 8));
	}
	for (i = 0; i < sizeof(c->cur32); i++)
		c->cur32[i] = (u8)(8 * (i & 31));
	for (i = 0; i < T33_LCEPF_ZONES * 32; i++)
		c->prev[i] = 0x400;
}

/*
 * The lceInfo state tisp_lce_init leaves (but the list mirror and the
 * section pointer): s is the driver's copy of section 15 after
 * t33_lce_size (gausswgt writes its weights into it), w x h the sensor size,
 * EV and BV 0 as at the vendor's init.
 */
static inline void t33_lcepf_init(struct t33_lce_pf *c, u8 *s, u32 w, u32 h)
{
	memset(c, 0, sizeof(*c));
	c->h = (u16)h;
	c->w = (u16)w;
	t33_lcepf_curve_default(c);
	t33_lcepf_bin_para(c, s);
	t33_lcepf_ev_par(c, s);
	t33_lcepf_gausswgt(c, s);
	c->r62f4 = 0x1e0;	/* func_lce_para_update's last store */
}

/*
 * tisp_lsf_write_curve: the 32 point identity (0, 8 .. 248) 289 times into the LSF buffer (its whole 0x2420
 * bytes: the vendor's second loop starts at word 8 and runs to word 0x900 + 8).
 */
static inline void t33_lsf_write_curve(u32 *buf)
{
	u32 i;

	for (i = 0; i < T33_LSF_BUF_BYTES / 4; i++)
		buf[i] = (32 * (i & 7)) | (32 * (i & 7) + 8) << 8 | (32 * (i & 7) + 16) << 16 |
			 (u32)(32 * (i & 7) + 24) << 24;
}

/* tisp_lce_get_lce_stat_data: one statistics part (rows of 0x660 bytes: 24 x 32 u16 bins, 24 u32 sums), then 0x4c80 */
static inline void t33_lcepf_stat(struct t33_lce_pf *c, const u8 *b)
{
	u32 r, i;

	for (r = 0; r < T33_LCEPF_ROWS; r++) {
		const u8 *row = b + r * 0x660;

		for (i = 0; i < T33_LCEPF_COLS * 32; i++)
			c->stat[r * T33_LCEPF_COLS * 32 + i] = (u16)(t32_tp_le16(row + 2 * i) & 0x3fff);
		for (i = 0; i < T33_LCEPF_COLS; i++)
			c->zsum[r * T33_LCEPF_COLS + i] = t33_lcepf_le32(row + 0x600 + 4 * i) & 0x3fffff;
	}
	for (i = 0; i < 32; i++)
		c->glb[i] = t33_lcepf_le32(b + 0x4c80 + 4 * i) & 0x3fffff;
	c->sumy = t33_lcepf_le32(b + 0x4d00) & 0x3fffffff;
}

/*
 * tisp_lce_get_hist_data: the LSF buffer (0x4840 bytes).  dataout 0: the
 * global histogram (32 u16) and 288 x 32 u16 zone histograms (15 bits),
 * blended with the previous ones when sec u16 0x354 == 1 (weight sec u16
 * 0x356 of 128); dataout 1: 288 x 20 byte curves.
 */
static inline void t33_lcepf_hist(struct t33_lce_pf *c, const u8 *s, const u8 *b, u32 dataout)
{
	u32 sm = t32_tp_le16(s + 0x356), i;
	int om = 0x80 - (int)sm;

	memset(c->lsfcur, 0, sizeof(c->lsfcur));
	memset(c->lsfgl, 0, sizeof(c->lsfgl));
	memset(c->hist, 0, sizeof(c->hist));
	if (dataout) {
		memcpy(c->lsfcur, b, sizeof(c->lsfcur));
		return;
	}
	for (i = 0; i < 32; i++)
		c->lsfgl[i] = (u16)(t32_tp_le16(b + 2 * i) & 0x7fff);
	for (i = 0; i < T33_LCEPF_ZONES * 32; i++)
		c->hist[i] = (u16)(t32_tp_le16(b + 0x40 + 2 * i) & 0x7fff);
	if (t32_tp_le16(s + 0x354) != 1)
		return;
	for (i = 0; i < T33_LCEPF_ZONES * 32; i++)
		c->hist[i] = (u16)((int)((u32)c->hist[i] * sm + (u32)c->prev[i] * (u32)om + 0x40) >> 7);
	memcpy(c->prev, c->hist, sizeof(c->prev));
}

/* tisp_lce_hist_adjust: the 32 point cdf a moves away from the identity (8 i) by up / down (Q8) */
static inline void t33_lcepf_hist_adjust(u8 *a, const u32 *up, const u32 *dn)
{
	u32 i;

	for (i = 0; i < 32; i++) {
		int d = (int)a[i] - (int)(8 * i), adj = 0;

		if (d > 0)
			adj = d * (int)up[i];
		else if (d < 0)
			adj = d * (int)dn[i];
		a[i] = (u8)(((8 * i) * 0x100 + 0x80 + (u32)adj) >> 8);
	}
}

/* tisp_lce_16bit_data_converge (n = 32): cur follows tgt, the step (gain, Q7) limited to lo .. hi */
static inline void t33_lcepf_converge(u32 gain, u32 lo, u32 hi, const u8 *tgt, u8 *cur)
{
	u32 i;

	lo &= 0xff;
	hi &= 0xff;
	for (i = 0; i < 32; i++) {
		u32 t = tgt[i], v = cur[i];
		int d;

		if (t + lo < v) {
			d = (int)((v - t) * gain + 0x40) >> 7;
			if (d < (int)lo)
				cur[i] = (u8)(t + lo);
			else if ((int)hi < d)
				cur[i] = (u8)(t + hi);
			else
				cur[i] = (u8)(t + d);
		} else if (v + lo < t) {
			d = (int)((t - v) * gain + 0x40) >> 7;
			if (d < (int)lo)
				cur[i] = (u8)(t - lo);
			else if (d <= (int)hi)
				cur[i] = (u8)(t - d);
			else
				cur[i] = (u8)(t - hi);
		}
	}
}

/* the triangle number table of tisp_lce_hist_method (rodata): (k + 1) (k + 2) / 2 */
static inline u32 t33_lcepf_tri(int k)
{
	return (u32)((k + 1) * (k + 2) / 2);
}

/* ramp up i + 1 / ramp down 32 - i (rodata) */
#define T33_UP(i)	((u32)(i) + 1)
#define T33_DN(i)	(32U - (u32)(i))
#define T33_RAMP(x, n, d)	((u16)(((x) * (n) + ((d) >> 1)) / (d)))

/*
 * tisp_lce_hist_method: clip the 32 bins at clip[0], spread the clipped mass
 * by ramps over the bins (par = lo, mid, hi from tisp_lce_thres_search),
 * with clip2 (sec[0xc5] == 1) clip the spread at clip[1] and spread that
 * excess evenly over mid .. 31 (str <= 0x80) or 0 .. mid - 2, add it, and
 * fill the total up to 0x8000 (see the spill note at the top).
 */
static inline void t33_lcepf_hist_method(u16 *h, u16 *spill, const u16 *clip, const u8 *par, u32 str, int clip2)
{
	u16 w[32];
	u32 lo = par[0], mid = par[1], hi = par[2], exc = 0, half = 0, q = 0, q2 = 0, rem = 0, rest = 0, d, i;
	int narrow, ramp, sum = 0;

	memset(w, 0, sizeof(w));
	for (i = 0; i < 32; i++) {
		if (clip[0] < h[i]) {
			exc = (exc - clip[0] + h[i]) & 0xffff;
			h[i] = clip[0];
		}
	}
	narrow = (int)(mid - lo) < 3;
	if (narrow)
		half = (exc + 1) >> 1;
	else
		q = (exc + 2) >> 2;
	ramp = !((int)(hi - mid) < 3);
	if (!ramp) {
		rem = narrow ? (exc - half) & 0xffff : (exc - 2 * q) & 0xffff;
	} else {
		q2 = (exc + 2) >> 2;
		rest = narrow ? (exc - half - q2) & 0xffff : (exc - 2 * q - q2) & 0xffff;
	}
	if (narrow) {
		if (hi == 0) {
			hi = 1;
			mid = 1;
		}
		if (mid == 0) {
			w[0] = (u16)half;	/* T33_UP(0) / tri(0) */
			if (!ramp)
				goto tail_a;
			goto tail_b;
		}
		d = t33_lcepf_tri(mid - 1);
		for (i = 0; i < mid; i++)
			w[i] = T33_RAMP(T33_UP(i), half, d);
	} else {
		if (hi == 0) {
			hi = 1;
			mid = 1;
			lo = 1;
		}
		d = t33_lcepf_tri(lo);
		for (i = 0; i <= lo; i++)
			w[i] = T33_RAMP(T33_DN(31 - lo + i), q, d);
		if (lo + 1 < mid) {
			d = t33_lcepf_tri(mid - lo - 2);
			for (i = lo + 1; i < mid; i++)
				w[i] = T33_RAMP(T33_UP(i - lo - 1), q, d);
		}
	}
	if (ramp)
		goto tail_b;
	if (mid == 31) {
		w[31] = (u16)rem;		/* T33_DN(31) / tri(0) */
		goto clip_2;
	}
tail_a:
	d = t33_lcepf_tri(30 - mid);
	for (i = mid + 1; i < 32; i++)
		w[i] = T33_RAMP(T33_DN(i), rem, d);
	goto clip_2;
tail_b:
	d = t33_lcepf_tri(hi - mid) - 1;
	for (i = mid; (int)i < (int)hi; i++)
		w[i] = T33_RAMP(T33_DN((31 - hi + i) & 0xff), q2, d);
	d = t33_lcepf_tri(31 - hi);
	for (i = hi; i < 32; i++)
		w[i] = T33_RAMP(T33_UP(i - hi), rest, d);
clip_2:
	if (clip2) {
		u32 e2 = 0;

		for (i = 0; i < 32; i++) {
			if (clip[1] < w[i]) {
				e2 = (e2 - clip[1] + w[i]) & 0xffff;
				w[i] = clip[1];
			}
		}
		if (e2) {
			int n = str <= 0x80 ? 0x21 - (int)mid : (int)mid - 1, r, k;
			u32 from = str <= 0x80 ? mid : 0, to = str <= 0x80 ? 32 : mid - 1;

			if (n) {
				r = (int)e2 % n;
				for (k = (int)from; k < (int)to; k++) {
					w[k] = (u16)(w[k] + (int)e2 / n + (r > 0));
					if (r > 0)
						r--;
				}
			}
		}
	}
	for (i = 0; i < 32; i++) {
		h[i] = (u16)(h[i] + w[i]);
		sum += h[i];
	}
	if (sum < 0x8000) {
		u32 rest32 = 0x8000U - (u32)sum, add = (rest32 >> 5) & 0xffff, g, k;

		for (i = 0; i < (rest32 & 0x1f); i++)
			h[i]++;
		for (g = 0; g < 4; g++)
			for (k = 0; k < 8; k++) {
				u32 at = 9 * g + k;
				u16 *p = at < 32 ? &h[at] : &spill[at - 32];

				*p = (u16)(*p + add);
			}
	}
}

/*
 * Tisp_lce_soft on the soft_process copies: per zone thres_search, the
 * strength from the zone sum (rounded Q24 with the cell reciprocal),
 * hist_method, the 32 point cdf (Q7 of the running sum, before the bin),
 * hist_adjust, converge to the last frame's curve unless upd == 4, the new
 * 32 point curve and its 20 point form (points 0..8, then every other one).
 */
static inline void t33_lcepf_soft(struct t33_lce_pf *c, const u8 *s, u32 upd)
{
	static const u16 thr[3] = { 0x2000, 0x4000, 0x6000 };
	u32 rh = c->p[4], rw = c->p[5], g = c->p[0x78], lo = c->p[0x79] & 0xff, hi = c->p[0x7a] & 0xff;
	u32 srow = s[0x3d6], scol = s[0x3d7], row, col, i;
	u16 clip[2];
	u8 par[3] = { 0, 0, 0 }, a[32];

	clip[0] = (u16)(c->p[0x10] << 10);
	clip[1] = (u16)(c->p[0x11] << 10);
	upd &= 0xff;
	for (row = 0; row < T33_LCEPF_ROWS; row++) {
		u32 t = rh < row ? 0 : 2;

		for (col = 0; col < T33_LCEPF_COLS; col++) {
			u32 z = row * T33_LCEPF_COLS + col, v, cum = 0;
			u16 *h = &c->w_hist[z * 32];
			u16 *spill = z + 1 < T33_LCEPF_ZONES ? h + 32 : (u16 *)c->w_zsum;
			int show = srow == row && scol == col;

			t32_lce_thres_search(h, thr, par);
			if (show)
				memcpy(c->show_h0, h, 0x40);
			v = c->p[0x146 + (rw < col ? t : t + 1)] * c->w_zsum[z];
			t33_lcepf_hist_method(h, spill, clip, par, ((v >> 24) + ((v >> 23) & 1)) & 0xff,
					      s[0xc5] == 1);
			if (show)
				memcpy(c->show_h1, h, 0x40);
			for (i = 0; i < 32; i++) {
				if (cum >> 15)
					cum = 0x7fff;
				a[i] = (u8)(cum >> 7);
				cum += h[i];
			}
			if (show)
				for (i = 0; i < 16; i++)
					c->show_cdf[i] = a[2 * i];
			t33_lcepf_hist_adjust(a, &c->p[0x64 / 4], &c->p[0xe4 / 4]);
			if (upd != 4)
				t33_lcepf_converge(g, lo, hi, &c->cur32[z * 32], a);
			memcpy(&c->cur32[z * 32], a, 32);
			if (show)
				for (i = 0; i < 16; i++)
					c->show_cur[i] = a[2 * i];
			for (i = 0; i < 9; i++)
				c->cur20[z * 20 + i] = a[i];
			for (i = 0; i < 11; i++)
				c->cur20[z * 20 + 9 + i] = a[10 + 2 * i];
		}
	}
}

/* tisp_lce_write_curve: the 20 point curves into curve buffer half 2 (word writes, little endian) */
static inline void t33_lcepf_write_curve(const struct t33_lce_pf *c, u32 *half2)
{
	u32 i;

	for (i = 0; i < T33_LCE_CURVE_HALF / 4; i++)
		half2[i] = t33_lcepf_le32(c->cur20 + 4 * i);
}

/*
 * tisp_lce_soft_process (event 0xe).  dataout: 0x25220 bit 0; half2: curve
 * buffer half 2.  Returns 1 when the curve was written (the vendor's
 * tisp_lce_write_all_reg; its list part ran once at init: lce_frame_flag).
 */
static inline int t33_lcepf_process(struct t33_lce_pf *c, u8 *s, u32 dataout, u32 *half2)
{
	u32 srow = s[0x3d6], scol = s[0x3d7], mode = s[0x2fb], z, k;
	int wrote = 0;

	dataout &= 1;
	if (!dataout || mode != dataout) {
		T33_LCEPF_LOCK(c);
		memcpy(c->w_hist, c->hist, sizeof(c->w_hist));
		memcpy(c->w_zsum, c->zsum, sizeof(c->w_zsum));
		memcpy(c->w_glb, c->glb, sizeof(c->w_glb));
		c->w_sumy = c->sumy;
		memcpy(c->w_lsfgl, c->lsfgl, sizeof(c->w_lsfgl));
		memcpy(c->w_lsfcur, c->lsfcur, sizeof(c->w_lsfcur));
		T33_LCEPF_UNLOCK(c);
		if (!dataout)
			t33_lcepf_soft(c, s, s[5]);
		else
			memcpy(c->cur20, c->w_lsfcur, sizeof(c->cur20));
		s[5] = 0;
		t33_lcepf_write_curve(c, half2);
		wrote = 1;
		c->frames = c->frames + 1 > 0x1fffffff ? 0xf : c->frames + 1;
		if (!dataout)
			return wrote;
	}
	if (srow < T33_LCEPF_ROWS && scol < T33_LCEPF_COLS) {
		z = srow * T33_LCEPF_COLS + scol;
		memcpy(c->show_h0, &c->stat[z * 32], 0x40);
		memcpy(c->show_h1, c->show_h0, 0x40);
		for (k = 0; k < 4; k++)
			c->show_cdf[k] = c->cur20[z * 20 + 2 * k];
		for (k = 0; k < 12; k++)
			c->show_cdf[4 + k] = c->cur20[z * 20 + 8 + k];
		memcpy(c->show_cur, c->show_cdf, 16);
	}
	return wrote;
}

/*
 * tisp_lsf_interrupt_static (irq 30) without the event push: b the LSF
 * histogram buffer (0x4840 bytes, uncached); d_irq / d_hist 0x25220 as the
 * vendor reads it twice (the irq's mode check, then get_hist_data's own).
 */
static inline void t33_lcepf_irq30(struct t33_lce_pf *c, const u8 *s, const u8 *b, u32 d_irq, u32 d_hist)
{
	t33_lcepf_hist(c, s, b, d_hist & 1);
	if ((d_irq & 1) && s[0x2fb] == 1) {
		memcpy(c->w_lsfcur, c->lsfcur, sizeof(c->w_lsfcur));
		memcpy(c->cur20, c->w_lsfcur, sizeof(c->cur20));
	}
}

/*
 * tisp_lce_interrupt_static (irq 8) without the list starts: idx = 0xc088,
 * stat / stat_pa the statistics buffer, hist_pa the LSF histogram buffer,
 * curve / curve_pa the curve buffer, l16 / l37 the two lists.  The caller
 * then starts list 16 (one sensor) and, when 0x25040 reads 0, list 37.
 */
static inline void t33_lcepf_irq8(struct t33_lce_pf *c, const u8 *s, u32 idx, const u8 *stat, u32 stat_pa,
				  u32 hist_pa, u8 *curve, u32 curve_pa, u32 *l16, u32 *l37)
{
	u32 part = (idx & 3) * T33_LCE_STAT_PART;

	t33_lcepf_stat(c, stat + part);
	T33_SW(l37, T33_LSF_SLOT_STAT, stat_pa + part);
	if (s[0x2fb] != 1 || s[0xc8] != 1) {
		T33_SW(l37, T33_LSF_SLOT_HIST, hist_pa);
		memcpy(curve, curve + T33_LCE_CURVE_HALF, T33_LCE_CURVE_HALF);
		T33_SW(l16, T33_LCE_SLOT_CURVE, curve_pa);
		T33_SW(l16, T33_LCE_SLOT_EN, 1);
	}
}

/* the end of tisp_lce_interrupt_static: list 37's start word, before the 0x25040 check */
static inline void t33_lcepf_irq8_lsf(u32 *l37)
{
	T33_SW(l37, T33_LSF_SLOT_START, 1);
}

#endif /* TX_ISP_T33_LCEPF_H */
