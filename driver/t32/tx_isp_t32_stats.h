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
 * AWB API accessors (vendor tisp_awb_api_set_face / get_face /
 * set_ct_trend_offset / get_ct_trend_offset / get_awb_stable), checked by
 * t32_awbaa_oracle.  Same ctx as above.  The face block is five words at
 * sec + 0x1d470 (in[1], in[2], in[3], in[4], then in[0] at +0x1d480); the
 * vendor's set_face first calls tisp_awb_face_pos(ch, byte in[0], u16 +8,
 * u16 +16, u16 +4, u16 +12), a hook here.  The ct trend offset is six words at
 * tune + 3064 plus two u16 at tune + 3088 / 3092, and setting it raises the
 * flag byte at sec + 0x1a44c.
 */
#define T32_AWB_SEC_FACE	0x1d470U
#define T32_AWB_T_CTTR		3064U
#define T32_AWB_T_CTTR_A	3088U

typedef void (*t32_awb_facepos_fn)(void *priv, u32 ch, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5);

/* in: byte 0 / u16 4,8,12,16 plus words 1..4 (20 bytes, vendor reads them as u8/u16/u32) */
static inline void t32_awb_api_set_face(const struct t32_awb_ctx *c, u32 ch, const u8 *in,
					t32_awb_facepos_fn pos, void *priv)
{
	u32 k;

	pos(priv, ch, in[0], t32_st16(in, 8), t32_st16(in, 16), t32_st16(in, 4), t32_st16(in, 12));
	for (k = 1; k < 5; k++)
		t32_wr32(c->sec, T32_AWB_SEC_FACE + 4 * (k - 1), t32_ld32(in, 4 * k));
	t32_wr32(c->sec, T32_AWB_SEC_FACE + 16, t32_ld32(in, 0));
}

static inline void t32_awb_api_get_face(const struct t32_awb_ctx *c, u8 *out)
{
	u32 k;

	for (k = 1; k < 5; k++)
		t32_wr32(out, 4 * k, t32_ld32(c->sec, T32_AWB_SEC_FACE + 4 * (k - 1)));
	t32_wr32(out, 0, t32_ld32(c->sec, T32_AWB_SEC_FACE + 16));
}

/* in: u16 offsets at 0 / 2, six words at 4..24 */
static inline void t32_awb_api_set_ct_trend_offset(const struct t32_awb_ctx *c, const u8 *in)
{
	u32 k;

	for (k = 0; k < 6; k++)
		t32_wr32(c->tune, T32_AWB_T_CTTR + 4 * k, t32_ld32(in, 4 + 4 * k));
	t32_wr32(c->tune, T32_AWB_T_CTTR_A, t32_st16(in, 0));
	t32_wr32(c->tune, T32_AWB_T_CTTR_A + 4, t32_st16(in, 2));
	c->sec[T32_AWB_SEC_FLAG] = 1;
}

static inline void t32_awb_api_get_ct_trend_offset(const struct t32_awb_ctx *c, u8 *out)
{
	u32 k;

	for (k = 0; k < 6; k++)
		t32_wr32(out, 4 + 4 * k, t32_ld32(c->tune, T32_AWB_T_CTTR + 4 * k));
	t32_wr16(out, 0, t32_ld32(c->tune, T32_AWB_T_CTTR_A));
	t32_wr16(out, 2, t32_ld32(c->tune, T32_AWB_T_CTTR_A + 4));
}

/* the vendor reads the word at tune + 8 */
static inline u32 t32_awb_api_get_awb_stable(const struct t32_awb_ctx *c)
{
	return t32_ld32(c->tune, 8);
}

/*
 * AE API accessors (vendor tisp_api_ae_get_tgain, tisp_ae_api_set_ev_start /
 * get_luma / get_stable / get_flicker_flag / get_gobal_roi_luma /
 * get_face_ae_tune_params / set_face_ae_tune_params / get_fps / set_fps /
 * get_faceae, tisp_ae_get_reg_state), checked by t32_aeaa_oracle.
 * Vendor layout: e15a8[ch] -> { tune, buf }, e1588[ch] -> st (state block),
 * dd2d4 + ch * 232 -> per-channel fps / hook block.  The getters that return
 * a leftover register in the vendor return nothing here (the value is unused).
 */
#define T32_AEA_BUF_TGAIN	7480U
#define T32_AEA_T_EVSTART	368U
#define T32_AEA_ST_FACE		1500U
#define T32_AEA_ST_ROI		1524U
#define T32_AEA_ST_TUNE		1532U
#define T32_AEA_FPS_CUR		56U

struct t32_aea_ctx {
	u8 *tune;	/* e15a8[ch] + 0 */
	u8 *buf;	/* e15a8[ch] + 4 */
	u8 *st;		/* e1588[ch] */
	u8 *fps;	/* dd2d4 + ch * 232 */
	u8 *hist;	/* d0950 + ch * 1052 */
	u8 *wsel;	/* e1570 + ch * 2 */
	u8 *base;	/* dd0a4[ch] */
	u8 *comp;	/* d2a30 + ch * 1028 */
	u8 *lum;	/* d19c0 + ch * 1052 */
};

static inline u32 t32_aea_get_tgain(const struct t32_aea_ctx *c)
{
	return t32_ld32(c->buf, T32_AEA_BUF_TGAIN);
}

static inline void t32_aea_set_ev_start(const struct t32_aea_ctx *c, u32 a2, u32 a3)
{
	t32_wr32(c->tune, T32_AEA_T_EVSTART, a2);
	t32_wr32(c->tune, T32_AEA_T_EVSTART + 4, a3);
}

/* mean of the 256-bin histogram (buf words 0..255) over the tune / buf size product;
 * a zero product returns 0xff like the recovered C (the MIPS divide is undefined there). */
static inline u8 t32_aea_get_luma(const struct t32_aea_ctx *c)
{
	u32 sum = 0, k, v;

	for (k = 0; k < 256; k++)
		sum += k * t32_ld32(c->buf, 4 * k);
	v = t32_st16(c->tune, 2770) * t32_st16(c->tune, 2774) * t32_st16(c->buf, 11796) *
	    t32_st16(c->buf, 11826);
	return v ? (u8)(sum / v) : 0xffU;
}

static inline void t32_aea_get_stable(const struct t32_aea_ctx *c, u32 *out)
{
	out[0] = t32_st16(c->buf, 8568);
	out[1] = t32_st16(c->buf, 8570);
	out[2] = t32_st16(c->buf, 8572);
}

static inline u32 t32_aea_get_flicker_flag(const struct t32_aea_ctx *c)
{
	return c->st[1694];
}

static inline void t32_aea_get_gobal_roi_luma(const struct t32_aea_ctx *c, u32 *o1, u32 *o2)
{
	*o1 = t32_ld32(c->st, T32_AEA_ST_ROI);
	*o2 = t32_ld32(c->st, T32_AEA_ST_ROI + 4);
}

static inline void t32_aea_get_face_ae_tune_params(const struct t32_aea_ctx *c, u32 *o1, u32 *o2,
						   u32 *o3)
{
	*o1 = t32_ld32(c->st, T32_AEA_ST_TUNE);
	*o2 = t32_ld32(c->st, T32_AEA_ST_TUNE + 4);
	*o3 = t32_ld32(c->st, T32_AEA_ST_TUNE + 8);
}

/* a zero *p3 is replaced by 1 (written back to the caller) after a warning (hook) */
static inline void t32_aea_set_face_ae_tune_params(const struct t32_aea_ctx *c, const u32 *p1,
						   const u32 *p2, u32 *p3, void (*warn)(void *priv),
						   void *priv)
{
	t32_wr32(c->st, T32_AEA_ST_TUNE, *p1);
	t32_wr32(c->st, T32_AEA_ST_TUNE + 4, *p2);
	if (!*p3) {
		*p3 = 1;
		warn(priv);
	}
	t32_wr32(c->st, T32_AEA_ST_TUNE + 8, *p3);
	c->st[1285] = 1;
}

static inline u32 t32_aea_get_fps(const struct t32_aea_ctx *c)
{
	return t32_ld32(c->fps, T32_AEA_FPS_CUR);
}

/* out[1..5] = st + 1500.., out[0] = st + 1520 */
static inline void t32_aea_get_faceae(const struct t32_aea_ctx *c, u32 *out)
{
	u32 k;

	for (k = 0; k < 5; k++)
		out[k + 1] = t32_ld32(c->st, T32_AEA_ST_FACE + 4 * k);
	out[0] = t32_ld32(c->st, T32_AEA_ST_FACE + 20);
}

static inline void t32_aea_set_reg_state(const struct t32_aea_ctx *c, u32 v)
{
	c->st[1696] = (u8)v;
}

/*
 * tisp_ae_api_set_fps: tune + 1740 == 1 warns (hook) and returns -1.  When the
 * stored fps differs, the per-channel hook (dd2d4 + ch * 232 + 212) is called
 * with (fps, ch); the vendor hands it a 128 byte stack buffer (ch at +8), the
 * ch * 232 and the caller's a3.  Its result is ignored.
 */
typedef void (*t32_aea_fps_fn)(void *priv, u32 fps, u32 ch);

static inline int t32_aea_set_fps(const struct t32_aea_ctx *c, u32 ch, u32 fps, t32_aea_fps_fn setfps,
				  void (*warn)(void *priv), void *priv)
{
	if (t32_ld32(c->tune, 1740) == 1) {
		warn(priv);
		return -1;
	}
	if (t32_ld32(c->fps, T32_AEA_FPS_CUR) != fps)
		setfps(priv, fps, ch);
	return 0;
}

/*
 * AE API data blocks (vendor api_ae_get_hist_bin / set_hist_bin / get_y_zone /
 * get_weight / set_weight / get_antiflicker_step, tisp_ae_api_get_antiflick /
 * set_antiflick / set_comp), checked by t32_aeab_oracle.  The vendor takes the
 * T32 spin lock around hist_bin / y_zone; that is left to the caller.  The
 * weight block is { sel0, sel1, 225 bytes (tune + 1270), 225 bytes (tune + 3062) }
 * (u32 / u32 / bytes at 8 and 233); set_weight also writes the 225 bytes inverted
 * (8 - w) to buf + 12608.
 */
#define T32_AEA_HIST_BYTES	1052U
#define T32_AEA_HIST_BIN	1044U
#define T32_AEA_BUF_YZONE	7648U
#define T32_AEA_YZONE_BYTES	900U
#define T32_AEA_T_W0		1270U
#define T32_AEA_T_W1		3062U
#define T32_AEA_BUF_WINV	12608U
#define T32_AEA_W_N		225U
#define T32_AEA_T_AFEN		2920U
#define T32_AEA_T_AFMODE	2922U
#define T32_AEA_T_AFVAL		1660U
#define T32_AEA_T_COMP		2960U
#define T32_AEA_BUF_AFSTEP	12842U
#define T32_AEA_ST_AFSTEP	1700U
#define T32_AEA_BASE_AF		1764U

static inline void t32_aea_cpy(u8 *dst, const u8 *src, u32 n)
{
	u32 k;

	for (k = 0; k < n; k++)
		dst[k] = src[k];
}

static inline void t32_aea_get_hist_bin(const struct t32_aea_ctx *c, u8 *out)
{
	t32_aea_cpy(out, c->hist, T32_AEA_HIST_BYTES);
}

static inline void t32_aea_set_hist_bin(const struct t32_aea_ctx *c, const u8 *in)
{
	t32_aea_cpy(c->hist + T32_AEA_HIST_BIN, in, 4);
}

static inline void t32_aea_get_y_zone(const struct t32_aea_ctx *c, u8 *out)
{
	t32_aea_cpy(out, c->buf + T32_AEA_BUF_YZONE, T32_AEA_YZONE_BYTES);
}

static inline void t32_aea_get_weight(const struct t32_aea_ctx *c, u8 *out)
{
	t32_wr32(out, 0, c->wsel[0]);
	t32_wr32(out, 4, c->wsel[1]);
	t32_aea_cpy(out + 8, c->tune + T32_AEA_T_W0, T32_AEA_W_N);
	t32_aea_cpy(out + 233, c->tune + T32_AEA_T_W1, T32_AEA_W_N);
}

static inline void t32_aea_set_weight(const struct t32_aea_ctx *c, const u8 *in)
{
	u32 sel0 = t32_ld32(in, 0), sel1 = t32_ld32(in, 4), k;
	u8 inv[T32_AEA_W_N];

	c->wsel[0] = (u8)sel0;
	c->wsel[1] = (u8)sel1;
	if (sel0 == 1) {
		t32_aea_cpy(c->tune + T32_AEA_T_W0, in + 8, T32_AEA_W_N);
		for (k = 0; k < T32_AEA_W_N; k++)
			inv[k] = (u8)(8 - in[8 + k]);
		t32_aea_cpy(c->buf + T32_AEA_BUF_WINV, inv, T32_AEA_W_N);
	}
	if (sel1 == 1)
		t32_aea_cpy(c->tune + T32_AEA_T_W1, in + 233, T32_AEA_W_N);
}

/* vendor args (ch, 4-byte out, u16 out). tisp_ae_clac_deflicker_cfg(ch) (hook) runs first; then -1 unless the anti-flicker
 * enable word (tune + 2920) is 1; out1: u16, out2: 4 bytes */
static inline int t32_aea_get_antiflicker_step(const struct t32_aea_ctx *c, u32 ch, u8 *out1, u8 *out2,
					       void (*deflicker)(void *priv, u32 ch), void *priv)
{
	deflicker(priv, ch);
	if (t32_st16(c->tune, T32_AEA_T_AFEN) != 1)
		return -1;
	t32_wr16(out1, 0, c->buf[T32_AEA_BUF_AFSTEP]);
	t32_aea_cpy(out2, c->st + T32_AEA_ST_AFSTEP, 4);
	return 0;
}

/* out: word 0 = 0 (off) / 1 / 2 (mode), byte 4 = value */
static inline void t32_aea_get_antiflick(const struct t32_aea_ctx *c, u8 *out)
{
	if (!t32_st16(c->tune, T32_AEA_T_AFEN)) {
		t32_wr32(out, 0, 0);
		out[4] = 0;
	} else if (t32_st16(c->tune, T32_AEA_T_AFMODE)) {
		t32_wr32(out, 0, 1);
		out[4] = (u8)t32_ld32(c->base, T32_AEA_BASE_AF);
	} else {
		t32_wr32(out, 0, 2);
		out[4] = (u8)t32_ld32(c->tune, T32_AEA_T_AFVAL);
	}
}

/* in: word 0 mode (0 off, 2 -> mode 0, else mode 1), byte 4 value;
 * tisp_ae_clac_deflicker_cfg(ch) is a hook */
static inline void t32_aea_set_antiflick(const struct t32_aea_ctx *c, u32 ch, const u8 *in,
					 void (*deflicker)(void *priv, u32 ch), void *priv)
{
	u32 mode = t32_ld32(in, 0);

	if (!mode) {
		t32_wr16(c->tune, T32_AEA_T_AFEN, 0);
	} else {
		t32_wr16(c->tune, T32_AEA_T_AFEN, 1);
		t32_wr16(c->tune, T32_AEA_T_AFMODE, mode == 2 ? 0 : 1);
		t32_wr32(c->tune, T32_AEA_T_AFVAL, in[4]);
	}
	deflicker(priv, ch);
	c->st[1285] = 1;
}

/* comp 0 / 1..128: tune + 2960 = (v * comp) >> 7 (comp 0 counts as 1);
 * above 128: comp * 300 / 127 + v - 302 */
static inline void t32_aea_set_comp(const struct t32_aea_ctx *c, u32 comp)
{
	u32 v = t32_st16(c->tune, T32_AEA_T_COMP), x;

	comp &= 0xffff;
	c->comp[1024] = (u8)comp;
	if (comp == 0)
		x = (u32)((int)v >> 7);
	else if (comp <= 128)
		x = (u32)((int)(v * comp) >> 7);
	else
		x = (u32)((int)(comp * 300) / 127) + v - 302;
	t32_wr16(c->tune, T32_AEA_T_COMP, x);
	c->st[1285] = 1;
}

/*
 * AE scene parameters (vendor tisp_ae_api_set_scene_par / get_scene_par /
 * get_scene_luma), checked by t32_aeac_oracle.  The scene block is 10 words:
 * [0] mode A (1, 2, 3), byte 4 value A, [2] mode B, byte 12 value B,
 * [4] flag (comp != 128), [5] comp, [6] stable flag, [7] ev start, [8] luma,
 * [9] scene luma.  A (2938..) and B (2940..) share the vendor's mode word at
 * tune + 1684 (set: modes 2 and 3 write 1; get: 0, 1, 2 are read).
 * fix_point_div_32 / mult2_32 are the hooks of struct t32_ae_ops.  Like the
 * vendor, get_scene_luma reads st at 8 * ((tune[1656] & 0xffff) + 1).
 */
#define T32_AEA_T_SCMODE	1684U
#define T32_AEA_T_SCA16		2942U
#define T32_AEA_T_SCB16		2940U
#define T32_AEA_T_SCFLAG	2956U
#define T32_AEA_BUF_STABLE	8566U
#define T32_AEA_ST_SCENE	1200U

static inline void t32_aea_set_scene_par(const struct t32_aea_ctx *c, const u8 *in)
{
	u8 *t = c->tune;
	u32 m = t32_ld32(in, 0), v;

	if (m == 1) {
		t32_wr16(t, T32_AEA_T_SCA16, 1);
		t32_wr32(t, 1704, 1);
		t32_wr32(t, 1708, 1);
	} else if (m == 3) {
		t32_wr32(t, T32_AEA_T_SCMODE, 1);
		t32_wr16(t, T32_AEA_T_SCA16, in[4] + 1);
	} else if (m == 2) {
		t32_wr32(t, T32_AEA_T_SCMODE, 1);
		t32_wr32(t, 1704, 1);
		t32_wr32(t, 1708, in[4] + 1);
	}
	m = t32_ld32(in, 8);
	if (m == 1) {
		t32_wr16(t, T32_AEA_T_SCB16, 1);
		t32_wr32(t, 1712, 1);
		t32_wr32(t, 1716, 1);
	} else if (m == 3) {
		t32_wr32(t, T32_AEA_T_SCMODE, 1);
		t32_wr16(t, T32_AEA_T_SCB16, in[12] + 1);
	} else if (m == 2) {
		t32_wr32(t, T32_AEA_T_SCMODE, 1);
		t32_wr32(t, 1712, in[12] + 1);
		t32_wr32(t, 1716, 1);
	}
	if (t32_ld32(in, 16)) {
		t32_wr16(t, T32_AEA_T_SCFLAG, 1);
		t32_wr16(t, T32_AEA_T_COMP, t32_ld32(in, 20));
	} else {
		t32_wr16(t, T32_AEA_T_SCFLAG, 0);
	}
	if (t32_ld32(in, 24)) {
		v = t32_ld32(in, 28);
		t32_wr32(t, T32_AEA_T_EVSTART, v);
		t32_wr32(t, T32_AEA_T_EVSTART + 4, 0);
		t32_wr16(c->buf, T32_AEA_BUF_STABLE, 0);
	}
}

/* returns the scene luma (also the 9th word of get_scene_par) and shifts the luma history in st */
static inline u32 t32_aea_get_scene_luma(const struct t32_aea_ctx *c, const struct t32_ae_ops *ops)
{
	const u8 *t = c->tune, *b = c->buf;
	u8 *st = c->st;
	u32 sh = t32_st16(t, 1736), sum, p, a1, m, t6, lum, k, acc = 0;

	sum = ((t32_ld32(b, 0) >> 1) & 0x3fffffffU) + (t32_ld32(b, 1020) & 0x3fffffffU);
	p = t32_st16(t, 2770) * t32_st16(t, 2774) * t32_st16(b, 11796) * t32_st16(b, 11826);
	a1 = sum < p ? p - sum : 1;
	m = ops->mult2_32(sh, t32_ld32(st, 8 * ((t32_ld32(t, 1656) & 0xffffU) + 1)),
			  ops->div32(sh, a1 << sh, p << sh));
	t6 = m ? m : 1;
	lum = t32_aea_get_luma(c);
	if (!lum)
		lum = 1;
	for (k = 0; k < 14; k++)
		t32_wr32(st, T32_AEA_ST_SCENE + 4 * k, t32_ld32(st, T32_AEA_ST_SCENE + 4 + 4 * k));
	t32_wr32(st, T32_AEA_ST_SCENE + 56, ops->div32(sh, t6, lum << sh) >> 2);
	for (k = 1; k <= 8; k++)
		acc += k * t32_ld32(st, T32_AEA_ST_SCENE + 4 * (k - 1));
	return acc / 37;
}

static inline void t32_aea_get_scene_par(const struct t32_aea_ctx *c, u8 *out, const struct t32_ae_ops *ops)
{
	const u8 *t = c->tune;
	u32 mode = t32_ld32(t, T32_AEA_T_SCMODE);

	if (mode == 0) {
		t32_wr32(out, 0, 1);
		out[4] = 0;
		t32_wr32(out, 8, 1);
		out[12] = 0;
	} else if (mode == 1) {
		t32_wr32(out, 0, 3);
		t32_wr32(out, 8, 3);
		out[4] = (u8)(t[T32_AEA_T_SCA16] - 1);
		out[12] = (u8)(t[T32_AEA_T_SCB16] - 1);
	} else if (mode == 2) {
		t32_wr32(out, 0, 2);
		t32_wr32(out, 8, 2);
		out[4] = (u8)(t32_ld32(t, 1708) - 1);
		out[12] = (u8)(t32_ld32(t, 1712) - 1);
	}
	t32_wr32(out, 20, t32_st16(t, T32_AEA_T_COMP));
	t32_wr32(out, 16, t32_st16(t, T32_AEA_T_COMP) != 128);
	t32_wr32(out, 24, (t32_st16(c->buf, T32_AEA_BUF_STABLE) ^ 1) != 0);
	t32_wr32(out, 28, t32_ld32(t, T32_AEA_T_EVSTART));
	t32_wr32(out, 32, t32_aea_get_luma(c));
	t32_wr32(out, 36, t32_aea_get_scene_luma(c, ops));
}

/*
 * AE exposure / ev / at lists (vendor tisp_ae_api_set_exp_list / get_exp_list,
 * tisp_ae_set_ev_list / get_ev_list / set_at_list / get_at_list), checked by
 * t32_aead_oracle.  exp list: 16 entries of 20 bytes (4 words used); set stores
 * 4 words per entry at tune + 588 (contiguous), get reads entry words 1..3 from
 * tune + 592 + 16 i and st + 680 + 16 i / + 4 (the vendor is not symmetric).
 * ev list: 15 entries of 8 bytes at tune + 1496 (or the default at st + 1544),
 * at list: 15 u16 at tune + 2870 (or the default at st + 1664); flag byte at
 * st + 2180 / 2181.  Setting with flag 1 stores the caller's list, any other
 * flag copies the default list.
 */
#define T32_AEA_T_EXPEN		1740U
#define T32_AEA_T_EXPL		588U
#define T32_AEA_ST_EXPL		680U
#define T32_AEA_T_EVL		1496U
#define T32_AEA_ST_EVL		1544U
#define T32_AEA_T_ATL		2870U
#define T32_AEA_ST_ATL		1664U

/* dcw: the channel word at dc284 + ch * 904 + 900 (>= 2: warning (hook), nothing else); returns 0 */
static inline void t32_aea_set_exp_list(const struct t32_aea_ctx *c, u32 dcw, u32 flag, const u8 *in,
					void (*warn)(void *priv), void *priv)
{
	u32 i, j;

	if (dcw >= 2) {
		warn(priv);
		return;
	}
	t32_wr32(c->tune, T32_AEA_T_EXPEN, flag);
	if (flag != 1)
		return;
	for (i = 0; i < 16; i++)
		for (j = 0; j < 4; j++)
			t32_wr32(c->tune, T32_AEA_T_EXPL + 16 * i + 4 * j, t32_ld32(in, 20 * i + 4 * j));
	c->st[1285] = 1;
}

/* out: 16 entries of 5 words at out + 0 (word 0 and 4 stay 0), flag word at *flag */
static inline void t32_aea_get_exp_list(const struct t32_aea_ctx *c, u8 *flag, u8 *out)
{
	u32 i;

	for (i = 0; i < 16; i++) {
		t32_wr32(out, 20 * i, 0);
		t32_wr32(out, 20 * i + 4, t32_ld32(c->tune, T32_AEA_T_EXPL + 4 + 16 * i));
		t32_wr32(out, 20 * i + 8, t32_ld32(c->st, T32_AEA_ST_EXPL + 16 * i));
		t32_wr32(out, 20 * i + 12, t32_ld32(c->st, T32_AEA_ST_EXPL + 4 + 16 * i));
		t32_wr32(out, 20 * i + 16, 0);
	}
	t32_wr32(flag, 0, t32_ld32(c->tune, T32_AEA_T_EXPEN));
}

static inline void t32_aea_set_ev_list(const struct t32_aea_ctx *c, u32 flag, const u8 *in)
{
	u32 i;

	flag &= 0xff;
	if (flag == 1) {
		for (i = 0; i < 15; i++) {
			t32_wr32(c->tune, T32_AEA_T_EVL + 8 * i, t32_ld32(in, 8 * i));
			t32_wr32(c->tune, T32_AEA_T_EVL + 8 * i + 4, t32_ld32(in, 8 * i + 4));
		}
	} else {
		t32_aea_cpy(c->tune + T32_AEA_T_EVL, c->st + T32_AEA_ST_EVL, 120);
	}
	c->st[1285] = 1;
	c->st[2180] = (u8)flag;
}

/* out: word 0 = flag byte, then 15 entries of 8 bytes from out + 8 */
static inline void t32_aea_get_ev_list(const struct t32_aea_ctx *c, u8 *out)
{
	u32 i;

	for (i = 0; i < 15; i++) {
		t32_wr32(out, 8 + 8 * i, t32_ld32(c->tune, T32_AEA_T_EVL + 8 * i));
		t32_wr32(out, 12 + 8 * i, t32_ld32(c->tune, T32_AEA_T_EVL + 8 * i + 4));
	}
	t32_wr32(out, 0, c->st[2180]);
}

static inline void t32_aea_set_at_list(const struct t32_aea_ctx *c, u32 flag, const u8 *in)
{
	u32 i;

	flag &= 0xff;
	if (flag == 1) {
		for (i = 0; i < 15; i++)
			t32_wr16(c->tune, T32_AEA_T_ATL + 2 * i, t32_ld32(in, 8 * i));
	} else {
		t32_aea_cpy(c->tune + T32_AEA_T_ATL, c->st + T32_AEA_ST_ATL, 30);
	}
	c->st[2181] = (u8)flag;
	c->st[1285] = 1;
}

static inline void t32_aea_get_at_list(const struct t32_aea_ctx *c, u8 *out)
{
	u32 i;

	for (i = 0; i < 15; i++) {
		t32_wr32(out, 8 + 8 * i, t32_st16(c->tune, T32_AEA_T_ATL + 2 * i));
		t32_wr32(out, 12 + 8 * i, 0);
	}
	t32_wr32(out, 0, c->st[2181]);
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

/*
 * Per-frame AWB algorithm paths (vendor tisp_awb_algo_waitdone,
 * tisp_awb_algo_getinfo, tisp_awb_algo_init, tisp_awb_sleep; Matt Davis'
 * recovered C, PR #15), checked by tests/t32_oracle (t32_awbin_oracle).
 *
 * The vendor's e16b4[ch] block is struct t32_awb_algo_ctx (words 0 / 1 =
 * tune / sec, the completion at +16, the zone copy buffer at +28), e16c4[ch]
 * is struct t32_awb_algo_state (mode word and three location words).
 * tisp_awb_set_lum_th_freq, the completion, vmalloc / vfree and the
 * out-of-memory message are hooks.  Quirk kept: in getinfo the compare
 * words are out[6] / out[7] against lum_a / lum_c, lum_b is only copied.
 */
#define T32_AWB_ALG_WAIT_MS		200U
#define T32_AWB_ALG_ZONE_BYTES		12288U
#define T32_AWB_ALG_ZONE_SRC		0xe4a8U

struct t32_awb_algo_ctx {
	u8 *tune;
	u8 *sec;
	u8 *completion;
	u8 *zone;
};

struct t32_awb_algo_state {
	u32 mode, lum_a, lum_b, lum_c;
};

struct t32_awb_algo_ops {
	void (*init_completion)(u8 *c);
	void *(*vmalloc)(u32 size);
	void (*vfree)(void *p);
	u32 (*wait_completion)(u8 *c, u32 ms);	/* 0 = timeout */
	void (*lum_th_freq)(u32 ch, struct t32_awb_algo_state *st);	/* may rewrite the state words */
	void (*no_memory)(void);
};

/* 0, or -62 when the statistics are not done within 200 ms */
static inline int t32_awb_algo_waitdone(const struct t32_awb_algo_state *st,
					const struct t32_awb_algo_ctx *c,
					const struct t32_awb_algo_ops *ops)
{
	if (st->mode != 1)
		return 0;
	return ops->wait_completion(c->completion, T32_AWB_ALG_WAIT_MS) ? 0 : -62;
}

/* out: 9 words, out[1] = channel on input; returns the zone buffer or 0 */
static inline u8 *t32_awb_algo_getinfo(struct t32_awb_algo_ctx *c, struct t32_awb_algo_state *st,
				       u32 *out, const struct t32_awb_algo_ops *ops)
{
	u32 ch = out[1], k;

	if (!c)
		return 0;
	if (!st->mode)
		return 0;
	out[2] = t32_st16(c->sec, T32_AWB_SEC_B0);
	out[3] = t32_st16(c->sec, T32_AWB_SEC_B1);
	out[4] = t32_st16(c->sec, T32_AWB_SEC_A0);
	out[5] = t32_st16(c->sec, T32_AWB_SEC_A1);
	if (out[6] != st->lum_a || out[7] != st->lum_c) {
		st->lum_c = out[7];
		st->lum_a = out[6];
		ops->lum_th_freq(ch, st);
	}
	out[6] = st->lum_a;
	out[7] = st->lum_c;
	out[8] = st->lum_b;
	if (c->zone)
		for (k = 0; k < T32_AWB_ALG_ZONE_BYTES; k++)
			c->zone[k] = c->sec[T32_AWB_ALG_ZONE_SRC + k];
	return c->zone;
}

/* info: 20 bytes, the vendor's caller structure */
static inline void t32_awb_algo_init(struct t32_awb_algo_ctx *c, struct t32_awb_algo_state *st,
				     u32 mode, u8 *info, const struct t32_awb_algo_ops *ops)
{
	st->mode = mode;
	if (mode != 1) {
		if (c->zone) {
			ops->vfree(c->zone);
			c->zone = 0;
		}
		return;
	}
	ops->init_completion(c->completion);
	t32_wr32(info, 0, mode);
	t32_wr32(info, 4, t32_st16(c->tune, 3188));
	t32_wr32(info, 8, t32_st16(c->tune, 3184));
	info[12] = (u8)t32_st16(c->tune, 3190);
	info[13] = (u8)t32_st16(c->tune, 3186);
	t32_wr32(info, 16, 1);
	c->zone = (u8 *)ops->vmalloc(T32_AWB_ALG_ZONE_BYTES);
	if (!c->zone)
		ops->no_memory();
}

/* tisp_awb_sleep: vendor e1670[ch] (2 words) and e1630[ch] (4 words) */
struct t32_awb_sleep_state {
	u32 e1670[2];
	u32 e1630[4];
};

static inline void t32_awb_sleep(struct t32_awb_sleep_state *s, u32 (*rd)(u32 reg),
				 u32 (*sys_rd)(u32 reg))
{
	s->e1670[0] = rd(0x160e0);
	s->e1670[1] = rd(0x160e4);
	s->e1630[0] = rd(0x6040);
	s->e1630[1] = rd(0x6044);
	s->e1630[2] = sys_rd(0x2a040);
	s->e1630[3] = sys_rd(0x2a044);
 * AE statistics block and special area (vendor tisp_ae_get_ae_statis /
 * tisp_ae_sepc_area), checked by t32_aeae_oracle.  The vendor takes the T32
 * spin lock around get_ae_statis; that is left to the caller.  get_ae_statis
 * output: 5 little-endian u16 from hist + 1024 + 4 * i at out + 0, the
 * 1024-byte hist block at out + 10, the y-zone block at out + 1034.
 * sepc_area input words: [0] = enable-like value stored to st + 1520,
 * [1..4] = x0 / y0 / x1 / y1 in pixels, [5] = strength (clamped to 10..200,
 * written back to the input block).  The ROI is handed to tisp_ae_roi_point
 * through the hook; ax0 / ay0 / ax1 / ay1 are the per-channel byte slots
 * (e151c / e1524 / e1518 / e1520 + ch).  A zero divisor yields ~0 as in the
 * recovered reference.
 */
#define T32_AEA_HIST_STAT	1024U
#define T32_AEA_BUF_W		8548U
#define T32_AEA_BUF_H		8550U
#define T32_AEA_ST_SEPC		1520U
#define T32_AEA_ST_SEPC_OFF	2182U
#define T32_AEA_ST_SEPC_STR	1296U
#define T32_AEA_ST_SEPC_IN	1500U
#define T32_AEA_SEPC_GRID	15U

static inline void t32_aea_get_ae_statis(const struct t32_aea_ctx *c, u8 *out)
{
	u32 i, v;

	t32_aea_cpy(out + 10, c->hist, 1024);
	for (i = 0; i < 5; i++) {
		v = t32_st16(c->hist, T32_AEA_HIST_STAT + 4 * i);
		out[2 * i] = (u8)v;
		out[2 * i + 1] = (u8)(v >> 8);
	}
	t32_aea_cpy(out + 1034, c->buf + T32_AEA_BUF_YZONE, T32_AEA_YZONE_BYTES);
}

static inline u32 t32_aea_div(u32 n, u32 d)
{
	return d ? n / d : ~0U;
}

typedef void (*t32_aea_roi_fn)(void *priv, u32 ch, u32 x0, u32 y0, u32 x1, u32 y1);

static inline void t32_aea_sepc_area(const struct t32_aea_ctx *c, u32 ch, u8 *in, u8 *ax0, u8 *ay0,
				     u8 *ax1, u8 *ay1, t32_aea_roi_fn roi, void *priv)
{
	u32 w, h, x0, y0, x1, y1, s;

	if (t32_ld32(c->st, T32_AEA_ST_SEPC) != t32_ld32(in, 0))
		t32_wr32(c->st, T32_AEA_ST_SEPC, t32_ld32(in, 0));
	if (t32_ld32(in, 4) || t32_ld32(in, 8) || t32_ld32(in, 12) || t32_ld32(in, 16))
		c->st[T32_AEA_ST_SEPC_OFF] = 0;
	else
		c->st[T32_AEA_ST_SEPC_OFF] = 1;
	w = t32_aea_div(t32_st16(c->buf, T32_AEA_BUF_W), T32_AEA_SEPC_GRID);
	h = t32_aea_div(t32_st16(c->buf, T32_AEA_BUF_H), T32_AEA_SEPC_GRID);
	x0 = t32_aea_div(t32_ld32(in, 4), w) & 0xff;
	x1 = (t32_aea_div(t32_ld32(in, 12), w) + 1) & 0xff;
	y0 = t32_aea_div(t32_ld32(in, 8), h) & 0xff;
	y1 = (t32_aea_div(t32_ld32(in, 16), h) + 1) & 0xff;
	*ax0 = (u8)x0;
	*ax1 = (u8)x1;
	*ay0 = (u8)y0;
	*ay1 = (u8)y1;
	if (x0 >= 15)
		*ax0 = 14;
	if (y0 >= 15)
		*ay0 = 14;
	if (x1 >= 15)
		*ax1 = 15;
	if (y1 >= 15)
		*ay1 = 15;
	roi(priv, ch & 0xff, t32_ld32(in, 4) & 0xffff, t32_ld32(in, 8) & 0xffff,
	    t32_ld32(in, 12) & 0xffff, t32_ld32(in, 16) & 0xffff);
	s = t32_ld32(in, 20);
	if (s >= 201)
		t32_wr32(in, 20, 200);
	else if (s < 10)
		t32_wr32(in, 20, 10);
	c->st[T32_AEA_ST_SEPC_STR] = (u8)t32_ld32(in, 20);
	for (s = 0; s < 5; s++)
		t32_wr32(c->st, T32_AEA_ST_SEPC_IN + 4 * s, t32_ld32(in, 4 + 4 * s));
}

/*
 * AE / AWB info blocks (vendor tisp_set_ae_info, tisp_get_awb_info,
 * tisp_set_awb_info), checked by t32_aeaf_oracle.  Layout of the info block:
 * word at +4 = payload size, payload at +27 (unaligned).  tisp_set_ae_info
 * only stores the size 164 and prints the 41 payload words (print hook);
 * tisp_get_ae_info is "return 0" in the vendor and not ported.  The awb pair
 * moves the 28 byte tisp_awb_get_mode / tisp_awb_set_mode block (7 words);
 * set_info copies only the first 3 words, the only ones tisp_awb_set_mode reads.
 */
#define T32_AEA_INFO_PAYLOAD	27U
#define T32_AEA_INFO_AE_BYTES	164U
#define T32_AWB_INFO_BYTES	28U

typedef void (*t32_aea_info_print_fn)(void *priv, const u32 *w);

static inline void t32_aea_set_ae_info(u8 *blk, t32_aea_info_print_fn print, void *priv)
{
	u32 w[T32_AEA_INFO_AE_BYTES / 4], i;

	t32_wr32(blk, 4, T32_AEA_INFO_AE_BYTES);
	for (i = 0; i < T32_AEA_INFO_AE_BYTES / 4; i++)
		w[i] = t32_ld32(blk, T32_AEA_INFO_PAYLOAD + 4 * i);
	print(priv, w);
}

static inline void t32_awb_api_get_info(const struct t32_awb_ctx *c, u8 *blk)
{
	u32 m[7], i;

	t32_awb_get_mode(c, m);
	t32_wr32(blk, 4, T32_AWB_INFO_BYTES);
	for (i = 0; i < 7; i++)
		t32_wr32(blk, T32_AEA_INFO_PAYLOAD + 4 * i, m[i]);
}

static inline void t32_awb_api_set_info(const struct t32_awb_ctx *c, u8 *blk)
{
	u32 m[3], i;

	t32_wr32(blk, 4, T32_AWB_INFO_BYTES);
	for (i = 0; i < 3; i++)
		m[i] = t32_ld32(blk, T32_AEA_INFO_PAYLOAD + 4 * i);
	t32_awb_set_mode(c, m);
}

/*
 * AE parameter array (vendor tisp_ae_param_array_get / tisp_ae_param_array_set),
 * checked by t32_aeag_oracle.  The array is { tune 3288 bytes, buf 12848 bytes }.
 * get first gathers live state into the blocks (st + 1276 -> buf + 12833, 9
 * bytes; st + 576 -> buf + 8608, 32 bytes; 16 word pairs st + 680 + 16 * k ->
 * tune + 596 + 16 * k; st + 1260 -> tune + 1664; the current fps word of the
 * channel split into buf + 11776 (high half) and buf + 11780 (low half)), then
 * copies both blocks out and reports 16136 bytes.  set copies both blocks in,
 * clears the exposure-list enable word when the channel's dc284 word is >= 2
 * and flags st + 1285.
 */
#define T32_AEA_PA_TUNE		3288U
#define T32_AEA_PA_BUF		12848U
#define T32_AEA_PA_TOTAL	16136U
#define T32_AEA_PA_ST9		1276U
#define T32_AEA_PA_BUF9		12833U
#define T32_AEA_PA_ST32		576U
#define T32_AEA_PA_BUF32	8608U
#define T32_AEA_PA_ST_PAIR	680U
#define T32_AEA_PA_T_PAIR	596U
#define T32_AEA_PA_ST_T1664	1260U
#define T32_AEA_PA_T_T1664	1664U
#define T32_AEA_PA_BUF_FPSH	11776U
#define T32_AEA_PA_BUF_FPSL	11780U

static inline void t32_aea_param_array_get(const struct t32_aea_ctx *c, u8 *out, u32 *len)
{
	u32 k, fps;

	t32_aea_cpy(c->buf + T32_AEA_PA_BUF9, c->st + T32_AEA_PA_ST9, 9);
	t32_aea_cpy(c->buf + T32_AEA_PA_BUF32, c->st + T32_AEA_PA_ST32, 32);
	for (k = 0; k < 16; k++) {
		t32_wr32(c->tune, T32_AEA_PA_T_PAIR + 16 * k, t32_ld32(c->st, T32_AEA_PA_ST_PAIR + 16 * k));
		t32_wr32(c->tune, T32_AEA_PA_T_PAIR + 16 * k + 4, t32_ld32(c->st, T32_AEA_PA_ST_PAIR + 16 * k + 4));
	}
	t32_wr32(c->tune, T32_AEA_PA_T_T1664, t32_ld32(c->st, T32_AEA_PA_ST_T1664));
	fps = t32_ld32(c->fps, T32_AEA_FPS_CUR);
	t32_wr32(c->buf, T32_AEA_PA_BUF_FPSH, fps >> 16);
	t32_wr32(c->buf, T32_AEA_PA_BUF_FPSL, fps & 0xffff);
	t32_aea_cpy(out, c->tune, T32_AEA_PA_TUNE);
	t32_aea_cpy(out + T32_AEA_PA_TUNE, c->buf, T32_AEA_PA_BUF);
	*len = T32_AEA_PA_TOTAL;
}

static inline void t32_aea_param_array_set(const struct t32_aea_ctx *c, u32 dcw, const u8 *in)
{
	t32_aea_cpy(c->tune, in, T32_AEA_PA_TUNE);
	t32_aea_cpy(c->buf, in + T32_AEA_PA_TUNE, T32_AEA_PA_BUF);
	if (dcw >= 2)
		t32_wr32(c->tune, T32_AEA_T_EXPEN, 0);
	c->st[1285] = 1;
}

/*
 * AWB parameter array (vendor tisp_awb_param_array_get / _set), checked by
 * t32_awbab_oracle.  The array is { tune 6740 bytes, hw 4292 bytes }; get
 * reports 11032 bytes.  set also flags the section, runs the refresh over the
 * new tune block, programs the hardware unless the freeze byte is set and
 * always calls the show-parameter update (hooks).
 */
#define T32_AWB_PA_TUNE		6740U
#define T32_AWB_PA_HW		4292U
#define T32_AWB_PA_TOTAL	11032U

typedef void (*t32_awb_pa_fn)(void *priv, u32 ch);
typedef void (*t32_awb_pa_refresh_fn)(void *priv, u32 ch, u8 *tune);

static inline void t32_awb_param_array_get(const struct t32_awb_ctx *c, const u8 *hw, u8 *out, u32 *len)
{
	t32_aea_cpy(out, c->tune, T32_AWB_PA_TUNE);
	t32_aea_cpy(out + T32_AWB_PA_TUNE, hw, T32_AWB_PA_HW);
	*len = T32_AWB_PA_TOTAL;
}

static inline void t32_awb_param_array_set(const struct t32_awb_ctx *c, u8 *hw, u32 ch, const u8 *in,
					   t32_awb_pa_refresh_fn refresh, t32_awb_pa_fn set_hw, t32_awb_pa_fn show,
					   void *priv)
{
	c->sec[T32_AWB_SEC_FLAG] = 1;
	t32_aea_cpy(c->tune, in, T32_AWB_PA_TUNE);
	t32_aea_cpy(hw, in + T32_AWB_PA_TUNE, T32_AWB_PA_HW);
	refresh(priv, ch, c->tune);
	if (!c->sec[T32_AWB_SEC_FRZ])
		set_hw(priv, ch);
	show(priv, ch);
}

/*
 * AE day/night parameter refresh and the long luma-histogram process (vendor
 * api_ae_dn_par_refresh / api_ae_long_process), checked by t32_aeah_oracle.
 * dn_par_refresh: hardware parameter hook, deflicker config hook, the riscv
 * hook only when the channel's f0900 byte is 1, then st + 1285 = 1.
 * long_process (the vendor takes the T32 spin lock around the copies; left to
 * the caller): copies the first 1024 buf bytes to the comp and lum blocks and
 * the 4 hist_bin bytes hist + 1044 to lum + 1044, sums the 256 buf words into
 * five bins bounded by those four bytes (lum + 1024 + 4 * k), scales bins 1..4
 * to a total of 0xffff (x * 0xffff / total, the vendor's fix_point_mult2 /
 * fix_point_div with shift 0, done in 64 bit here) and sets bin 0 to 0xffff -
 * the others (all zero with bin 0 = 0xffff when the total is 0).  The result is
 * copied to the hist block, hist words 1..255 are multiplied by 4 and
 * width * height - their sum is stored in hist word 0.
 */
#define T32_AEA_LP_BYTES	1024U
#define T32_AEA_LP_BINS		1024U
#define T32_AEA_LP_BIN_BYTES	20U
#define T32_AEA_LP_WORDS	256U
#define T32_AEA_LP_FULL		0xffffU
#define T32_AEA_ST_DN		1285U

typedef void (*t32_aea_ch_fn)(void *priv, u32 ch);

static inline void t32_aea_dn_par_refresh(const struct t32_aea_ctx *c, u32 ch, u8 f0900,
					  t32_aea_ch_fn set_hw, t32_aea_ch_fn deflicker,
					  t32_aea_ch_fn riscv, void *priv)
{
	set_hw(priv, ch & 0xff);
	deflicker(priv, ch);
	if (f0900 == 1)
		riscv(priv, ch & 0xff);
	c->st[T32_AEA_ST_DN] = 1;
}

static inline void t32_aea_long_process(const struct t32_aea_ctx *c)
{
	u32 b[6], sum[5], total, i, k, acc;

	t32_aea_cpy(c->comp, c->buf, T32_AEA_LP_BYTES);
	t32_aea_cpy(c->lum, c->buf, T32_AEA_LP_BYTES);
	t32_aea_cpy(c->lum + T32_AEA_HIST_BIN, c->hist + T32_AEA_HIST_BIN, 4);
	b[0] = 0;
	for (k = 0; k < 4; k++)
		b[k + 1] = c->lum[T32_AEA_HIST_BIN + k];
	b[5] = T32_AEA_LP_WORDS;
	total = 0;
	for (k = 0; k < 5; k++) {
		sum[k] = 0;
		for (i = b[k]; i < b[k + 1]; i++)
			sum[k] += t32_ld32(c->buf, 4 * i);
		total += sum[k];
	}
	if (total == 0) {
		sum[0] = T32_AEA_LP_FULL;
		sum[1] = sum[2] = sum[3] = sum[4] = 0;
	} else {
		for (k = 1; k < 5; k++)
			sum[k] = (u32)((t32_u64)sum[k] * T32_AEA_LP_FULL / total);
		sum[0] = T32_AEA_LP_FULL - sum[4] - sum[1] - sum[2] - sum[3];
	}
	for (k = 0; k < 5; k++)
		t32_wr32(c->lum, T32_AEA_LP_BINS + 4 * k, sum[k]);
	t32_aea_cpy(c->hist, c->lum, T32_AEA_LP_BYTES);
	acc = 0;
	for (i = 1; i < T32_AEA_LP_WORDS; i++) {
		t32_wr32(c->hist, 4 * i, t32_ld32(c->hist, 4 * i) << 2);
		acc += t32_ld32(c->hist, 4 * i);
	}
	t32_wr32(c->hist, 0, t32_st16(c->buf, T32_AEA_BUF_W) * t32_st16(c->buf, T32_AEA_BUF_H) - acc);
	t32_aea_cpy(c->hist + T32_AEA_LP_BINS, c->lum + T32_AEA_LP_BINS, T32_AEA_LP_BIN_BYTES);
}

/*
 * AWB face position (vendor tisp_awb_face_pos 0x3ee64, called by
 * tisp_awb_api_set_face), checked by t32_awbad_oracle.  The face box
 * (x0 = a2, x1 = a3, y0 = a4, y1 = a5, u16) is made ordered (x0 = min(x0, x1),
 * y0 = min(y0, y1)), raised to at least 2, and shrunk up to twice: each step
 * moves the lower edge up by a sixth of the span and the upper edge to
 * lower + 5/6 of the new span, while a quarter of the area is still not below
 * an eighth of the picture area (u16 at sec + 28690 * u16 at sec + 28692).
 * flag == 1: bumps the counter word at sec + 119940 (wraps at 255) and writes
 * the box to the AWB window registers (y1 << 16 | y0 at 0x160e0, x1 << 16 | x0
 * at 0x160e4), then raises the trigger and sets sec + 28706 = 1,
 * sec + 28707 = 0; any other flag writes 0x20002 to both registers, triggers
 * and clears sec + 28706.  The section flag byte (sec + 0x1a44c) is set last.
 * The vendor's shadow-register variant (data d03a0 != 0, indices 1923 / 1924)
 * is not ported: always direct addresses, as in the other register writers.
 */
#define T32_AWB_SEC_FACE_W	28690U
#define T32_AWB_SEC_FACE_H	28692U
#define T32_AWB_SEC_FACE_EN	28706U
#define T32_AWB_SEC_FACE_EN2	28707U
#define T32_AWB_SEC_FACE_CNT	119940U
#define T32_AWB_FACE_REG_Y	90336U
#define T32_AWB_FACE_REG_X	90340U
#define T32_AWB_FACE_OFF	0x20002U
#define T32_AWB_FACE_CNT_WRAP	255U

typedef void (*t32_awb_wr_fn)(void *priv, u32 reg, u32 val);
typedef void (*t32_awb_trig_fn)(void *priv, u32 kind, u32 arg);

static inline void t32_awb_face_pos(const struct t32_awb_ctx *c, u32 flag, u32 a2, u32 a3, u32 a4, u32 a5,
				    t32_awb_wr_fn wr, t32_awb_trig_fn trig, void *priv)
{
	u32 x0 = a2 & 0xffff, x1 = a3 & 0xffff, y0 = a4 & 0xffff, y1 = a5 & 0xffff;
	u32 thr = (t32_st16(c->sec, T32_AWB_SEC_FACE_W) * t32_st16(c->sec, T32_AWB_SEC_FACE_H)) >> 3;
	u32 n, cnt;

	if (x1 < x0)
		x0 = x1;
	if (x0 < 2)
		x0 = 2;
	if (x1 < 2)
		x1 = 2;
	if (y1 < y0)
		y0 = y1;
	if (y0 < 2)
		y0 = 2;
	if (y1 < 2)
		y1 = 2;
	for (n = 2; n; n--) {
		x0 = (x0 + (u32)((int)(x1 - x0) / 6)) & 0xffff;
		x1 = (x0 + (u32)((int)((x1 - x0) * 5) / 6)) & 0xffff;
		y0 = (y0 + (u32)((int)(y1 - y0) / 6)) & 0xffff;
		y1 = (y0 + (u32)((int)((y1 - y0) * 5) / 6)) & 0xffff;
		if ((u32)((int)((x1 - x0) * (y1 - y0)) / 4) < thr)
			break;
	}
	if ((flag & 0xff) == 1) {
		cnt = t32_ld32(c->sec, T32_AWB_SEC_FACE_CNT) + 1;
		if (cnt == T32_AWB_FACE_CNT_WRAP)
			cnt = 0;
		t32_wr32(c->sec, T32_AWB_SEC_FACE_CNT, cnt);
		wr(priv, T32_AWB_FACE_REG_Y, y1 << 16 | y0);
		wr(priv, T32_AWB_FACE_REG_X, x1 << 16 | x0);
		trig(priv, 1, 0);
		c->sec[T32_AWB_SEC_FACE_EN] = 1;
		c->sec[T32_AWB_SEC_FACE_EN2] = 0;
	} else {
		wr(priv, T32_AWB_FACE_REG_Y, T32_AWB_FACE_OFF);
		wr(priv, T32_AWB_FACE_REG_X, T32_AWB_FACE_OFF);
		trig(priv, 1, 0);
		c->sec[T32_AWB_SEC_FACE_EN] = 0;
	}
	c->sec[T32_AWB_SEC_FLAG] = 1;
}

/*
 * AE exposure info attribute (vendor api_ae_get_exprinfo_attr 0x33498 /
 * api_ae_set_exprinfo_attr 0x338e0), checked by t32_aeai_oracle.  Layout of the
 * attribute block (words): [0] scale flag, [1] enable, [2..5] unit flags, [6..9]
 * unit values, [10..17] tune enables with the values [18..25], [27..30] flags,
 * [31..34] values, [35..42] tune enables with the values [43..50]; get also
 * reports [51] (word 204) .. [54] (word 216).  The ae gain / time values that
 * carry the scale flag are multiplied by q on get and divided by it on set,
 * with q = (fps word & 0xffff) * 1000000 / ((fps word >> 16) * u16 fps + 2),
 * from the per-channel dd2d4 block (c->fps).  get first waits (10 ms steps,
 * 50 at most) until the state byte st + 1285 is clear and returns the number of
 * steps, 50 on timeout (nothing written); the vendor's set stores 1 in
 * tune + 1259 / + 1260 when the enable of words [36] / [37] is not 1 (the
 * disabled flag byte is 1 there, 0 everywhere else).  Disabled tune entries
 * fall back to dd2d4 words (68, 72, 88, 92), 1024, or tisp_math_exp2 of dd2d4
 * words 36 / 40 (hook).
 */
typedef void (*t32_aea_msleep_fn)(void *priv, u32 ms);
typedef u32 (*t32_aea_math_fn)(void *priv, u32 a0, u32 a1, u32 a2);

static inline u32 t32_aea_expi_q(const u8 *dd)
{
	u32 w = t32_ld32(dd, 56);

	return t32_aea_div((w & 0xffff) * 1000000U, (w >> 16) * t32_st16(dd, 2));
}

static inline u32 t32_aea_get_exprinfo_attr(const struct t32_aea_ctx *c, u8 *out, t32_aea_msleep_fn msleep,
					    t32_aea_math_fn log2, void *priv)
{
	const u8 *tune = c->tune, *buf = c->buf;
	u32 n = 0, q, f, k, b;

	q = t32_aea_expi_q(c->fps);
	while (c->st[1285]) {
		msleep(priv, 10);
		n = (n + 1) & 0xff;
		if (n == 50)
			return 50;
	}
	t32_aea_cpy(c->buf + 12833, c->st + 1276, 9);
	t32_aea_cpy(c->buf + 8608, c->st + 576, 32);
	b = buf[12841];
	t32_wr32(out, 4, b == 1 ? 1 : 0);
	for (k = 0; k < 4; k++)
		t32_wr32(out, 8 + 4 * k, buf[12833 + k]);
	f = t32_ld32(out, 0);
	t32_wr32(out, 24, f ? q * t32_ld32(buf, 8608) : t32_ld32(buf, 8608));
	t32_wr32(out, 28, t32_ld32(buf, 8612));
	t32_wr32(out, 32, t32_ld32(buf, 8616));
	t32_wr32(out, 36, t32_ld32(buf, 8620));
	for (k = 0; k < 8; k++)
		t32_wr32(out, 40 + 4 * k, tune[1250 + k]);
	t32_wr32(out, 72, f ? q * t32_ld32(tune, 448) : t32_ld32(tune, 448));
	t32_wr32(out, 76, t32_ld32(tune, 452));
	t32_wr32(out, 80, t32_ld32(tune, 456));
	t32_wr32(out, 84, t32_ld32(tune, 460));
	t32_wr32(out, 88, f ? q * t32_ld32(tune, 464) : t32_ld32(tune, 464));
	t32_wr32(out, 92, t32_ld32(tune, 468));
	t32_wr32(out, 96, t32_ld32(tune, 472));
	t32_wr32(out, 100, t32_ld32(tune, 476));
	t32_wr32(out, 104, (t32_ld32(buf, 12836) & 0xffffff00U) == 0x01010100U && buf[12840] == 1 ? 1 : 0);
	for (k = 0; k < 4; k++)
		t32_wr32(out, 108 + 4 * k, buf[12837 + k]);
	t32_wr32(out, 124, f ? q * t32_ld32(buf, 8624) : t32_ld32(buf, 8624));
	t32_wr32(out, 128, t32_ld32(buf, 8628));
	t32_wr32(out, 132, t32_ld32(buf, 8632));
	t32_wr32(out, 136, t32_ld32(buf, 8636));
	for (k = 0; k < 8; k++)
		t32_wr32(out, 140 + 4 * k, tune[1258 + k]);
	t32_wr32(out, 172, f ? q * t32_ld32(tune, 480) : t32_ld32(tune, 480));
	t32_wr32(out, 176, t32_ld32(tune, 484));
	t32_wr32(out, 180, t32_ld32(tune, 488));
	t32_wr32(out, 184, t32_ld32(tune, 492));
	t32_wr32(out, 188, f ? q * t32_ld32(tune, 496) : t32_ld32(tune, 496));
	t32_wr32(out, 192, t32_ld32(tune, 500));
	t32_wr32(out, 196, t32_ld32(tune, 504));
	t32_wr32(out, 200, t32_ld32(tune, 508));
	t32_wr32(out, 204, t32_ld32(buf, 7480));
	t32_wr32(out, 208, t32_ld32(buf, 7512));
	t32_wr32(out, 212, t32_ld32(buf, 7476) << 22 | t32_ld32(buf, 7472) >> 10);
	t32_wr32(out, 216, log2(priv, t32_ld32(buf, 7472), 10, 16));
	return n;
}

/* a scaled word of the block: divided by q when the scale flag is set */
static inline u32 t32_aea_expi_val(const u8 *in, u32 f, u32 q, u32 word)
{
	return f ? t32_aea_div(t32_ld32(in, 4 * word), q) : t32_ld32(in, 4 * word);
}

static inline void t32_aea_set_exprinfo_attr(const struct t32_aea_ctx *c, const u8 *in, t32_aea_math_fn exp2, void *priv)
{
	u8 *tune = c->tune, *buf = c->buf;
	const u8 *dd = c->fps;
	u32 q = t32_aea_expi_q(dd), f = t32_ld32(in, 0);

	if (t32_ld32(in, 4) == 1) {
		buf[12841] = 1;
		t32_wr32(buf, 8608, t32_aea_expi_val(in, f, q, 6));
		t32_wr32(buf, 8612, t32_ld32(in, 28));
		t32_wr32(buf, 8616, t32_ld32(in, 32));
		t32_wr32(buf, 8620, t32_ld32(in, 36));
		t32_wr32(buf, 8624, t32_aea_expi_val(in, f, q, 31));
		t32_wr32(buf, 8628, t32_ld32(in, 128));
		t32_wr32(buf, 8632, t32_ld32(in, 132));
		t32_wr32(buf, 8636, t32_ld32(in, 136));
	} else {
		buf[12841] = 0;
		buf[12833] = (u8)t32_ld32(in, 8);
		if (t32_ld32(in, 8) == 1)
			t32_wr32(buf, 8608, t32_aea_expi_val(in, f, q, 6));
		buf[12834] = (u8)t32_ld32(in, 12);
		if (t32_ld32(in, 12) == 1)
			t32_wr32(buf, 8612, t32_ld32(in, 28));
		buf[12836] = (u8)t32_ld32(in, 20);
		if (t32_ld32(in, 20) == 1)
			t32_wr32(buf, 8620, t32_ld32(in, 36));
		buf[12835] = (u8)t32_ld32(in, 16);
		if (t32_ld32(in, 16) == 1)
			t32_wr32(buf, 8616, t32_ld32(in, 32));
		buf[12837] = (u8)t32_ld32(in, 108);
		if (t32_ld32(in, 108) == 1)
			t32_wr32(buf, 8624, t32_aea_expi_val(in, f, q, 31));
		buf[12838] = (u8)t32_ld32(in, 112);
		if (t32_ld32(in, 112) == 1)
			t32_wr32(buf, 8628, t32_ld32(in, 128));
		buf[12840] = (u8)t32_ld32(in, 120);
		if (t32_ld32(in, 120) == 1)
			t32_wr32(buf, 8636, t32_ld32(in, 136));
		buf[12839] = (u8)t32_ld32(in, 116);
		if (t32_ld32(in, 116) == 1)
			t32_wr32(buf, 8632, t32_ld32(in, 132));
	}
	/* tune entries 1250..1257: enable word [10 + k], value word [18 + k] */
	if (t32_ld32(in, 40) == 1) {
		tune[1250] = 1;
		t32_wr32(tune, 448, t32_aea_expi_val(in, f, q, 18));
	} else {
		tune[1250] = 0;
		t32_wr32(tune, 448, t32_ld32(dd, 68));
	}
	if (t32_ld32(in, 44) == 1) {
		tune[1251] = 1;
		t32_wr32(tune, 452, t32_ld32(in, 76));
	} else {
		tune[1251] = 0;
		t32_wr32(tune, 452, 1024);
	}
	if (t32_ld32(in, 48) == 1) {
		tune[1252] = 1;
		t32_wr32(tune, 456, t32_ld32(in, 80));
	} else {
		tune[1252] = 0;
		t32_wr32(tune, 456, 1024);
	}
	if (t32_ld32(in, 52) == 1) {
		tune[1253] = 1;
		t32_wr32(tune, 460, t32_ld32(in, 84));
	} else {
		tune[1253] = 0;
		t32_wr32(tune, 460, 1024);
	}
	if (t32_ld32(in, 56) == 1) {
		tune[1254] = 1;
		t32_wr32(tune, 464, t32_aea_expi_val(in, f, q, 22));
	} else {
		tune[1254] = 0;
		t32_wr32(tune, 464, t32_ld32(dd, 72));
	}
	if (t32_ld32(in, 60) == 1) {
		tune[1255] = 1;
		t32_wr32(tune, 468, t32_ld32(in, 92));
	} else {
		tune[1255] = 0;
		t32_wr32(tune, 468, exp2(priv, t32_ld32(dd, 36), 16, 10));
	}
	if (t32_ld32(in, 64) == 1) {
		tune[1256] = 1;
		t32_wr32(tune, 472, t32_ld32(in, 96));
	} else {
		tune[1256] = 0;
		t32_wr32(tune, 472, exp2(priv, t32_ld32(dd, 40), 16, 10));
	}
	if (t32_ld32(in, 68) == 1) {
		tune[1257] = 1;
		t32_wr32(tune, 476, t32_ld32(in, 100));
	} else {
		tune[1257] = 0;
	}
	/* tune entries 1258..1265: enable word [35 + k], value word [43 + k] */
	if (t32_ld32(in, 140) == 1) {
		tune[1258] = 1;
		t32_wr32(tune, 480, t32_aea_expi_val(in, f, q, 43));
	} else {
		tune[1258] = 0;
		t32_wr32(tune, 480, t32_ld32(dd, 88));
	}
	if (t32_ld32(in, 144) == 1) {
		tune[1259] = (u8)t32_ld32(in, 144);
		t32_wr32(tune, 484, t32_ld32(in, 176));
	} else {
		tune[1259] = 1;
		t32_wr32(tune, 484, 1024);
	}
	if (t32_ld32(in, 148) == 1) {
		tune[1260] = (u8)t32_ld32(in, 148);
		t32_wr32(tune, 488, t32_ld32(in, 180));
	} else {
		tune[1260] = 1;
		t32_wr32(tune, 488, 1024);
	}
	if (t32_ld32(in, 152) == 1) {
		tune[1261] = 1;
		t32_wr32(tune, 492, t32_ld32(in, 184));
	} else {
		tune[1261] = 0;
		t32_wr32(tune, 492, 1024);
	}
	if (t32_ld32(in, 156) == 1) {
		tune[1262] = 1;
		t32_wr32(tune, 496, t32_aea_expi_val(in, f, q, 47));
	} else {
		tune[1262] = 0;
		t32_wr32(tune, 496, t32_ld32(dd, 92));
	}
	if (t32_ld32(in, 160) == 1) {
		tune[1263] = 1;
		t32_wr32(tune, 500, t32_ld32(in, 192));
	} else {
		tune[1263] = 0;
		t32_wr32(tune, 500, exp2(priv, t32_ld32(dd, 36), 16, 10));
	}
	if (t32_ld32(in, 164) == 1) {
		tune[1264] = 1;
		t32_wr32(tune, 504, t32_ld32(in, 196));
	} else {
		tune[1264] = 0;
		t32_wr32(tune, 504, exp2(priv, t32_ld32(dd, 40), 16, 10));
	}
	if (t32_ld32(in, 168) == 1) {
		tune[1265] = 1;
		t32_wr32(tune, 508, t32_ld32(in, 200));
	} else {
		tune[1265] = 0;
	}
	c->st[1285] = 1;
}

/*
 * ioctl attribute glue of the AE / AWB blocks (vendor tx_isp_awb_weight_s_attr /
 * _g_attr, tx_isp_ae_explist_s_attr / _g_attr, tx_isp_ae_scence_g_attr, the
 * *_attr_isra_N functions), checked by t32_aeaj_oracle.  buf is the caller's
 * zeroed scratch block (the vendor's stack), user the user pointer (for the get
 * functions the vendor reads it from the first word of the argument block, the
 * caller does that), the ops are the kernel copy helpers, isp_printf (the
 * level only: 1 = call failed, 2 = copy failed) and the tisp_[sg]_* call.
 * set (copy in, then call): awb_weight_s stops after a failed copy and returns
 * its error; explist_s prints the error but still calls the setter, then
 * prints again and returns the copy result.  The setters' results are never
 * used.  get (call, then copy out): awb_weight_g / explist_g stop at a failed
 * getter (level 1, return its result); scence_g prints and still copies out;
 * all return the copy result (level 2 when it failed).
 */
struct t32_isra_ops {
	int (*copy_from_user)(void *priv, u8 *dst, u32 user, u32 n);
	int (*copy_to_user)(void *priv, u32 user, const u8 *src, u32 n);
	void (*printk)(void *priv, u32 level);
	int (*call)(void *priv, u32 ch, u8 *buf);	/* tisp_[sg]_xxx(ch, buf) of the function */
	void *priv;
};

#define T32_ISRA_AWB_WEIGHT	1024U
#define T32_ISRA_AE_EXPLIST	324U
#define T32_ISRA_AE_SCENCE	40U

static inline int t32_isra_set_stop(const struct t32_isra_ops *o, u32 ch, u32 user, u8 *buf, u32 n)
{
	int r = o->copy_from_user(o->priv, buf, user, n);

	if (r) {
		o->printk(o->priv, 2);
		return r;
	}
	o->call(o->priv, ch, buf);
	return 0;
}

static inline int t32_isra_set_go_on(const struct t32_isra_ops *o, u32 ch, u32 user, u8 *buf, u32 n)
{
	int r = o->copy_from_user(o->priv, buf, user, n);

	if (r)
		o->printk(o->priv, 2);
	o->call(o->priv, ch, buf);
	if (r)
		o->printk(o->priv, 1);
	return r;
}

static inline int t32_isra_get_stop(const struct t32_isra_ops *o, u32 ch, u32 user, u8 *buf, u32 n)
{
	int r = o->call(o->priv, ch, buf);

	if (r) {
		o->printk(o->priv, 1);
		return r;
	}
	r = o->copy_to_user(o->priv, user, buf, n);
	if (r)
		o->printk(o->priv, 2);
	return r;
}

static inline int t32_isra_get_go_on(const struct t32_isra_ops *o, u32 ch, u32 user, u8 *buf, u32 n)
{
	int r = o->call(o->priv, ch, buf);

	if (r)
		o->printk(o->priv, 1);
	r = o->copy_to_user(o->priv, user, buf, n);
	if (r)
		o->printk(o->priv, 2);
	return r;
}

static inline int t32_isra_awb_weight_s(const struct t32_isra_ops *o, u32 ch, u32 user, u8 *buf)
{
	return t32_isra_set_stop(o, ch, user, buf, T32_ISRA_AWB_WEIGHT);
}

static inline int t32_isra_awb_weight_g(const struct t32_isra_ops *o, u32 ch, u32 user, u8 *buf)
{
	return t32_isra_get_stop(o, ch, user, buf, T32_ISRA_AWB_WEIGHT);
}

static inline int t32_isra_ae_explist_s(const struct t32_isra_ops *o, u32 ch, u32 user, u8 *buf)
{
	return t32_isra_set_go_on(o, ch, user, buf, T32_ISRA_AE_EXPLIST);
}

static inline int t32_isra_ae_explist_g(const struct t32_isra_ops *o, u32 ch, u32 user, u8 *buf)
{
	return t32_isra_get_stop(o, ch, user, buf, T32_ISRA_AE_EXPLIST);
}

static inline int t32_isra_ae_scence_g(const struct t32_isra_ops *o, u32 ch, u32 user, u8 *buf)
{
	return t32_isra_get_go_on(o, ch, user, buf, T32_ISRA_AE_SCENCE);
}

#endif /* TX_ISP_T32_STATS_H */
