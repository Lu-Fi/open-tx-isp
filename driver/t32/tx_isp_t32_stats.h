/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 AE and AWB statistics engines: the vendor's initialisation from the IQ
 * bin (S4), as register writes in the vendor's order.
 *
 * Reverse engineered flow (PRJ007 driver): tisp_ae_malloc_cfg,
 * tisp_ae_set_hardware_param, tisp_ae_roi_point (from tisp_ae_init), and
 * tisp_awb_malloc_cfg, tisp_awb_set_hardware_param,
 * tisp_awb_set_regional_threshold, tisp_awb_set_lum_th_freq (from
 * tisp_awb_init); tisp_simple_intp_uint16 for the EV dependent thresholds.
 * Checked against the vendor's writes in the QEMU trace (run v1, cv8001
 * PRJ007 bin, 3840x2160).
 *
 * Parameter blocks: IQ bin sections 1 (AE, bin + 0x104) and 2 (AWB,
 * bin + 0xddc), found by the vendor's section walk (tx_isp_t32_iq.h).  The
 * vendor copies them to tparamsP + 0x68 / + 0xd40; the 4 bytes between the
 * bin and tparamsP offsets come from its per-section copy, not from a
 * different pointer (the former open question).
 *
 * The AE and AWB parts are checked against the vendor flow (Matt Davis' recovered C,
 * PR #15) by tests/t32_oracle (t32_ae_oracle, t32_awb_oracle).  AE: the vendor writes one
 * zone u16 per counted zone into a 16-entry table without a bound; ours stops at 15
 * (the oracle uses the real range, counts <= 15).  AWB: the vendor tisp_awb_init's writes to the AWB
 * block (0x16xxx) equal t32_awb_stats_init's, including the section fields that
 * tisp_awb_params_refresh rewrites from the sensor size.
 */
#ifndef TX_ISP_T32_STATS_H
#define TX_ISP_T32_STATS_H

#ifndef __KERNEL__
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

/* IQ bin sections 1 and 2 (tx_isp_t32_iq.h; the vendor's tparamsP + 0x68 / + 0xd40) */
#define T32_AE_PARAM		0x104U
#define T32_AWB_PARAM		0xddcU
#define T32_AE_BUF_BYTES	0x6000U		/* 4 zone buffers of 4 KiB + 4 histograms of 2 KiB */
#define T32_AWB_BUF_BYTES	0x1c000U	/* 4 buffers of 0x7000 */
#define T32_STATS_ID		0x0134da11U
#define T32_AWB_INIT_EV		1024U		/* tisp_awb_init */

#define T32_AE_TRIG		0x17010
#define T32_AE_ID		0x17004
#define T32_AWB_TRIG		0x16010
#define T32_STATS_MAX_WRITES	48

struct t32_stats_write {
	u32 reg, val;
};

static inline u32 t32_st16(const u8 *p, u32 o) { return p[o] | (u32)p[o + 1] << 8; }

static inline u16 t32_intp_u16(u32 i, u32 frac, const u8 *t)
{
	u32 a, b, d, v;

	if (i >= 10)
		return (u16)t32_st16(t, 20);
	a = t32_st16(t, 2 * i);
	b = t32_st16(t, 2 * i + 2);
	if (a == b)
		return (u16)a;
	d = (a < b ? b - a : a - b) * frac;
	v = (d >> 16) + ((d >> 15) & 1);
	return (u16)(a < b ? a + v : a - v);
}

static inline unsigned int t32_stats_add(struct t32_stats_write *w, unsigned int n, u32 reg, u32 val)
{
	if (n < T32_STATS_MAX_WRITES) {
		w[n].reg = reg;
		w[n].val = val;
	}
	return n + 1;
}

/* the 4 bytes p16[o + 6], p16[o + 4], p16[o + 2], p16[o] as one word */
static inline u32 t32_st_bytes4(const u8 *p, u32 o)
{
	return t32_st16(p, o + 6) << 24 | t32_st16(p, o + 4) << 16 | t32_st16(p, o + 2) << 8 | t32_st16(p, o);
}

static inline u32 t32_bytes4(const u16 *z, unsigned int i)
{
	return (u32)z[i + 3] << 24 | (u32)z[i + 2] << 16 | (u32)z[i + 1] << 8 | z[i];
}

/* AE: buffers at buf_pa (T32_AE_BUF_BYTES), sensor width x height */
static inline unsigned int t32_ae_stats_init(const u8 *bin, u32 width, u32 height, u32 buf_pa,
					     struct t32_stats_write *w)
{
	const u8 *p = bin + T32_AE_PARAM;
	u32 x0 = t32_st16(p, 2768), nx_v = t32_st16(p, 2770), y0 = t32_st16(p, 2772), nx = t32_st16(p, 2774);
	u32 lum = t32_st16(p, 2778), hw, hh, cx, cy;
	u16 zh[16] = { 0 }, zv[16] = { 0 };
	unsigned int n = 0, i;

	for (i = 0; i < 4; i++)
		n = t32_stats_add(w, n, 0x17068 + 4 * i, buf_pa + i * 0x1000);
	for (i = 0; i < 4; i++)
		n = t32_stats_add(w, n, 0x17078 + 4 * i, buf_pa + 0x4000 + i * 0x800);
	n = t32_stats_add(w, n, 0x17088, 0x10010033);
	/* zones: half picture over the zone count (tisp_ae_set_hardware_param) */
	for (i = 0; i < nx && i < 15; i++)
		zh[i] = (u16)(((width - y0 + 1) / 2) / nx);
	for (i = 0; i < nx_v && i < 15; i++)
		zv[i] = (u16)(((height - x0 + 1) / 2) / nx_v);
	n = t32_stats_add(w, n, 0x17040, nx << 28 | y0 << 16 | nx_v << 12 | x0);
	n = t32_stats_add(w, n, 0x17044, t32_bytes4(zh, 0));
	n = t32_stats_add(w, n, 0x17048, t32_bytes4(zh, 4));
	n = t32_stats_add(w, n, 0x1704c, t32_bytes4(zh, 8));
	n = t32_stats_add(w, n, 0x17050, (u32)zh[14] << 16 | (u32)zh[13] << 8 | zh[12]);
	n = t32_stats_add(w, n, 0x17054, t32_bytes4(zv, 0));
	n = t32_stats_add(w, n, 0x17058, t32_bytes4(zv, 4));
	n = t32_stats_add(w, n, 0x1705c, t32_bytes4(zv, 8));
	n = t32_stats_add(w, n, 0x17060, (u32)zv[14] << 16 | (u32)zv[13] << 8 | zv[12]);
	if (lum < 255)		/* the vendor raises values below 255 to 255 */
		lum = 255;
	n = t32_stats_add(w, n, 0x17064, 0x110000 | (lum * 2 / 3) << 8 | t32_st16(p, 2776));
	/* centre ROI: an eighth or a quarter of the size around the centre */
	cx = width >> 1;
	cy = height >> 1;
	if (width * height / 4 >= 0x100000) {
		hw = (width >> 3) & 0xffff;
		hh = (height >> 3) & 0xffff;
	} else {
		hw = (width >> 2) & 0xffff;
		hh = (height >> 2) & 0xffff;
	}
	n = t32_stats_add(w, n, 0x17090, ((cx + hw) & 0xffff) << 16 | ((cx - hw) & 0xffff));
	n = t32_stats_add(w, n, 0x17094, ((cy + hh) & 0xffff) << 16 | ((cy - hh) & 0xffff));
	n = t32_stats_add(w, n, T32_AE_TRIG, 1);
	n = t32_stats_add(w, n, T32_AE_TRIG, 1);
	n = t32_stats_add(w, n, T32_AE_ID, T32_STATS_ID);
	return n;
}

/* AWB: buffers at buf_pa (T32_AWB_BUF_BYTES), thresholds for the EV (log2 Q16) */
/*
 * tisp_awb_params_refresh: the vendor rewrites fields of its section copy before
 * the hardware setup, from the sensor size: it raises the u16 divisors at +3190 and
 * +3186 until (width - [+3188] + 1) / 2 / divisor and (height - [+3184] + 1) / 2 /
 * divisor are below 127, stores the divisors back, fills the two 15-entry u16
 * tables at +3192 and +3222 with the quotients and keeps them at +3290 / +3292.
 * loc is the section region from +3184 (128 bytes, copied by the caller); the
 * offsets below are relative to +3184.  Divisions follow the MIPS core (zero
 * divisor: quotient 1 / -1).
 */
static inline int t32_awb_div(int a, int b)
{
	if (!b)
		return a < 0 ? 1 : -1;
	if (b == -1)
		return (int)(0U - (u32)a);
	return a / b;
}

static inline void t32_awb_params_refresh(u8 *loc, u32 width, u32 height)
{
	u32 x0 = t32_st16(loc, 0), x1 = t32_st16(loc, 2), x2 = t32_st16(loc, 4), x3 = t32_st16(loc, 6);
	int a0 = t32_awb_div((int)(width - x2 + 1), 2), v1;
	u32 a3, q0, i;

	for (;;) {
		a3 = (u32)t32_awb_div(a0, (int)x3) & 0xffff;
		if (a3 < 127)
			break;
		x3 = (x3 + 1) & 0xffff;
	}
	v1 = t32_awb_div((int)(height - x0 + 1), 2);
	for (;;) {
		q0 = (u32)t32_awb_div(v1, (int)x1) & 0xffff;
		if (q0 < 127)
			break;
		x1 = (x1 + 1) & 0xffff;
	}
	loc[2] = (u8)x1; loc[3] = (u8)(x1 >> 8);
	loc[6] = (u8)x3; loc[7] = (u8)(x3 >> 8);
	for (i = 0; i < 15; i++) {
		loc[8 + 2 * i] = (u8)a3; loc[9 + 2 * i] = (u8)(a3 >> 8);
		loc[38 + 2 * i] = (u8)q0; loc[39 + 2 * i] = (u8)(q0 >> 8);
	}
	loc[106] = (u8)a3; loc[107] = (u8)(a3 >> 8);
	loc[108] = (u8)q0; loc[109] = (u8)(q0 >> 8);
}

static inline unsigned int t32_awb_stats_init(const u8 *bin, u32 width, u32 height, u32 buf_pa, u32 ev,
					      struct t32_stats_write *w)
{
	static const u16 thr[8] = { 4588, 4610, 4632, 4654, 4676, 4698, 4720, 4742 };
	const u8 *p = bin + T32_AWB_PARAM;
	u8 loc[128];		/* section region +3184 .. +3311, refreshed below; offsets here are minus 3184 */
	u32 i = ev >> 16, f = ev & 0xffff, q[8], l[11], k;
	unsigned int n = 0;

	for (k = 0; k < sizeof(loc); k++)
		loc[k] = p[3184 + k];
	t32_awb_params_refresh(loc, width, height);

	for (k = 0; k < 4; k++)
		n = t32_stats_add(w, n, 0x16078 + 4 * k, buf_pa + k * 0x7000);
	n = t32_stats_add(w, n, 0x16088, 3);
	n = t32_stats_add(w, n, 0x16040, t32_st16(loc, 4) << 16 | t32_st16(loc, 0));
	n = t32_stats_add(w, n, 0x16044, t32_st_bytes4(loc, 8));
	n = t32_stats_add(w, n, 0x16048, t32_st_bytes4(loc, 16));
	n = t32_stats_add(w, n, 0x1604c, t32_st_bytes4(loc, 24));
	n = t32_stats_add(w, n, 0x16050, t32_st16(loc, 6) << 24 | (t32_st_bytes4(loc, 30) >> 8));
	n = t32_stats_add(w, n, 0x16054, t32_st_bytes4(loc, 38));
	n = t32_stats_add(w, n, 0x16058, t32_st_bytes4(loc, 46));
	n = t32_stats_add(w, n, 0x1605c, t32_st_bytes4(loc, 54));
	n = t32_stats_add(w, n, 0x16060, t32_st16(loc, 2) << 24 | (t32_st_bytes4(loc, 60) >> 8));
	n = t32_stats_add(w, n, 0x16088, 0x01010103 | (u32)loc[92] << 12);
	n = t32_stats_add(w, n, 0x160f4, loc[106] | loc[107] << 8 | loc[108] << 16 | (u32)loc[109] << 24);
	/* tisp_awb_set_regional_threshold */
	for (k = 0; k < 8; k++)
		q[k] = t32_intp_u16(i, f, p + thr[k]);
	n = t32_stats_add(w, n, 0x16064, q[1] << 16 | q[0]);
	n = t32_stats_add(w, n, 0x16068, q[3] << 16 | q[2]);
	n = t32_stats_add(w, n, 0x1606c, q[5] << 16 | q[4]);
	n = t32_stats_add(w, n, 0x16070, q[7] << 16 | q[6]);
	n = t32_stats_add(w, n, T32_AWB_TRIG, 1);
	/* tisp_awb_set_lum_th_freq (no luma override set) */
	for (k = 0; k < 11; k++)
		l[k] = t32_intp_u16(i, f, p + 4764 + 22 * k);
	n = t32_stats_add(w, n, 0x160f0, (l[3] & 15) | (l[4] & 15) << 8 | (l[5] & 15) << 16 | (l[6] & 15) << 24);
	n = t32_stats_add(w, n, 0x16074, t32_st16(loc, 90) << 24 | l[2] << 16 | l[1] << 8 | l[0]);
	n = t32_stats_add(w, n, 0x160ec, (t32_st16(loc, 96) & 3) << 16 | (t32_st16(loc, 94) & 1));
	n = t32_stats_add(w, n, 0x160f8, (l[7] & 15) | (l[8] & 15) << 8 | (l[9] & 15) << 16 | (l[10] & 15) << 24);
	n = t32_stats_add(w, n, T32_AWB_TRIG, 1);
	return n;
}

/*
 * Bring-up controller helpers (NOT the vendor AE): the AE histogram as the
 * statistics engine writes it (8 bytes after the buffer start, 256 bins of
 * 24 bits, two per 64-bit word) and one proportional exposure step.
 */
#ifdef __KERNEL__
#include <linux/math64.h>
#else
static inline unsigned long long div_u64(unsigned long long a, u32 b) { return a / b; }
#endif

#define T32_AE_HIST_HEAD	8
#define T32_AE_HIST_BINS	256
#define T32_AE_CUR		0x1708c	/* bits 1:0 histogram buffer written last */

static inline u32 t32_ae_hist_bin(const u8 *h, unsigned int i)
{
	unsigned int bit = i * 24 + i / 2 * 16, k;
	u32 v = 0;

	for (k = 0; k < 24; k++, bit++)
		v |= (u32)((h[bit / 8] >> (bit % 8)) & 1) << k;
	return v;
}

/* mean bin (0..255) times 16, 0 when the histogram is empty */
static inline u32 t32_ae_hist_mean16(const u8 *h)
{
	unsigned long long sum = 0;
	u32 n = 0;
	unsigned int i;

	for (i = 0; i < T32_AE_HIST_BINS; i++) {
		u32 c = t32_ae_hist_bin(h, i);

		sum += (unsigned long long)c * i;
		n += c;
	}
	if (!n)
		return 0;
	return (u32)div_u64(sum * 16 + n / 2, n);
}

/* next integration time: proportional to target / mean, limited to a
 * quarter / four times per step and to 1 .. max lines */
static inline u32 t32_bringup_ae_step(u32 it, u32 mean16, u32 target16, u32 max)
{
	u32 next;

	if (!mean16 || !target16 || !it)
		return it;
	next = (u32)div_u64((unsigned long long)it * target16, mean16);
	if (next > it * 4)
		next = it * 4;
	if (next < it / 4)
		next = it / 4;
	if (next < 1)
		next = 1;
	if (next > max)
		next = max;
	return next;
}

/*
 * Per-frame AE paths (vendor tisp_ae_get_hist, tisp_ae_interrupt_hist,
 * tisp_ae_get_statistics, tisp_ae_long_target, tisp_ae_calc_convergence_speed,
 * tisp_ae_weight_mean; Matt Davis' recovered C), checked by tests/t32_oracle
 * (t32_aes_oracle).  Locks and cache syncs of the vendor are left to the caller.
 * fix_point_*, tisp_ae_fliker_detect and tisp_ae_lib_bilinear_intp are hooks.
 */
typedef unsigned long long t32_u64;

struct t32_ae_ops {
	u32 (*div32)(u32 shift, u32 a, u32 b);
	u32 (*mult2_32)(u32 shift, u32 a, u32 b);
	u32 (*mult3_32)(u32 shift, u32 a, u32 b, u32 c);
	void (*flicker)(u32 ch, u8 *state);
	t32_u64 (*bilinear)(t32_u64 x, t32_u64 e0, t32_u64 e1, u32 y0, u32 y1);
};

/* vendor e15a8[ch] (tune, buf) and the ROI bytes e151c/e1518/e1524/e1520 */
struct t32_ae_ctx {
	const u8 *tune;
	u8 *buf;
	u8 roi_x0, roi_x1, roi_y0, roi_y1;
};

static inline u32 t32_ld32(const u8 *p, u32 o)
{
	return p[o] | (u32)p[o + 1] << 8 | (u32)p[o + 2] << 16 | (u32)p[o + 3] << 24;
}

static inline t32_u64 t32_ld64(const u8 *p, u32 o)
{
	return t32_ld32(p, o) | (t32_u64)t32_ld32(p, o + 4) << 32;
}

static inline void t32_wr32(u8 *p, u32 o, u32 v)
{
	p[o] = (u8)v; p[o + 1] = (u8)(v >> 8); p[o + 2] = (u8)(v >> 16); p[o + 3] = (u8)(v >> 24);
}

static inline void t32_wr16(u8 *p, u32 o, u32 v)
{
	p[o] = (u8)v; p[o + 1] = (u8)(v >> 8);
}

/* reg_a / reg_b: the two tisp_reg_read(ch, 96) values */
static inline void t32_ae_get_hist(u8 *hist, const u8 *src, u32 reg_a, u32 reg_b)
{
	u32 a = (reg_a >> 1) & 1, b = (reg_b >> 2) & 1, i;

	for (i = 0; i < 128; i++) {
		const u8 *s = src + 8 + 8 * i;
		u8 *d0 = hist + 8 * i, *d1 = hist + 1024 + 8 * i;

		if (a == 1) {
			t32_wr32(d0, 0, t32_ld32(s, 0) & 0xffffff);
			t32_wr32(d0, 4, (t32_ld32(s, 4) << 8) + s[3]);
			if (b == a) {
				t32_wr32(d1, 0, t32_ld32(s, 1016) & 0xffffff);
				t32_wr32(d1, 4, (t32_ld32(s, 1020) << 8) + s[1019]);
			}
		} else if (b == 1) {
			t32_wr32(d1, 0, t32_ld32(s, 0) & 0xffffff);
			t32_wr32(d1, 4, (t32_ld32(s, 4) << 8) + s[3]);
		}
	}
}

/* cur: register 0x1708c, hists: vendor e1598[ch] + 16 */
static inline u32 t32_ae_interrupt_hist(const u8 *hists, u32 cur, u8 *hist, u32 reg_a, u32 reg_b)
{
	t32_ae_get_hist(hist, hists + ((cur & 3) << 11), reg_a, reg_b);
	return 1;
}

static inline void t32_ae_get_statistics(const u8 *tune, u8 *buf, const u8 *src)
{
	u32 nx = t32_st16(tune, 2774), rows = t32_st16(tune, 2770), row, k;

	buf[8606] = (u8)((t32_ld32(src, 12) >> 28) & 3);
	for (row = 0; row < rows; row++) {
		for (k = 0; k < nx; k++) {
			const u8 *z = src + 16 * nx * row + 16 * k;
			u32 q0 = t32_ld32(z, 8), q1 = t32_ld32(z, 12), q2 = t32_ld32(z, 16), q3 = t32_ld32(z, 20);
			u32 i = 60 * row + 4 * k;

			t32_wr32(buf, 2048 + i, q0 & 0x3fffff);
			t32_wr32(buf, 2948 + i, (q1 & 0xfff) << 10 | q0 >> 22);
			t32_wr32(buf, 3848 + i, (q2 & 3) << 20 | q1 >> 12);
			t32_wr32(buf, 4748 + i, (q2 >> 2) & 0x3fff);
			t32_wr32(buf, 5648 + i, (q2 >> 16) & 0x3fff);
			t32_wr32(buf, 6548 + i, (q3 & 0xfffff) << 2 | q2 >> 30);
		}
	}
}

static inline t32_u64 t32_ae_lt_ent(const u8 *tab, u32 i, u32 sh)
{
	return t32_ld64(tab, 8 * i) << sh;
}

static inline u32 t32_ae_long_target(t32_u64 x, const u8 *tab, const u8 *y, u32 shift,
				     const struct t32_ae_ops *ops)
{
	u32 sh = shift & 0xffff, s1 = 0;

	if (!(t32_ae_lt_ent(tab, 0, sh) < x))
		return t32_st16(y, 0);
	if (!(x < t32_ae_lt_ent(tab, 14, sh)))
		return t32_st16(y, 28);
	for (;;) {
		if (x < t32_ae_lt_ent(tab, s1, sh))
			s1++;
		else if (t32_ae_lt_ent(tab, s1 + 1, sh) < x)
			s1++;
		else
			break;
		if (s1 == 14) {
			s1 = 0;
			break;
		}
	}
	return (u32)ops->bilinear(x >> sh, t32_ld64(tab, 8 * s1), t32_ld64(tab, 8 * s1 + 8),
				  t32_st16(y, 2 * s1), t32_st16(y, 2 * s1 + 2));
}

static inline u32 t32_ae_calc_convergence_speed(const u8 *p, u8 *h1p, u8 *h2p, u32 a3, u32 arg4,
						u32 total, const struct t32_ae_ops *ops)
{
	u32 sh = arg4 & 0xffff, k = (a3 & 0xffff) << (sh & 31);
	u32 p0 = t32_ld32(p, 0), p1 = t32_ld32(p, 4);
	u32 h1 = t32_st16(h1p, 0), h2 = t32_st16(h2p, 0), s3 = 128U << (sh & 31), x = p0, y = p1, v;

	if (total < p0 + h1)
		x = total - h1;
	if (total < p1 + h2)
		y = total - h2;
	v = h1 + ops->div32(sh, ops->mult3_32(sh, k, x, x), s3);
	t32_wr16(h1p, 0, v);
	h2 = t32_st16(h2p, 0);
	v = h2 + ops->div32(sh, ops->mult3_32(sh, k, y, y), s3);
	t32_wr16(h2p, 0, v);
	return v;
}

/* stat: vendor e15d8[ch], st: e1588[ch]; outputs *mean, ratio[2], wsum[2] */
static inline u32 t32_ae_weight_mean(const struct t32_ae_ctx *c, u8 *stat, u8 *st, u32 ch, u32 *mean,
				     u32 *ratio, u32 cnt, u32 arg4, u32 *wsum,
				     const struct t32_ae_ops *ops)
{
	const u8 *tune = c->tune, *wt = tune + 3062, *mp = tune + 1270;
	u8 *buf = c->buf, *tmp = buf + 7648;
	u32 nx = t32_st16(tune, 2774), ny = t32_st16(tune, 2770), lum = t32_st16(tune, 2778);
	u32 divf = t32_st16(tune, 2944), shift = t32_st16(tune, 1736), mode = t32_ld32(tune, 1684);
	u32 b1 = t32_st16(buf, 11796), b2 = t32_st16(buf, 11826);
	u32 a3m1 = (cnt & 0xffff) - 1, x16 = 0, x20 = 0, x24 = 0, x4 = 0, x5 = 0, s5 = 0, s0 = 0, s2 = 0;
	u32 t6 = 0, row, k, vsum = 0, vcnt = 0, t4, a3, ea, eb, lo;

	for (row = 0; row < ny; row++) {
		for (k = 0; k < nx; k++) {
			u32 z = row * 15 + k, i = 60 * row + 4 * k;
			u32 w = wt[z], m = mp[z];
			u32 area = t32_st16(buf, (row + 5896) * 2 + 4) * t32_st16(buf, (k + 5912) * 2 + 2);
			u32 e1 = t32_ld32(buf, 2048 + i), e2 = t32_ld32(buf, 2948 + i), e3 = t32_ld32(buf, 3848 + i);
			u32 e4 = t32_ld32(buf, 4748 + i), e5 = t32_ld32(buf, 5648 + i);
			u32 sum = e1 + e2 + e3, q1, q2, l1, l2;

			t32_wr32(tmp, i, area ? sum / area : ~0U);
			t32_wr32(stat, (k + t6 + 2712) * 4 + 8, t32_ld32(tmp, t6 * 4 + 4 * k) << 4);
			x16 += m * sum;
			x20 += area * m;
			s5 += (8 - m) * sum;
			x24 += area * (8 - m);
			q1 = divf ? e4 * a3m1 / divf : ~0U;
			l1 = q1 + w * e4;
			x5 += w * e5;
			q2 = divf ? e1 * a3m1 / divf : ~0U;
			l2 = q2 + w * sum;
			x4 += l1;
			s2 += l2;
			s0 = (area - (e4 + e5)) * w + (w * e5 + l1 + s0);
		}
		t6 += nx;
	}
	if (t32_ld32(tune, 1748) == 1)
		ops->flicker(ch, stat + 10856);
	if (!s0)
		s0 = 1;
	s2 = s2 / s0;
	if (!s2)
		s2 = 1;
	*mean = s2;
	t32_wr32(st, 1524, s2);
	wsum[0] = x20 ? x16 / x20 : ~0U;
	wsum[1] = x24 ? s5 / x24 : ~0U;
	ea = c->roi_y0;
	eb = c->roi_y1;
	a3 = ea * 15;
	for (t4 = ea; t4 < eb; t4++) {
		u32 t3 = c->roi_x0, t7 = c->roi_x1, a0 = ((vcnt << 30) - vcnt + a3 + t3) << 2;

		t3 -= vcnt;
		while (vcnt + t3 < t7) {
			vsum += t32_ld32(tmp, (vcnt << 2) + a0);
			vcnt++;
		}
		a3 += 15;
	}
	if (vsum)
		lo = vcnt ? vsum / vcnt : ~0U;
	else
		lo = t32_ld32(tmp, (ea * 15 + c->roi_x0) << 2);
	t32_wr32(st, 1528, lo);
	if (st[1297]) {
		u32 v1 = t32_ld32(st, 1536), a0 = t32_ld32(st, 1532), a1 = t32_ld32(st, 1524);
		u32 a2 = a0 + v1, v0 = lo * v1 + a0 * a1;

		if (!a2)
			a2 = 1;
		*mean = v0 / a2;
	}
	if (!*mean)
		*mean = 1;
	s0 <<= shift & 31;
	ratio[0] = ops->div32(shift, x5 << (shift & 31), s0);
	ratio[1] = ops->div32(shift, x4 << (shift & 31), s0);
	if (mode == 1) {
		u32 lm = lum < 255 ? ((lum << 1) / 3) & 0xffff : 255, d, s7, si, t0 = 0, area, r;

		d = ops->div32(16, ((arg4 & 0xffff) - 1) << 16, divf << 16);
		s7 = (256 - lm) << 16;
		for (si = lm; si != 256; si++) {
			r = ops->div32(16, (si - lm) << 16, s7);
			r = ops->mult2_32(16, r, r);
			r = ops->mult2_32(16, d, r);
			t0 += ops->mult2_32(5, t32_ld32(buf, si << 2) << 5, r >> 11) >> 5;
		}
		area = nx * ny * b1 * b2;
		r = ops->div32(shift, area + t0, area);
		*mean = (r * *mean) >> (shift & 31);
	}
	if (*mean >= 256)
		*mean = 255;
	if (!*mean)
		*mean = 1;
	return 1;
}

/*
 * Per-frame AWB paths (vendor tisp_awb_get_global_statis, _get_attr,
 * _set_weight, _get_weight, _get_zone, _set_start, _get_mode, _set_mode,
 * _set_frz, _get_frz, _set_ct, _api_set_ct, _set_statis_localtion; Matt Davis'
 * recovered C, PR #15), checked by tests/t32_oracle (t32_awba_oracle).
 *
 * Vendor e16b4[ch] = { par, st, scr }: par = section copy (weights at +5714,
 * window at +3184), st = runtime state, scr = 28 byte mode scratch.  base is the
 * tuning base dd0a4[ch].  tisp_awb_set_start ends in a tail call to
 * tisp_awb_set_gain, which is a hook here.
 */
#define T32_AWB_ST_FLAG		107596U
#define T32_AWB_ST_FRZ		107597U
#define T32_AWB_ST_MODE		107598U
#define T32_AWB_ST_CT		107600U
#define T32_AWB_ST_MA		107604U
#define T32_AWB_ST_MB		107606U
#define T32_AWB_ST_MC		107608U
#define T32_AWB_ST_MD		107610U
#define T32_AWB_ST_ME		107620U
#define T32_AWB_ST_MF		107622U
#define T32_AWB_ST_ZONE		0x1a46cU
#define T32_AWB_ST_APICT	119916U
#define T32_AWB_ST_CLR		79036U
#define T32_AWB_PAR_WEIGHT	5714U
#define T32_AWB_PAR_LOC		3184U
#define T32_AWB_BASE_START	3408U

struct t32_awb_ctx {
	u8 *par;
	u8 *st;
	u8 *scr;
	u8 *base;
};

struct t32_awb_ops {
	void (*set_gain)(u32 ch, const u8 *win, u32 w1, u32 w0);
};

static inline void t32_awb_copy(u8 *d, const u8 *s, u32 n)
{
	u32 i;

	for (i = 0; i < n; i++)
		d[i] = s[i];
}

static inline u32 t32_awb_rcp(const u8 *st, u32 o)
{
	return (u32)t32_awb_div(0x10000, (int)t32_st16(st, o));
}

static inline void t32_awb_get_global_statis(const struct t32_awb_ctx *c, u32 *out)
{
	out[0] = t32_awb_rcp(c->st, T32_AWB_ST_ME);
	out[1] = t32_awb_rcp(c->st, T32_AWB_ST_MF);
	out[2] = t32_st16(c->st, T32_AWB_ST_MC);
	out[3] = t32_st16(c->st, T32_AWB_ST_MD);
}

/* out: 19 words */
static inline void t32_awb_get_attr(const struct t32_awb_ctx *c, u32 *out)
{
	u32 mode = c->st[T32_AWB_ST_MODE], k;

	out[3] = 0;
	out[4] = t32_ld32(c->par, 44);
	out[5] = 0;
	out[6] = 0;
	out[0] = mode;
	for (k = 7; k < 16; k++)
		out[k] = 0;
	out[16] = 1;
	out[17] = t32_ld32(c->par, 24);
	out[18] = t32_ld32(c->par, 28);
	if (mode == 9) {
		out[0] = 0;
		out[5] = 1;
	} else {
		out[1] = t32_st16(c->st, T32_AWB_ST_MA);
		out[2] = t32_st16(c->st, T32_AWB_ST_MB);
	}
	if (out[0] == 0) {
		out[1] = t32_st16(c->st, T32_AWB_ST_ME);
		out[2] = t32_st16(c->st, T32_AWB_ST_MF);
	}
}

static inline void t32_awb_set_weight(const struct t32_awb_ctx *c, const u8 *w)
{
	u32 k;

	for (k = 0; k < 1024; k++)
		c->par[T32_AWB_PAR_WEIGHT + k] = w[k];
	c->st[T32_AWB_ST_CLR] = 0;
}

static inline void t32_awb_get_weight(const struct t32_awb_ctx *c, u8 *out)
{
	u32 k;

	for (k = 0; k < 1024; k++)
		out[k] = c->par[T32_AWB_PAR_WEIGHT + k];
}

static inline void t32_awb_get_zone(const struct t32_awb_ctx *c, u8 *out)
{
	u32 k;

	for (k = 0; k < 3; k++)
		t32_awb_copy(out + 4096 * k, c->st + T32_AWB_ST_ZONE + 4096 * k, 4096);
}

/* win: two u32 (x, y) */
static inline void t32_awb_set_start(const struct t32_awb_ctx *c, u32 ch, const u8 *win,
				     const struct t32_awb_ops *ops)
{
	u32 w0 = t32_ld32(win, 0), w1 = t32_ld32(win, 4);

	t32_wr32(c->par, 16, w0);
	t32_wr32(c->par, 20, w1);
	t32_wr32(c->par, 24, w0);
	t32_wr32(c->par, 28, w1);
	t32_wr32(c->base, T32_AWB_BASE_START, w0);
	t32_wr32(c->base, T32_AWB_BASE_START + 4, w1);
	t32_wr32(c->base, T32_AWB_BASE_START + 8, w0);
	t32_wr32(c->base, T32_AWB_BASE_START + 12, w1);
	ops->set_gain(ch, win, w1, w0);
}

/* out: 28 bytes */
static inline void t32_awb_get_mode(const struct t32_awb_ctx *c, u8 *out)
{
	u8 *s = c->scr;

	t32_wr32(s, 0, c->st[T32_AWB_ST_MODE]);
	t32_wr32(s, 4, t32_st16(c->st, T32_AWB_ST_MA));
	t32_wr32(s, 8, t32_st16(c->st, T32_AWB_ST_MB));
	t32_wr32(s, 12, t32_awb_rcp(c->st, T32_AWB_ST_MC));
	t32_wr32(s, 16, t32_awb_rcp(c->st, T32_AWB_ST_MD));
	t32_wr32(s, 20, t32_st16(c->st, T32_AWB_ST_ME));
	t32_wr32(s, 24, t32_st16(c->st, T32_AWB_ST_MF));
	t32_awb_copy(out, s, 28);
}

/* arg: mode, x, y (u32); modes >= 10: the vendor only prints an error */
static inline void t32_awb_set_mode(const struct t32_awb_ctx *c, const u8 *arg)
{
	static const u16 pre[7][2] = {
		{ 384, 384 }, { 438, 303 }, { 219, 690 }, { 240, 564 },
		{ 315, 459 }, { 468, 279 }, { 240, 376 }
	};
	u32 mode = t32_ld32(arg, 0);
	u8 *s = c->scr;

	if (mode == 0) {
		t32_wr32(s, 0, 0);
	} else if (mode == 1 || mode == 9) {
		t32_wr32(s, 0, mode);
		t32_wr32(s, 4, t32_ld32(arg, 4));
		t32_wr32(s, 8, t32_ld32(arg, 8));
	} else if (mode >= 2 && mode <= 8) {
		t32_wr32(s, 0, mode);
		t32_wr32(s, 4, pre[mode - 2][0]);
		t32_wr32(s, 8, pre[mode - 2][1]);
	}
	c->st[T32_AWB_ST_MODE] = (u8)t32_ld32(s, 0);
	t32_wr16(c->st, T32_AWB_ST_MA, t32_ld32(s, 4));
	t32_wr16(c->st, T32_AWB_ST_MB, t32_ld32(s, 8));
	c->st[T32_AWB_ST_FLAG] = 1;
}

static inline void t32_awb_set_frz(const struct t32_awb_ctx *c, u8 v)
{
	c->st[T32_AWB_ST_FRZ] = v;
}

static inline u8 t32_awb_get_frz(const struct t32_awb_ctx *c)
{
	return c->st[T32_AWB_ST_FRZ];
}

static inline void t32_awb_set_ct(const struct t32_awb_ctx *c, u32 ct)
{
	t32_wr32(c->st, T32_AWB_ST_CT, ct);
}

static inline void t32_awb_api_set_ct(const struct t32_awb_ctx *c, u8 a1, u32 ct)
{
	if (a1 == 1)
		c->st[T32_AWB_ST_MODE] = 10;
	t32_wr32(c->st, T32_AWB_ST_APICT, ct);
}

/* a: x (u32 at 0), y (u32 at 4), u8 at 8, u8 at 9 */
static inline void t32_awb_set_statis_localtion(const struct t32_awb_ctx *c, const u8 *a)
{
	t32_wr16(c->par, T32_AWB_PAR_LOC, t32_ld32(a, 4));
	t32_wr16(c->par, T32_AWB_PAR_LOC + 2, a[9]);
	t32_wr16(c->par, T32_AWB_PAR_LOC + 4, t32_ld32(a, 0));
	t32_wr16(c->par, T32_AWB_PAR_LOC + 6, a[8]);
	c->st[T32_AWB_ST_CLR] = 0;
}

#endif /* TX_ISP_T32_STATS_H */
