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
 * Per-frame AE paths tisp_ae_algo_handle, tisp_ae_sleep, tisp_ae_wake_up
 * (Matt Davis' recovered C, PR #15), checked by tests/t32_oracle (t32_aew_oracle).
 * Registers are the direct addresses (vendor t32_data_d03a0 == 0).  The vendor's
 * dd2d4 + ch * 232 function table is the hook set; each hook gets a pointer to
 * a small block the vendor keeps on its stack (fa = sp + 16, fc = sp + 432).
 * Quirk kept: the last event of tisp_ae_algo_handle compares bf404[ch] but
 * stores into e1548[ch].
 */
#define T32_AE_REG_A 0x3058
#define T32_AE_REG_B 0x305c
#define T32_AE_REG_C 0x3208
#define T32_AE_REG_D 0x320c
#define T32_AE_REG_E 0x3010
#define T32_AE_REG_F 0x26048
#define T32_AE_REG_G 0x2603c

struct t32_ae_algo_in {
	u32 pad[4];
	u32 x16, x20, x24, x28, x32, x36, x40, x44;
};

struct t32_ae_algo_hooks {
	u32 (*h124)(u32 v, u8 *blk);	/* table offsets in dd2d4 + ch * 232 */
	u32 (*h128)(u32 v, u8 *blk);
	u32 (*h132)(u32 v, u8 *blk);
	u32 (*h136)(u32 v, u8 *blk);
	u32 (*h140)(u32 v, u8 *blk);
	u32 (*h148)(u32 v, u8 *blk);
	u32 (*h168)(u32 v, u8 *blk);
	u32 (*h172)(u32 v, u8 *blk);
	u32 (*h184)(u32 v, u8 *blk);
	u32 (*h188)(u32 v, u8 *blk);
	u32 (*h192)(u32 v, u8 *blk);
	u32 (*h196)(u32 v, u8 *blk);
	u32 (*log2)(u32 v, u32 in_frac, u32 out_frac);
	u32 (*exp2)(u32 v, u32 in_frac, u32 out_frac);
	void (*event_push)(u32 ch, u32 *ev);
};

/* buf = e15a8[ch] + 4, flag = e15a8[ch] + 8, the others the per-channel words */
struct t32_ae_algo_state {
	u8 *buf;
	u32 flag;
	u32 *e1568, *e1558, *e1548, *e1560, *e1550, *bf404;
};

static inline u32 t32_ae_curve(const struct t32_ae_algo_hooks *h, u32 (*curve)(u32, u8 *), u32 v, u8 *fc)
{
	return h->exp2(curve(h->log2(v << 6, 16, 16), fc), 16, 16) >> 6;
}

static inline void t32_ae_push(const struct t32_ae_algo_hooks *h, u32 ch, u32 type, u32 val, u8 *buf,
			       u32 off, int reload)
{
	u32 ev[6] = { 0 };

	ev[2] = type;
	ev[4] = val;
	h->event_push(ch, ev);
	t32_wr32(buf, off, reload ? ev[4] : val);
	t32_wr32(buf, off + 4, reload ? ev[5] : 0);
}

static inline unsigned int t32_ae_algo_handle(u32 ch, const struct t32_ae_algo_in *in,
					      const struct t32_ae_algo_hooks *h,
					      const struct t32_ae_ops *ops,
					      struct t32_ae_algo_state *st,
					      struct t32_stats_write *w)
{
	u8 fa[64] = { 0 }, fc[32] = { 0 };
	u8 *blk = fa + 8, *buf = st->buf;
	u32 r140, r148 = 0, s2 = in->x36, s3 = 0, s4 = 0, s5 = 0, s6, s7, s8 = in->x20, x1, regv, l;
	unsigned int n = 0;

	t32_wr32(blk, 0, ch);
	r140 = h->h140(in->x16, blk);
	if (in->x16 != r140)
		s8 = ops->div32(10, ops->mult2_32(10, s8, in->x16 << 10), r140 << 10);
	h->h168(t32_st16(fa, 24), fa);
	t32_wr32(buf, 8608, r140);
	t32_wr32(fc, 0, ch);
	x1 = t32_ae_curve(h, h->h124, s8, fc);
	h->h184(t32_st16(fc, 4), fc);
	t32_wr32(buf, 8612, x1);
	t32_wr32(fc, 0, ch);
	s6 = t32_ae_curve(h, h->h132, in->x24, fc);
	h->h192(t32_st16(fc, 6), fc);
	t32_wr32(buf, 8616, s6);
	regv = in->x28 << 16 | (in->x28 & 0xffff);
	n = t32_stats_add(w, n, T32_AE_REG_A, regv);
	n = t32_stats_add(w, n, T32_AE_REG_B, regv);
	n = t32_stats_add(w, n, T32_AE_REG_E, 0xffffffffU);
	n = t32_stats_add(w, n, T32_AE_TRIG, 1);
	t32_wr32(buf, 8620, in->x28);
	s7 = ops->mult2_32(10, ops->mult3_32(10, r140 << 10, x1, s6), in->x28);
	s6 = ops->mult3_32(10, x1, s6, in->x28);
	if (st->flag == 1) {
		t32_wr32(blk, 0, ch);
		r148 = h->h148(in->x32, blk);
		if (in->x32 != r148)
			s2 = ops->div32(10, ops->mult2_32(10, s2, in->x32 << 10), r148 << 10);
		h->h172(t32_st16(fa, 26), fa);
		t32_wr32(buf, 8624, r148);
		t32_wr32(fc, 0, ch);
		s2 = t32_ae_curve(h, h->h128, s2, fc);
		h->h188(t32_st16(fc, 14), fc);
		t32_wr32(buf, 8628, s2);
		t32_wr32(fc, 0, ch);
		s4 = t32_ae_curve(h, h->h136, in->x40, fc);
		h->h196(t32_st16(fc, 20), fc);
		t32_wr32(buf, 8632, s4);
		s3 = ops->mult2_32(10, in->x36, in->x40);
		s3 = ops->mult2_32(10, in->x44, ops->div32(10, s3, ops->mult2_32(10, s2, s4)));
		n = t32_stats_add(w, n, T32_AE_TRIG, 1);
		t32_wr32(buf, 8636, s3);
		s5 = ops->mult2_32(10, ops->mult3_32(10, r148 << 10, s2, s4), s3);
		s3 = ops->mult3_32(10, s2, s4, s3);
	}
	if (*st->e1568 != s7) {
		t32_ae_push(h, ch, 6, s7, buf, 7472, 1);
		*st->e1568 = s7;
	}
	if (*st->e1558 != s6) {
		l = h->log2(s6 << 6, 16, 16);
		t32_ae_push(h, ch, 4, l, buf, 7480, 0);
		*st->e1558 = s6;
	}
	if (*st->e1548 != x1) {
		l = h->log2(x1 << 6, 16, 16);
		t32_ae_push(h, ch, 5, l, buf, 7488, 0);
		*st->e1548 = x1;
		t32_wr32(buf, 8612, x1);
	}
	if (*st->e1560 != s5) {
		t32_ae_push(h, ch, 10, s5, buf, 7504, 1);
		*st->e1560 = s5;
	}
	if (*st->e1550 != s3) {
		l = h->log2(s3 << 6, 16, 16);
		t32_ae_push(h, ch, 8, l, buf, 7512, 0);
		*st->e1550 = s3;
	}
	if (*st->bf404 != s2) {
		l = h->log2(s2 << 6, 16, 16);
		t32_ae_push(h, ch, 9, l, buf, 7520, 0);
		*st->e1548 = s2;
		t32_wr32(buf, 8628, s2);
	}
	return n;
}

/* tisp_ae_sleep: e153c[3] and e1528[5] are the vendor's saved registers */
struct t32_ae_sleep_state {
	u32 e153c[3];
	u32 e1528[5];
};

static inline void t32_ae_sleep(struct t32_ae_sleep_state *s, u32 (*rd)(u32 reg))
{
	s->e153c[0] = rd(0x17098);
	s->e153c[1] = rd(0x170b4);
	s->e153c[2] = rd(0x170b8);
	s->e1528[0] = rd(T32_AE_REG_A);
	s->e1528[1] = rd(T32_AE_REG_B);
	s->e1528[2] = rd(T32_AE_REG_C);
	s->e1528[3] = rd(T32_AE_REG_D);
	s->e1528[4] = rd(T32_AE_REG_F);
}

static inline void t32_ae_copy_regs(struct t32_stats_write *w, unsigned int *n,
				    const struct t32_stats_write *t, unsigned int m, u32 lo, u32 hi)
{
	unsigned int i;

	for (i = 0; i < m; i++)
		if (t[i].reg >= lo && t[i].reg <= hi)
			*n = t32_stats_add(w, *n, t[i].reg, t[i].val);
}

/*
 * tisp_ae_wake_up: the zone, buffer and ROI registers of tisp_ae_set_hardware_param
 * are taken from t32_ae_stats_init (same width / height / buffer inputs), the
 * rest are the vendor's direct writes.  tune is the AE section (bin + T32_AE_PARAM),
 * buf the vendor's e15a8[ch] + 4 buffer, buf_pa the e1598[ch] + 8 word.
 */
static inline unsigned int t32_ae_wake_up(const u8 *tune, const u8 *buf, u32 buf_pa,
					  const struct t32_ae_sleep_state *sv,
					  struct t32_stats_write *w)
{
	struct t32_stats_write t[T32_STATS_MAX_WRITES];
	unsigned int n = 0, m, k;

	m = t32_ae_stats_init(tune - T32_AE_PARAM, t32_st16(buf, 8548), t32_st16(buf, 8550), buf_pa, t);
	if (m > T32_STATS_MAX_WRITES)
		m = T32_STATS_MAX_WRITES;
	t32_ae_copy_regs(w, &n, t, m, 0x17040, 0x17064);
	n = t32_stats_add(w, n, T32_AE_REG_A, sv->e1528[0]);
	n = t32_stats_add(w, n, T32_AE_REG_B, sv->e1528[1]);
	n = t32_stats_add(w, n, T32_AE_REG_C, sv->e1528[2]);
	n = t32_stats_add(w, n, T32_AE_REG_D, sv->e1528[3]);
	n = t32_stats_add(w, n, T32_AE_REG_E, 0xffffffffU);
	n = t32_stats_add(w, n, T32_AE_REG_F, sv->e1528[4]);
	n = t32_stats_add(w, n, T32_AE_REG_G, 1);
	t32_ae_copy_regs(w, &n, t, m, 0x17068, 0x17088);
	n = t32_stats_add(w, n, 0x17098, sv->e153c[0]);
	n = t32_stats_add(w, n, 0x170b4, sv->e153c[1]);
	n = t32_stats_add(w, n, 0x170b8, sv->e153c[2]);
	t32_ae_copy_regs(w, &n, t, m, 0x17090, 0x17094);
	for (k = 0; k < 6; k++)
		n = t32_stats_add(w, n, 0x1709c + 4 * k, t32_ld32(buf, 7448 + 4 * k));
	n = t32_stats_add(w, n, T32_AE_TRIG, 1);
	return n;
}

/*
 * Per-frame AWB control paths (vendor tisp_awb_get_global_statis, _get_attr,
 * _set_weight, _get_weight, _get_zone, _set_start, _get_mode, _set_mode,
 * _set_frz, _get_frz, _set_ct, _api_set_ct, _set_statis_localtion; Matt Davis'
 * recovered C, PR #15), checked by tests/t32_oracle (t32_awbac_oracle).
 *
 * The vendor's e16b4[ch] points to { tune, sec, mode }: tune is a block with
 * the location / weight fields, sec a large state block (attributes from
 * +0x1a44c, zone buffers from +0x1a46c), mode a 7-word attribute block.  base
 * is the vendor's dd0a4[ch] (tuning base).  tisp_awb_set_start tail-calls
 * tisp_awb_set_gain, which is a hook here (priv, ch, in, in[1], in[0]).
 * tisp_awb_set_mode: modes >= 10 only print in the vendor and change nothing.
 */
#define T32_AWB_SEC_FLAG	0x1a44cU
#define T32_AWB_SEC_FRZ		0x1a44dU
#define T32_AWB_SEC_MODE	0x1a44eU
#define T32_AWB_SEC_CT		0x1a450U
#define T32_AWB_SEC_M1		0x1a454U
#define T32_AWB_SEC_M2		0x1a456U
#define T32_AWB_SEC_A0		0x1a458U
#define T32_AWB_SEC_A1		0x1a45aU
#define T32_AWB_SEC_B0		0x1a464U
#define T32_AWB_SEC_B1		0x1a466U
#define T32_AWB_SEC_ZONE	0x1a46cU
#define T32_AWB_SEC_API_CT	0x1d46cU
#define T32_AWB_SEC_WCLR	0x134bcU
#define T32_AWB_T_WEIGHT	5714U
#define T32_AWB_T_LOC		3184U

struct t32_awb_ctx {
	u8 *tune;	/* e16b4[ch] + 0 */
	u8 *sec;	/* e16b4[ch] + 4 */
	u32 *mode;	/* e16b4[ch] + 8 */
	u8 *base;	/* dd0a4[ch] */
};

typedef void (*t32_awb_gain_fn)(void *priv, u32 ch, const u32 *in, u32 a2, u32 a3);

static inline u32 t32_awb_recip(const u8 *sec, u32 off)
{
	return (u32)t32_awb_div(0x10000, (int)t32_st16(sec, off));
}

static inline void t32_awb_get_global_statis(const struct t32_awb_ctx *c, u32 *out)
{
	out[0] = t32_awb_recip(c->sec, T32_AWB_SEC_B0);
	out[1] = t32_awb_recip(c->sec, T32_AWB_SEC_B1);
	out[2] = t32_st16(c->sec, T32_AWB_SEC_A0);
	out[3] = t32_st16(c->sec, T32_AWB_SEC_A1);
}

/* a: 19 words */
static inline void t32_awb_get_attr(const struct t32_awb_ctx *c, u32 *a)
{
	u32 mode = c->sec[T32_AWB_SEC_MODE], k;

	for (k = 0; k < 19; k++)
		a[k] = 0;
	a[4] = t32_ld32(c->tune, 44);
	a[16] = 1;
	a[17] = t32_ld32(c->tune, 24);
	a[18] = t32_ld32(c->tune, 28);
	a[0] = mode;
	if (mode != 9) {
		a[1] = t32_st16(c->sec, T32_AWB_SEC_M1);
		a[2] = t32_st16(c->sec, T32_AWB_SEC_M2);
	} else {
		a[0] = 0;
		a[5] = 1;
	}
	if (!a[0]) {
		a[1] = t32_st16(c->sec, T32_AWB_SEC_B0);
		a[2] = t32_st16(c->sec, T32_AWB_SEC_B1);
	}
}

/* w: 1024 bytes */
static inline void t32_awb_set_weight(const struct t32_awb_ctx *c, const u8 *w)
{
	u32 k;

	for (k = 0; k < 1024; k++)
		c->tune[T32_AWB_T_WEIGHT + k] = w[k] + 0;
	c->sec[T32_AWB_SEC_WCLR] = 0;
}

static inline void t32_awb_get_weight(const struct t32_awb_ctx *c, u8 *w)
{
	u32 k;

	for (k = 0; k < 1024; k++)
		w[k] = c->tune[T32_AWB_T_WEIGHT + k];
}

/* z: 3 * 4096 bytes */
static inline void t32_awb_get_zone(const struct t32_awb_ctx *c, u8 *z)
{
	u32 k;

	for (k = 0; k < 3 * 4096; k++)
		z[k] = c->sec[T32_AWB_SEC_ZONE + k];
}

static inline void t32_awb_set_start(const struct t32_awb_ctx *c, u32 ch, const u32 *in,
				     t32_awb_gain_fn set_gain, void *priv)
{
	t32_wr32(c->tune, 16, in[0]);
	t32_wr32(c->tune, 20, in[1]);
	t32_wr32(c->tune, 24, in[0]);
	t32_wr32(c->tune, 28, in[1]);
	t32_wr32(c->base, 3408, in[0]);
	t32_wr32(c->base, 3412, in[1]);
	t32_wr32(c->base, 3416, in[0]);
	t32_wr32(c->base, 3420, in[1]);
	set_gain(priv, ch, in, in[1], in[0]);
}

/* out: 7 words */
static inline void t32_awb_get_mode(const struct t32_awb_ctx *c, u32 *out)
{
	u32 *m = c->mode, k;

	m[0] = c->sec[T32_AWB_SEC_MODE];
	m[1] = t32_st16(c->sec, T32_AWB_SEC_M1);
	m[2] = t32_st16(c->sec, T32_AWB_SEC_M2);
	m[3] = t32_awb_recip(c->sec, T32_AWB_SEC_A0);
	m[4] = t32_awb_recip(c->sec, T32_AWB_SEC_A1);
	m[5] = t32_st16(c->sec, T32_AWB_SEC_B0);
	m[6] = t32_st16(c->sec, T32_AWB_SEC_B1);
	for (k = 0; k < 7; k++)
		out[k] = m[k];
}

static inline void t32_awb_set_mode(const struct t32_awb_ctx *c, const u32 *in)
{
	u32 *m = c->mode, mode = in[0];

	switch (mode) {
	case 0: m[0] = 0; break;
	case 1: case 9: m[0] = mode; m[1] = in[1]; m[2] = in[2]; break;
	case 2: m[0] = 2; m[1] = 384; m[2] = 384; break;
	case 3: m[0] = 3; m[1] = 438; m[2] = 303; break;
	case 4: m[0] = 4; m[1] = 219; m[2] = 690; break;
	case 5: m[0] = 5; m[1] = 240; m[2] = 564; break;
	case 6: m[0] = 6; m[1] = 315; m[2] = 459; break;
	case 7: m[0] = 7; m[1] = 468; m[2] = 279; break;
	case 8: m[0] = 8; m[1] = 240; m[2] = 376; break;
	default: break;
	}
	c->sec[T32_AWB_SEC_MODE] = (u8)m[0];
	t32_wr16(c->sec, T32_AWB_SEC_M1, m[1]);
	t32_wr16(c->sec, T32_AWB_SEC_M2, m[2]);
	c->sec[T32_AWB_SEC_FLAG] = 1;
}

static inline void t32_awb_set_frz(const struct t32_awb_ctx *c, u32 v)
{
	c->sec[T32_AWB_SEC_FRZ] = (u8)v;
}

static inline u8 t32_awb_get_frz(const struct t32_awb_ctx *c)
{
	return c->sec[T32_AWB_SEC_FRZ];
}

static inline void t32_awb_set_ct(const struct t32_awb_ctx *c, u32 ct)
{
	t32_wr32(c->sec, T32_AWB_SEC_CT, ct);
}

static inline void t32_awb_api_set_ct(const struct t32_awb_ctx *c, u32 flag, u32 ct)
{
	if ((u8)flag == 1)
		c->sec[T32_AWB_SEC_MODE] = 10;
	t32_wr32(c->sec, T32_AWB_SEC_API_CT, ct);
}

/* in: 3 words (in[1] -> +3184, byte 1 of in[2] -> +3186, in[0] -> +3188, byte 0 of in[2] -> +3190) */
static inline void t32_awb_set_statis_localtion(const struct t32_awb_ctx *c, const u32 *in)
{
	t32_wr16(c->tune, T32_AWB_T_LOC, in[1]);
	t32_wr16(c->tune, T32_AWB_T_LOC + 2, (u8)(in[2] >> 8));
	t32_wr16(c->tune, T32_AWB_T_LOC + 4, in[0]);
	t32_wr16(c->tune, T32_AWB_T_LOC + 6, (u8)in[2]);
	c->sec[T32_AWB_SEC_WCLR] = 0;
}

/*
 * Per-frame AE exposure allocation (vendor tisp_ae_short_expt,
 * tisp_ae_short_ev_alloc, tisp_ae_long_ev_alloc; Matt Davis' recovered C),
 * checked by tests/t32_oracle (t32_aee_oracle).  Register writes go to w[]
 * (direct addresses, the vendor's t32_data_d03a0 = 0 case); the fixed point
 * maths, the sensor setters, events and the helpers are hooks.
 */
struct t32_ae_ev_ops {
	u32 (*mult3_32)(u32 shift, u32 a, u32 b, u32 c);
	u32 (*mult2_32)(u32 shift, u32 a, u32 b);
	t32_u64 (*mult3_64)(u32 shift, t32_u64 a, t32_u64 b, t32_u64 c);
	t32_u64 (*div_64)(u32 shift, t32_u64 a, t32_u64 b);
	t32_u64 (*bilinear)(t32_u64 x, t32_u64 e0, t32_u64 e1, u32 y0, u32 y1);
	u32 (*log2)(u32 v, u32 a, u32 b);
	u32 (*reg_read)(u32 ch, u32 reg);
	u32 (*running_mode)(const u8 *st0);
	u32 (*get_bv)(u32 ch);
	void (*short_calc)(u32 ch, t32_u64 v, u32 *o_a, u32 *o_b, u32 *o_c);
	void (*set_it)(u32 ch, int long_exp);
	void (*set_ag)(u32 ch, int long_exp);
	void (*event)(u32 ch, const u32 *w);
};

static inline void t32_wr64(u8 *p, u32 o, t32_u64 v)
{
	t32_wr32(p, o, (u32)v);
	t32_wr32(p, o + 4, (u32)(v >> 32));
}

static inline t32_u64 t32_ae_tab(const u8 *tune, u32 o, u32 sh)
{
	return t32_ld64(tune, o) << (sh & 63);
}

/* not below 1 << shift (sign extended like the vendor) */
static inline t32_u64 t32_ae_exp_floor(t32_u64 v, u32 sh)
{
	t32_u64 lim = (t32_u64)(long long)(int)(1U << (sh & 31));

	return v < lim ? lim : v;
}

static inline void t32_ae_short_expt(u32 ch, u8 *tune, u8 *st, const struct t32_ae_ev_ops *ops)
{
	u32 sh = t32_st16(tune, 1736), i, o_a = 1, o_b = 1024, o_c = 1024;
	t32_u64 x, v;

	x = ops->mult3_64(sh, (t32_u64)t32_ld32(st, 576) << (sh & 63), t32_ld32(st, 580),
			  t32_ld32(st, 588));
	if (t32_st16(tune, 1228)) {
		v = ops->div_64(sh, x, (t32_u64)t32_st16(tune, 1230) << (sh & 63));
	} else {
		for (i = 0; i < 15; i++) {
			t32_u64 in = t32_ld64(tune, 8 + 8 * i), out = in;

			if (t32_st16(tune, 2956) == 1) {
				t32_u64 pr = (in & 0xffffffffULL) * t32_st16(tune, 2962);
				u32 hi = (u32)(pr >> 32) + (u32)(in >> 32) * t32_st16(tune, 2962);

				out = ((t32_u64)hi << 32 | (u32)pr) >> 7;
			}
			if (!out)
				out = 1;
			t32_wr64(tune, 248 + 8 * i, out);
		}
		if (!(t32_ae_tab(tune, 128, sh) < x)) {
			v = t32_ae_tab(tune, 248, sh);
		} else if (!(x < t32_ae_tab(tune, 240, sh))) {
			v = t32_ae_tab(tune, 360, sh);
		} else {
			u32 s = 0;

			for (;;) {
				if (x < t32_ae_tab(tune, 128 + 8 * s, sh))
					s++;
				else if (t32_ae_tab(tune, 136 + 8 * s, sh) < x)
					s++;
				else
					break;
				s &= 0xff;
				if (s == 14) {
					s = 0;
					break;
				}
			}
			v = (t32_u64)(u32)ops->bilinear(x >> (sh & 63), t32_ld64(tune, 128 + 8 * s),
							t32_ld64(tune, 136 + 8 * s),
							t32_ld32(tune, 248 + 8 * s),
							t32_ld32(tune, 256 + 8 * s)) << (sh & 63);
		}
	}
	v = t32_ae_exp_floor(v, sh);
	ops->short_calc(ch, v, &o_a, &o_b, &o_c);
	t32_wr32(st, 592, o_a);
	t32_wr32(st, 596, o_b);
	t32_wr32(st, 604, o_c);
}

static inline unsigned int t32_ae_short_ev_alloc(u32 ch, u8 *tune, u8 *buf, u8 *st, const u8 *st0,
						 const u8 *dd2d4, const struct t32_ae_ev_ops *ops,
						 struct t32_stats_write *w)
{
	u32 s1 = t32_ld32(tune, 1656) & 0xffff, sh, p, pc, lg, ev[8] = { 0 };
	unsigned int n = 0;
	int k;

	if (st0[2183] == 1)
		s1 = dd2d4[ch * 232 + 123];
	if (t32_st16(buf, 8568) && !st[1285])
		return 0;
	sh = t32_ld32(tune, 1736) & 0xffff;
	t32_ae_short_expt(ch, tune, st, ops);
	ops->set_it(ch, 0);
	ops->set_ag(ch, 0);
	t32_wr32(buf, 7632, t32_ld32(st, 592));
	t32_wr32(buf, 7636, t32_ld32(st, 596));
	t32_wr32(buf, 7644, t32_ld32(st, 604));
	for (k = (short)s1; k >= 0; k--) {
		t32_wr32(st, 368 + 4 * (k + 1), t32_ld32(st, 368 + 4 * k));
		t32_wr32(st, 528 + 4 * (k + 1), t32_ld32(st, 528 + 4 * k));
		t32_wr32(st, 488 + 4 * (k + 1), t32_ld32(st, 488 + 4 * k));
	}
	t32_wr32(st, 368, ops->mult3_32(sh, t32_ld32(st, 592) << (sh & 31), t32_ld32(st, 596),
					t32_ld32(st, 604)));
	t32_wr32(st, 528, ops->mult2_32(sh, t32_ld32(st, 596), t32_ld32(st, 604)));
	t32_wr32(st, 488, t32_ld32(st, 604));
	p = t32_ld32(st, 488 + 4 * s1);
	pc = (p & 0xffff) | p << 16;
	n = t32_stats_add(w, n, 0x3208, pc);
	n = t32_stats_add(w, n, 0x320c, pc);
	n = t32_stats_add(w, n, 0x3010, 0xffffffffU);
	p = t32_ld32(st, 368 + 4 * s1 + 4);
	t32_wr32(buf, 7508, 0);
	t32_wr32(buf, 7504, p);
	ev[2] = 10;
	ev[4] = p;
	ev[6] = t32_ld32(buf, 11792);
	ops->event(ch, ev);
	lg = ops->log2(t32_ld32(st, 528 + 4 * s1 + 4) << 6, 16, 16);
	t32_wr32(buf, 7512, lg);
	t32_wr32(buf, 7516, 0);
	memset(ev, 0, sizeof(ev));
	ev[2] = 8;
	ev[4] = lg;
	ev[6] = t32_ld32(buf, 11792);
	ops->event(ch, ev);
	return n;
}

static inline unsigned int t32_ae_long_ev_alloc(u32 ch, u8 *tune, u8 *buf, u8 *st, u8 *st0,
						const u8 *dd2d4, u32 ctx8,
						const struct t32_ae_ev_ops *ops,
						struct t32_stats_write *w)
{
	u32 sh = t32_ld32(tune, 1736) & 0xffff, s3 = t32_ld32(tune, 1656) & 0xffff;
	u32 i, p, pc, bv, rd, lg, ev[8] = { 0 };
	unsigned int n = 0;
	t32_u64 e;
	int k;

	st0[2183] = (u8)ops->running_mode(st0);
	if (st0[2183] == 1)
		s3 = dd2d4[ch * 232 + 123];
	ops->set_it(ch, 1);
	ops->set_ag(ch, 1);
	t32_wr32(buf, 7616, t32_ld32(st, 576));
	t32_wr32(buf, 7620, t32_ld32(st, 580));
	t32_wr32(buf, 7628, t32_ld32(st, 588));
	for (i = 0; i < 255; i++)
		t32_wr32(buf, 8700 + 4 * i, t32_ld32(buf, 8704 + 4 * i));
	t32_wr32(buf, 9720, t32_ld32(st, 576));
	for (i = 0; i < 255; i++)
		t32_wr32(buf, 9724 + 4 * i, t32_ld32(buf, 9728 + 4 * i));
	t32_wr32(buf, 10744, t32_ld32(st, 580));
	for (i = 0; i < 255; i++)
		t32_wr32(buf, 10748 + 4 * i, t32_ld32(buf, 10752 + 4 * i));
	t32_wr32(buf, 11768, t32_ld32(st, 588));
	for (k = (short)s3; k >= 0; k--) {
		t32_wr32(st, 8 * (k + 1), t32_ld32(st, 8 * k));
		t32_wr32(st, 8 * (k + 1) + 4, t32_ld32(st, 8 * k + 4));
		t32_wr32(st, 328 + 4 * (k + 1), t32_ld32(st, 328 + 4 * k));
	}
	e = ops->mult3_64(sh, (t32_u64)t32_ld32(st, 576) << (sh & 63), t32_ld32(st, 580),
			  t32_ld32(st, 588));
	i = ops->mult2_32(sh, t32_ld32(st, 580), t32_ld32(st, 588));
	t32_wr64(st, 0, e);
	t32_wr32(st, 328, i);
	for (k = (short)s3; k >= 0; k--) {
		t32_wr32(st, 248 + 4 * (k + 1), t32_ld32(st, 248 + 4 * k));
		t32_wr32(st, 288 + 4 * (k + 1), t32_ld32(st, 288 + 4 * k));
	}
	t32_wr32(st, 248, t32_ld32(st, 580));
	t32_wr32(st, 288, t32_ld32(st, 588));
	bv = ops->get_bv(ch);
	p = t32_ld32(st, 288 + 4 * s3);
	pc = (p & 0xffff) | p << 16;
	n = t32_stats_add(w, n, 0x3058, pc);
	n = t32_stats_add(w, n, 0x305c, pc);
	if (!ctx8) {
		n = t32_stats_add(w, n, 0x3208, pc);
		n = t32_stats_add(w, n, 0x320c, pc);
	}
	n = t32_stats_add(w, n, 0x3010, 0xffffffffU);
	rd = ops->reg_read(ch, 0x3044);
	n = t32_stats_add(w, n, 0x26048, p << 16 | (rd & 0x1ff));
	n = t32_stats_add(w, n, 0x2603c, 1);
	t32_wr32(buf, 7472, t32_ld32(st, 8 * (s3 + 1)));
	t32_wr32(buf, 7476, t32_ld32(st, 8 * (s3 + 1) + 4));
	ev[2] = 6;
	if (!st[1695]) {
		ev[4] = t32_ld32(st, 8 * (s3 + 1));
		ev[5] = t32_ld32(st, 8 * (s3 + 1) + 4);
	} else {
		st[1695]--;
		ev[4] = t32_ld32(tune, 368) << 10;
		ev[5] = t32_ld32(tune, 372) << 10 | t32_ld32(tune, 368) >> 22;
	}
	ev[6] = bv;
	ops->event(ch, ev);
	lg = ops->log2(t32_ld32(st, 328 + 4 * s3 + 4) << 6, 16, 16);
	t32_wr32(buf, 7480, lg);
	t32_wr32(buf, 7484, 0);
	if (t32_ld32(buf, 11784) == 1)
		lg = t32_ld32(buf, 11788);
	memset(ev, 0, sizeof(ev));
	ev[2] = 4;
	ev[4] = lg;
	ev[6] = bv;
	ops->event(ch, ev);
	t32_wr32(buf, 11788, lg);
	lg = ops->log2(t32_ld32(st, 248 + 4 * s3 + 4) << 6, 16, 16);
	t32_wr32(buf, 7488, lg);
	t32_wr32(buf, 7492, 0);
	memset(ev, 0, sizeof(ev));
	ev[2] = 5;
	ev[4] = lg;
	ops->event(ch, ev);
	return n;
}
/*
 * Per-frame AE paths tisp_ae_get_bv, tisp_ae_calc_process and
 * tisp_ae_interrupt_static (Matt Davis' recovered C, PR #15 @ 2e2e06bc),
 * checked by tests/t32_oracle (t32_aeb_oracle).  Fixed point maths, the
 * algorithm / long process / completion calls, the exposure allocators and
 * events are hooks.  Register writes of calc_process go to w[] (direct
 * addresses, vendor t32_data_d03a0 == 0).  The printk-only debug block of
 * calc_process (dbg set and dbg_max >= frame count) changes no state and is
 * not ported.
 */
struct t32_ae_bv_ops {
	u32 (*div32)(u32 shift, u32 a, u32 b);
	u32 (*log2)(u32 v, u32 in_frac, u32 out_frac);
	u32 (*log2_64)(t32_u64 v, u32 in_frac, u32 out_frac);
	t32_u64 (*mult3_64)(u32 shift, t32_u64 a, t32_u64 b, t32_u64 c);
};

/* tune: AE section, buf: vendor e15a8[ch] + 4 buffer, st: vendor e1588[ch] */
static inline u32 t32_ae_get_bv(const u8 *tune, u8 *buf, const u8 *st, const struct t32_ae_bv_ops *ops)
{
	u32 e[16][3], k, s, gain, sum, l3, l4, bv;
	t32_u64 t[16], x;

	gain = t32_st16(buf, 12366);
	for (k = 0; k < 16; k++) {
		u32 d = ops->div32(16, t32_ld32(tune, 4 * (453 + 3 * k)) << 6, 0x440000U);

		e[k][0] = t32_ld32(tune, 4 * (452 + 3 * k));
		e[k][1] = ops->log2(d, 16, 16) + 0x180000U;
		e[k][2] = t32_ld32(tune, 4 * (454 + 3 * k));
	}
	x = ops->mult3_64(10, t32_ld32(st, 576) << 10, t32_ld32(st, 580), t32_ld32(st, 588));
	for (k = 0; k < 16; k++)
		t[k] = (t32_u64)e[k][0] << 10;
	if (!(t[0] < x)) {
		s = 0;
	} else if (!(x < t[15])) {
		s = 15;
	} else {
		for (s = 0; s != 15; s++)
			if (!(x < t[s]) && !(t[s + 1] < x))
				break;
		if (s == 15)
			s = 0;
	}
	sum = ops->log2(gain << 16, 16, 16);
	sum += ops->log2_64(t[s] << 6, 16, 16) + e[s][1];
	l3 = ops->log2(e[s][2] << 16, 16, 16);
	l4 = ops->log2_64(x << 6, 16, 16);
	bv = (l3 + l4 < sum) ? sum - (l3 + l4) : 0;
	t32_wr32(buf, 11792, bv);
	return bv;
}

struct t32_ae_cp_ops {
	u32 (*reg_read)(u32 ch, u32 reg);
	void (*algorithm)(u32 ch);
	void (*par_update_trig)(u32 ch);
	void (*long_process)(u32 ch);
	void (*complete)(void *done);
};

struct t32_ae_cp_glob {
	u32 *stop;		/* vendor e15b8[ch] */
	u32 *frame_cnt;		/* e1578[ch] */
	u32 dbg;		/* e1500[ch] */
	u32 dbg_max;		/* e14f0[ch] */
	u32 trace;		/* d9008 */
};

static inline u32 t32_ae_pair(u32 v)
{
	return (v & 0xffff) | v << 16;
}

/* flag: e15a8[ch] + 8, done: e15a8[ch] + 2008; returns the number of writes */
static inline unsigned int t32_ae_calc_process(u32 ch, const struct t32_ae_ctx *c, u8 *st, u32 flag,
					       const struct t32_ae_cp_glob *g, void *done,
					       const struct t32_ae_cp_ops *ops,
					       struct t32_stats_write *w)
{
	u32 idx = t32_st16(c->tune, 1656), k, v, p;
	unsigned int n = 0;

	if (st[2182] == 1 && st[1297])
		return 0;
	v = ops->reg_read(ch, T32_AE_REG_A) >> 16;
	ops->reg_read(ch, T32_AE_REG_C);	/* result unused by the vendor */
	if (v != t32_ld32(st, 288 + 4 * idx))
		t32_wr16(st, 1698, t32_st16(st, 1698) + 1);
	if (!st[1696]) {
		for (k = idx; k >= 2; k--) {
			t32_wr32(st, 288 + 4 * k, t32_ld32(st, 288 + 4 * (k - 1)));
			if (flag == 1)
				t32_wr32(st, 488 + 4 * k, t32_ld32(st, 488 + 4 * (k - 1)));
		}
		p = t32_ae_pair(t32_ld32(st, 288 + 4 * idx));
		n = t32_stats_add(w, n, T32_AE_REG_A, p);
		n = t32_stats_add(w, n, T32_AE_REG_B, p);
		p = t32_ae_pair(t32_ld32(st, 488 + 4 * idx));
		n = t32_stats_add(w, n, T32_AE_REG_C, p);
		n = t32_stats_add(w, n, T32_AE_REG_D, p);
		n = t32_stats_add(w, n, T32_AE_REG_E, 0xffffffffU);
		return n;
	}
	if (st[1285])
		st[1286] = 1;
	if (!*g->stop && st[1285])
		ops->par_update_trig(ch);
	ops->algorithm(ch & 0xff);
	if (st[1286] == 1) {
		st[1285] = 0;
		st[1286] = 0;
	}
	ops->long_process(ch);
	if (g->trace)
		*g->frame_cnt = *g->frame_cnt + 1;
	if (*g->stop == 1)
		ops->complete(done);
	return n;
}

struct t32_ae_irq_ops {
	void (*long_alloc)(u32 ch);
	void (*short_alloc)(u32 ch);
	u32 (*sys_read)(u32 reg);
	void (*cache_sync)(const u8 *p, u32 len);
	void (*event)(u32 ch, const u32 *w);
};

/*
 * stopped: e15b8[ch], mode: e1510[ch], flag: e15a8[ch] + 8 (read after the long
 * allocation), bufs: the zone buffers (vendor e1598[ch] + 4).
 */
static inline u32 t32_ae_interrupt_static(u32 ch, const struct t32_ae_ctx *c, const u32 *flag,
					  u32 stopped, u32 mode, const u8 *bufs,
					  const struct t32_ae_irq_ops *ops)
{
	u32 ev[8] = { 0 }, v;
	const u8 *src;

	if (!stopped && mode == 1) {
		ops->long_alloc(ch & 0xff);
		if (*flag == 1)
			ops->short_alloc(ch & 0xff);
	}
	v = ops->sys_read(T32_AE_CUR);
	src = bufs + (((v >> 4) & 3) << 12);
	ops->cache_sync(src, 4096);
	t32_ae_get_statistics(c->tune, c->buf, src);
	ev[2] = 1;
	ops->event(ch, ev);
	return 1;
}

#endif /* TX_ISP_T32_STATS_H */
