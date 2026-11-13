/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) LSF, module list 37 (0x25), registers 0x25040..0x25134: the
 * LSF part of tisp_lce_init (IQ bin section 15, the LCE section).
 *
 *  - tisp_rcfg_lsf_reg_init: the list layout (70 words, nine commands).
 *  - tisp_lce_malloc_cfg: the 0x2420-byte LSF buffer into slots 7 and 8
 *    (0x25068 / 0x2506c; the vendor stores slot 8 straight into the list).
 *  - tisp_lce_bin_para_update, the init's EV / BV interpolations of the u8
 *    tables and func_lce_para_update: the register fields from the section
 *    (after tisp_lce_size_para_calc, t33_lce_size), the 7 x 7 Gauss low pass
 *    (tisp_lce_gauss_lpf, ten Q12 weights of a quadrant) and four sets of 32
 *    Gauss weights (tisp_lce_gauss_wgt) packed four to a port word.
 *  - the mode with sec[0x2fb] = sec[0xc8] = 1 also stores the 0x4840-byte
 *    histogram buffer into slot 5 (the LCE list is then not built either,
 *    t33_lce_init).
 *
 * The vendor runs list 37 only from its LCE interrupt (tisp_lce_interrupt_static:
 * statistics part address into slot 4, slot 5 = histogram buffer, slot 0x45 = 1,
 * started when 0x25040 reads 0); its stream-on (tisp_rcfg_t33_write_all_reg)
 * does not start it.  func_lce_para_update also writes the four weight sets
 * back into the section (+0x117, +0x137, +0x1a3, +0x1c3); so does ours (s is
 * the driver's copy).  Checked against the vendor module's list 37 memory
 * after its init (QEMU, openimp #388).
 */
#ifndef TX_ISP_T33_LSF_H
#define TX_ISP_T33_LSF_H

#include "tx_isp_t33_lce.h"
#include "tx_isp_t32_lcetg.h"

#define T33_MOD_LSF		37
#define T33_LSF_BUF_BYTES	0x2420U		/* slots 7 / 8 */
#define T33_LSF_HIST_BYTES	0x4840U		/* slot 5 */
#define T33_LSF_SLOT_STAT	4
#define T33_LSF_SLOT_HIST	5
#define T33_LSF_SLOT_START	0x45

static const struct t33_tcmd t33_lsf_tmpl[] = {
	{ 0x25040, 5, 0 }, { 0x25068, 13, 0 }, { 0x250e0, 5, 0 }, { 0x25054, 5, 1 },
	{ 0x250a0, 8, 1 }, { 0x250c0, 8, 1 }, { 0x250f8, 8, 1 }, { 0x25118, 8, 1 },
	{ 0x25040, 1, 1 },
};

/* tisp_ev_intp_uint8_t / tisp_bv_intp_uint8_t: 11 u8 points over 11 LE u32 knots (bv: falling) */
static inline u32 t33_intp_u8(u32 x, const u8 *e, const u8 *t, int bv)
{
	u32 k, lo, hi, d, kv;
	unsigned long long f;

	for (k = 0; k < 11; k++) {
		kv = t33_le32(e + 4 * k);
		if (bv ? kv < x : x < kv)
			break;
	}
	if (k == 11)
		return t[10];
	if (!k)
		return t[0];
	hi = t33_le32(e + 4 * k);
	lo = t33_le32(e + 4 * k - 4);
	d = bv ? lo - hi : hi - lo;
	if (!d)
		return 2;
	f = (unsigned long long)(bv ? lo - x : x - lo) * 0x1000 + (d >> 1);
	return (u8)t33_sub_interp((u32)t32_tp_div64(f, (unsigned long long)d), t[k - 1], t[k]);
}

/*
 * tisp_lce_gauss_lpf: 7 x 7 kernel e^-(r^2 / sigma^2) (sigma at least 9),
 * normalised to 0x1000 (the centre takes the rounding rest); out gets the
 * ten weights of the upper left quadrant, centre first.
 */
static inline void t33_lce_gauss_lpf(u32 sigma, u32 out[10])
{
	u32 w[7][7];
	u16 q[7][7];
	u32 s2, tot = 0, r, c, u5 = 9;
	u16 sum = 0;
	int dy, dx;

	if (sigma < 9)
		sigma = 9;
	s2 = sigma * sigma;
	for (dy = -3; dy <= 3; dy++) {
		for (dx = -3; dx <= 3; dx++) {
			u32 r2 = (u32)(dx * dx + dy * dy) & 0xff;
			int e = (int)t32_calc_exp((r2 * 0x80000U + (s2 >> 1)) / s2, 15, 10, 8);
			u32 v = (u32)t32_lce_sdiv((u32)((e >> 1) + 0x100000), (u32)e);

			w[dy + 3][dx + 3] = v;
			tot += v;
		}
	}
	for (r = 0; r < 7; r++)
		for (c = 0; c < 7; c++) {
			q[r][c] = (u16)t32_tp_div64((unsigned long long)(long long)(int)w[r][c] << 12, (unsigned long long)(long long)(int)tot);
			sum = (u16)(sum + q[r][c]);
		}
	if (sum != 0x1000)
		q[3][3] = (u16)(q[3][3] + 0x1000 - sum);
	for (r = 0; r < 4; r++) {
		for (c = r; c < 4; c++)
			out[(r + u5 - c) & 0xff] = q[r][c];
		u5 = (u5 + r - 4) & 0xff;
	}
}

/* tisp_lce_gauss_wgt (T33: all 32 weights): a / e^((8 i - c)^2 / (2 b^2)) */
static inline void t33_lce_gauss_wgt(u32 a, u32 b, u32 c, u8 w[32])
{
	u32 s3 = (b * 2) * b;
	int i, d;

	for (i = 0; i < 32; i++) {
		u32 e;

		d = 8 * i - (int)c;
		e = t32_calc_exp(t32_lce_sdiv((u32)(d * d * 0x1000) + (u32)((int)s3 >> 1), s3), 10, 12, 12);
		w[i] = (u8)t32_lce_sdiv((u32)((int)e >> 1) + a * 0x1000, e);
	}
}

/* four weights into one port word, byte k from w[k] */
static inline u32 t33_lsf_pack4(const u8 *w)
{
	return (u32)w[0] | (u32)w[1] << 8 | (u32)w[2] << 16 | (u32)w[3] << 24;
}

/*
 * List 37 into l (capacity cap words).  s: the driver's copy of section 15
 * after t33_lce_size (the weight sets are written back into it), ev / bv the
 * exposure values (0 at init), buf the physical address of the LSF buffer,
 * hist the one of the histogram buffer (stored only in the LSF mode).
 * Returns the list length.
 */
static inline int t33_lsf_init(u8 *s, u32 ev, u32 bv, u32 buf, u32 hist, u32 *l, u32 cap)
{
	static const u16 tb[20] = { 0x1f, 0x2a, 0x35, 0x40, 0x4b, 0x56, 0x61, 0xb8, 0xcc, 0xda,
				    0xe5, 0xf0, 0xfb, 0x106, 0x158, 0x166, 0x171, 0x17c, 0x187, 0x192 };
	u32 v[20], lpf[10], k, x;
	u8 w[4][32];
	int len, isbv;

	len = t33_rcfg_tmpl_init(l, cap, t33_lsf_tmpl, ARRAY_SIZE(t33_lsf_tmpl));
	if (len <= 0)
		return len;
	/* tisp_lce_malloc_cfg */
	T33_SW(l, 7, buf);
	T33_SW(l, 8, buf);
	if (s[0x2fb] == 1 && s[0xc8] == 1)
		T33_SW(l, T33_LSF_SLOT_HIST, hist);
	/* the init's u8 interpolations (v[0] = sigma of the low pass, v[7] = 0x628, ...) */
	isbv = s[0x3d9] != 0;
	x = isbv ? bv : ev;
	for (k = 0; k < 20; k++)
		v[k] = t33_intp_u8(x, s + (isbv ? 0x328 : 0x2fc), s + tb[k], isbv);
	/* func_lce_para_update: the low pass and the four weight sets */
	t33_lce_gauss_lpf(v[0] & 0xff, lpf);
	t33_lce_gauss_wgt(s[0xac], s[0xad], s[0xae], w[0]);	/* 0x7cc */
	t33_lce_gauss_wgt(s[0xaf], s[0xb0], s[0xb1], w[1]);	/* 0x84c */
	t33_lce_gauss_wgt(s[0xb2], s[0xb3], s[0xb4], w[2]);	/* 0x68c */
	t33_lce_gauss_wgt(s[0xb5], s[0xb6], s[0xb7], w[3]);	/* 0x70c */
	memcpy(s + 0x1a3, w[0], 32);
	memcpy(s + 0x1c3, w[1], 32);
	memcpy(s + 0x117, w[2], 32);
	memcpy(s + 0x137, w[3], 32);
	for (k = 0; k < 5; k++)
		T33_SW(l, 0x1b + k, (lpf[2 * k + 1] & 0xfffU) << 16 | (lpf[2 * k] & 0xfffU));
	T33_SW(l, 9, (s[8] & 0xfU) << 8 | (s[9] & 0x1fU));
	T33_SW(l, 0xa, (u32)t32_tp_le16(s + 0x2f2) | (u32)t32_tp_le16(s + 0x2f4) << 16);
	T33_SW(l, 0xb, (u32)t32_tp_le16(s + 0x2f6) | (u32)t32_tp_le16(s + 0x2f8) << 16);
	T33_SW(l, 0xc, t33_le32(s + 0x2e4) & 0x7fffffU);
	T33_SW(l, 0xd, (u32)(s[5] != 4) << 16 | (s[0x2e8] & 0x7ffU));
	T33_SW(l, 0xe, (s[0xc8] & 1U) | v[7] << 24 | (s[0xc9] & 1U) << 4 | (s[0x2ca] & 0x3fU) << 8);
	T33_SW(l, 0xf, (v[8] & 0x1fU) << 8 | (s[0xcb] & 0x1fU) << 16 | (s[0xd7] & 1U));
	T33_SW(l, 0x10, (u32)s[0xd9] << 16 | s[0xd8]);
	T33_SW(l, 0x11, (v[9] & 0xffU) | v[12] << 24 | (v[10] & 0xffU) << 8 | (v[11] & 0xffU) << 16);
	T33_SW(l, 0x12, (u32)s[0x111] | v[13] << 24 | (u32)s[0x112] << 8 | (u32)s[0x113] << 16);
	T33_SW(l, 0x13, (s[0x115] & 1U) << 8 | (s[0x116] & 1U) << 16 | (s[0x114] & 1U));
	for (k = 0; k < 8; k++) {
		T33_SW(l, 0x21 + k, t33_lsf_pack4(w[2] + 4 * k));
		T33_SW(l, 0x2a + k, t33_lsf_pack4(w[3] + 4 * k));
		T33_SW(l, 0x33 + k, t33_lsf_pack4(w[0] + 4 * k));
		T33_SW(l, 0x3c + k, t33_lsf_pack4(w[1] + 4 * k));
	}
	T33_SW(l, 0x15, (v[14] & 0x1fU) << 8 | (s[0x157] & 0x1fU) << 16 | (s[0x163] & 1U));
	T33_SW(l, 0x16, (u32)s[0x165] << 16 | s[0x164]);
	T33_SW(l, 0x17, (v[15] & 0xffU) | v[18] << 24 | (v[16] & 0xffU) << 8 | (v[17] & 0xffU) << 16);
	T33_SW(l, 0x18, (u32)s[0x19d] | v[19] << 24 | (u32)s[0x19e] << 8 | (u32)s[0x19f] << 16);
	T33_SW(l, 0x19, (s[0x1a1] & 1U) << 8 | (s[0x1a2] & 1U) << 16 | (s[0x1a0] & 1U));
	return len;
}

#endif /* TX_ISP_T33_LSF_H */
