/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 AE helpers (binary oracle block r13D): tisp_ae_lib_bilinear_intp, tisp_ae_calc_convergence_speed,
 * tisp_ae_fps_calc_max_exp, tisp_ae_max_exp_calc_fps, tisp_ae_sort_H2L, tisp_ae_long_target,
 * tisp_ae_init_par_calc, tisp_ae_clac_deflicker_cfg.
 * info = stAeInfo[ch] block, cache = ae_cache[ch] block (arrays of per-channel pointers).
 */
#ifndef TX_ISP_T33_R13D_H
#define TX_ISP_T33_R13D_H

#include "tx_isp_t33_blk.h"

/* shared runtime (proven elsewhere): arguments are (shift, a, b[, c]) */
extern int fix_point_mult2_32(int shift, int a, int b);
extern int fix_point_mult3_32(int shift, int a, int b, int c);
extern int fix_point_div_32(int shift, int a, int b);
extern unsigned long long tisp_ae_lib_div_64(unsigned long long n, unsigned long long d);

static inline const u8 *t33_ae_tp(const u8 *info)
{
	return (const u8 *)(unsigned long)t33_le32(info + 1744);
}

static inline void t33_ae_w32(u8 *p, u32 off, u32 v)
{
	p[off] = (u8)v;
	p[off + 1] = (u8)(v >> 8);
	p[off + 2] = (u8)(v >> 16);
	p[off + 3] = (u8)(v >> 24);
}

static inline void t33_ae_w16(u8 *p, u32 off, u32 v)
{
	p[off] = (u8)v;
	p[off + 1] = (u8)(v >> 8);
}

static inline unsigned long long t33_ae_absdiff64(unsigned long long a, unsigned long long b)
{
	return a > b ? a - b : b - a;
}

/* tisp_ae_lib_bilinear_intp */
static inline u32 t33_ae_bilinear_intp(unsigned long long x, unsigned long long p, unsigned long long q,
				       u32 d, u32 e)
{
	unsigned long long den = t33_ae_absdiff64(p, q), num = t33_ae_absdiff64(x, p);
	u32 step = d > e ? d - e : e - d;
	u32 r = (u32)tisp_ae_lib_div_64((unsigned long long)step * num, den);

	return d > e ? d - r : d + r;
}

/* tisp_ae_calc_convergence_speed: p = two u32 limits, up / dn the two u16 speeds */
static inline u32 t33_ae_conv_speed(const u8 *p, u16 *up, u16 *dn, u32 a3, u32 shift, u32 v)
{
	u32 sh = shift & 0xffff, s = sh & 31, a = t33_le32(p), b = t33_le32(p + 4), cu = *up, cd = *dn, r;
	int big = (int)((a3 & 0xffff) << s), unit = (int)(128U << s);

	a = v < a + cu ? v - cu : a;
	b = v < b + cd ? v - cd : b;
	r = (u32)fix_point_div_32((int)sh, fix_point_mult3_32((int)sh, big, (int)a, (int)a), unit);
	*up = (u16)(cu + r);
	r = (u32)fix_point_div_32((int)sh, fix_point_mult3_32((int)sh, big, (int)b, (int)b), unit);
	r += cd;
	*dn = (u16)r;
	return r;
}

/* tisp_ae_fps_calc_max_exp */
static inline u32 t33_ae_fps_calc_max_exp(u32 ch, u32 fps, u8 *const *info, u8 *const *cache)
{
	const u8 *I = info[ch], *R = cache[ch], *T = t33_ae_tp(I);
	u32 sh = t32_tp_le16(T + 1736), s = sh & 31, base = t32_tp_le16(I + 2834);
	u32 used = base - t33_le32(R + 624), m, d;

	m = (u32)fix_point_mult2_32((int)sh, (int)(base << s), (int)t33_le32(R + 1260));
	d = (u32)fix_point_div_32((int)sh, (int)m, (int)(fps ? fps : 1));
	d >>= s;
	return d < used ? 0 : d - used;
}

/* tisp_ae_max_exp_calc_fps */
static inline u32 t33_ae_max_exp_calc_fps(u32 ch, u32 exp, u8 *const *info, u8 *const *cache)
{
	const u8 *I = info[ch], *R = cache[ch], *T = t33_ae_tp(I);
	u32 sh = t32_tp_le16(T + 1736), s = sh & 31, base = t32_tp_le16(I + 2834);
	u32 left = base - t33_le32(R + 624), m;

	m = (u32)fix_point_mult2_32((int)sh, (int)(base << s), (int)t33_le32(R + 1260));
	return (u32)fix_point_div_32((int)sh, (int)m, (int)((left + exp) << s));
}

/* tisp_ae_sort_H2L: absolute values, then descending bubble sort of v, w follows */
static inline void t33_ae_sort_h2l(int *v, int *w)
{
	int i, j, n, t;

	for (i = 0; i < 15; i++)
		if (v[i] < 0)
			v[i] = -v[i];
	for (n = 14; n > 0; n--)
		for (j = 0; j < n; j++)
			if (v[j] < v[j + 1]) {
				t = v[j];
				v[j] = v[j + 1];
				v[j + 1] = t;
				t = w[j];
				w[j] = w[j + 1];
				w[j + 1] = t;
			}
}

static inline unsigned long long t33_ae_tab64(const u8 *tab, u32 i, u32 s)
{
	return ((unsigned long long)t33_le32(tab + 8 * i + 4) << 32 | t33_le32(tab + 8 * i)) << s;
}

/* tisp_ae_long_target: tab = 15 u64 knots, y = 15 u16 values */
static inline u32 t33_ae_long_target(unsigned long long v, const u8 *tab, const u8 *y, u32 shift)
{
	u32 s = (shift & 0xffff) & 63, i;

	if (!(t33_ae_tab64(tab, 0, s) < v))
		return t32_tp_le16(y);
	if (!(v < t33_ae_tab64(tab, 14, s)))
		return t32_tp_le16(y + 28);
	for (i = 0; i < 14; i++)
		if (!(v < t33_ae_tab64(tab, i, s)) && !(t33_ae_tab64(tab, i + 1, s) < v))
			break;
	if (i == 14)
		i = 0;
	return t33_ae_bilinear_intp(v >> s, t33_ae_tab64(tab, i, 0), t33_ae_tab64(tab, i + 1, 0),
				    t32_tp_le16(y + 2 * i), t32_tp_le16(y + 2 * i + 2));
}

/* tisp_ae_init_par_calc */
static inline void t33_ae_init_par_calc(u32 ch, u8 *const *info, u8 *const *cache)
{
	u8 *I = info[ch];
	const u8 *R = cache[ch], *T = t33_ae_tp(I);
	u32 base = t32_tp_le16(I + 2834), sh = t32_tp_le16(T + 1736), s = sh & 31, rate = t33_le32(T + 1672);
	u32 rest = t33_le32(R + 1260), d, mx;

	d = (u32)fix_point_div_32((int)sh, (int)((rate >> 16) << s), (int)((rate & 0xffff) << s));
	mx = t33_ae_fps_calc_max_exp(ch, d, info, cache);
	t33_ae_w32(I, 2964, mx);
	t33_ae_w32(I, 2972, rest);
	t33_ae_w32(I, 2976, base - t33_le32(I + 2968));
}

/* tisp_ae_clac_deflicker_cfg: table of up to 120 u16 at info + 7672, count byte at info + 8146 */
static inline void t33_ae_clac_deflicker_cfg(u32 ch, u8 *const *info, u8 *const *cache)
{
	u8 *I = info[ch];
	const u8 *R = cache[ch], *T = t33_ae_tp(I);
	u32 sh = t32_tp_le16(T + 1736), s = sh & 31, per = (t33_le32(T + 1660) << 1) << s;
	u32 den = t33_le32(R + 1264), rest = t33_le32(R + 1260), base = t32_tp_le16(I + 2834) << s;
	u32 half, n, k, d, m;

	n = ((u32)fix_point_div_32((int)sh, (int)per, (int)(den ? den : 1)) >> s) & 0xff;
	if (n > 120)
		n = 120;
	else
		n = n ? n : 1;
	I[8146] = (u8)n;
	half = (u32)((int)(1U << s) / 2);
	for (k = 1; k <= I[8146]; k++) {
		d = (u32)fix_point_div_32((int)sh, (int)base, (int)per);
		m = (u32)fix_point_mult3_32((int)sh, (int)(k << s), (int)rest, (int)d);
		t33_ae_w16(I, 7672 + 2 * (k - 1), (m + half) >> s);
	}
	n = I[8146];
	for (k = n; k < 120; k++)
		t33_ae_w16(I, 7672 + 2 * k, t32_tp_le16(I + 7672 + 2 * (n - 1)));
	I[8146] = (u8)(n - 1);
}

#endif /* TX_ISP_T33_R13D_H */
