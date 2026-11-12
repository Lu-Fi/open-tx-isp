/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) colour correction (CCM), module list 10, IQ bin section 12
 * (0x760 bytes) and its payload (sec[0x75e] knots of 10 s32: colour
 * temperature, 3x3 matrix): tisp_ccm_init -> tisp_ccm_interp_by_ct,
 * tisp_ccm_interp_by_ev, tisp_ccm_no_interp, tisp_ccm_calc_para_refresh
 * (func_ccm_calc_single_ks: arguments from the disassembly, |d out| and
 * |d in| as 64 bit), tisp_ccm_write_reg(0) slot stores.  Block state as
 * the vendor's word array (0x290 bytes, ccm_sp / ccm_ip / ccm_ap defaults).
 *
 * Section: +4/+8/+0x68 enables, +0xc 12 EV points (EV >> 20), +0x3c EV
 * hysteresis, +0x40 manual matrix flag with the matrix at +0x44, then
 * 12-point EV tables (stride 0x30) at +0x6c (12 tables), +0x2b0, +0x434,
 * +0x5b8 (8 each) behind the enables at +0x2ac/+0x430/+0x5b4, +0x738 CT
 * hysteresis, +0x73a u16 per-knot hysteresis, +0x75d copy flag.
 * Init: CT 5000 K (ccm_ip), EV 0 as the T32 init (the PRJ008 EV tables
 * are flat, so the EV does not show in the trace).  QEMU trace of the
 * vendor module (openimp #388): list 10 word-equal.
 */
#ifndef TX_ISP_T33_CCM_H
#define TX_ISP_T33_CCM_H

#include "tx_isp_t33_blk.h"

#define T33_MOD_CCM		10
#define T33_SEC_CCM		12
#define T33_CCM_SEC_BYTES	0x760U
#define T33_CCM_WORDS		0xa4U
#define T33_CCM_INIT_CT		5000U
#define T33_CCM_INIT_EV		0U

static const struct t33_tcmd t33_ccm_tmpl[] = {
	{ 0x09010, 1, 0 }, { 0x09040, 9, 0 }, { 0x09084, 1, 0 }, { 0x0907c, 5, 1 }, { 0x09088, 12, 1 },
	{ 0x090b4, 3, 1 },
};

static inline int t33_s32(const u8 *p)
{
	return (int)t33_le32(p);
}

/* tisp_ccm_interp_by_ct (init: force) into v[0x5f..0x70] */
static inline void t33_ccm_ct(u32 *v, const u8 *sec, const u8 *lut, u32 ct)
{
	u32 n = sec[0x75e], st = 0, i, c, h0, h1, lo;
	const u8 *src = NULL;
	int k;

#define KCT(i)	t33_le32(lut + 40 * (i))
#define HY(i)	t32_tp_le16(sec + 0x73a + 2 * (i))
	if ((int)(HY(0) + KCT(0)) < (int)ct) {
		if ((int)(KCT(n - 1) - HY(n - 1)) <= (int)ct) {
			st = (n - 1) * 2;
		} else {
			st = n;
			for (i = 1; i < n; i++) {
				if ((int)ct < (int)(KCT(i) - HY(i))) {
					if ((int)(HY(i - 1) + KCT(i - 1)) < (int)ct) {
						st = (i - 1) * 2 + 1;
						break;
					}
				} else if ((int)ct <= (int)(KCT(i) + HY(i))) {
					st = i * 2;
					break;
				}
			}
		}
	}
	v[4] = ct;
	v[5] = st;
	if (t33_le32(sec + 0x40)) {
		src = sec + 0x44;
	} else if (n == 1 || ct <= KCT(0)) {
		src = lut + 4;
	} else if (ct >= KCT(n - 1)) {
		src = lut + 40 * (n - 1) + 4;
	} else {
		for (i = 1; i < n; i++) {
			c = KCT(i);
			if (c < ct)
				continue;
			lo = KCT(i - 1);
			h0 = HY(i - 1);
			h1 = HY(i);
			if (lo + h0 < ct && ct < c - h1) {
				int den = (int)(c - lo - h1 - h0), num = (int)(ct - lo - h0);

				for (k = 0; k < 9; k++) {
					int s = (den - num) * t33_s32(lut + 40 * (i - 1) + 4 + 4 * k) +
						num * t33_s32(lut + 40 * i + 4 + 4 * k);
					int r = s < 1 ? -(den >> 1) : den >> 1;

					s = (s + r) / den;
					v[0x5f + k] = (u32)(s < -0x2000 ? -0x2000 : s > 0x1fff ? 0x1fff : s);
				}
			} else {
				src = lut + 40 * (lo + h0 < ct ? i : i - 1) + 4;
			}
			break;
		}
	}
#undef KCT
#undef HY
	if (src)
		for (k = 0; k < 9; k++)
			v[0x5f + k] = t33_le32(src + 4 * k);
	for (k = 0; k < 9; k++) {
		v[0x54 + k] = v[0x5f + k];
		if (sec[0x75d] == 1)
			v[0x9a + k] = v[0x5f + k];
		v[0x68 + k] = v[0x5f + k];
	}
}

/* tisp_ccm_interp_by_ev (init: force): the 36 EV tables into v[0x72..0x98] */
static inline void t33_ccm_ev(u32 *v, const u8 *sec, u32 ev_lo, u32 ev_hi)
{
	static const u16 base[4] = { 0x6c, 0x2b0, 0x434, 0x5b8 };
	static const u8 cnt[4] = { 12, 8, 8, 8 }, word[4] = { 0x72, 0x7f, 0x88, 0x91 };
	u32 e = ev_hi << 22 | ev_lo >> 10, t0, t1, f = 0;
	unsigned int i, g, k, idx = 12;

	v[2] = ev_lo;
	v[3] = ev_hi;
	for (i = 0; i < 12; i++)
		if (e < t33_le32(sec + 0xc + 4 * i)) {
			idx = i;
			break;
		}
	if (idx && idx < 12) {
		t0 = t33_le32(sec + 0xc + 4 * idx - 4);
		t1 = t33_le32(sec + 0xc + 4 * idx);
		if (t1 == t0)
			return;		/* the vendor returns 2 */
		f = (u32)t32_tp_div64((unsigned long long)(e - t0) * 0x1000 + ((int)(t1 - t0) >> 1),
				      (unsigned long long)(t1 - t0));
	}
	for (g = 0; g < 4; g++)
		for (k = 0; k < cnt[g]; k++) {
			const u8 *p = sec + base[g] + 0x30 * k;

			if (idx == 12)
				v[word[g] + k] = t33_le32(p + 0x2c);
			else if (!idx)
				v[word[g] + k] = t33_le32(p);
			else
				v[word[g] + k] = (u32)t33_sub_interp(f, t33_s32(p + 4 * idx - 4), t33_s32(p + 4 * idx));
		}
}

/* func_ccm_calc_single_ks: the largest shift 10..0 with ((a << s) + b / 2) / b < 0x100 */
static inline void t33_ccm_ks(u32 a, u32 b, u32 *val, u32 *shift)
{
	unsigned long long q;
	int s;

	for (s = 10; s >= 0; s--) {
		q = t32_tp_div64(((unsigned long long)a << s) + (b >> 1), b);
		if (q < 0x100) {
			*val = (u32)q;
			*shift = (u32)s;
			return;
		}
	}
	*val = 0xff;
	*shift = 0;
}

/* tisp_ccm_no_interp + tisp_ccm_calc_para_refresh */
static inline void t33_ccm_para(u32 *v, const u8 *sec)
{
	/* { first in, first out, value word, shift word } of the three slope sets */
	static const u8 ks[3][4] = { { 0x28, 0x2c, 0x30, 0x33 }, { 0x37, 0x3b, 0x3f, 0x42 }, { 0x46, 0x4a, 0x4e, 0x51 } };
	unsigned int k, j;

	v[0x5d] = t33_le32(sec + 4);
	v[0x5e] = t33_le32(sec + 8);
	v[0x71] = t33_le32(sec + 0x68);
	v[0x7e] = t33_le32(sec + 0x2ac);
	v[0x87] = t33_le32(sec + 0x430);
	v[0x90] = t33_le32(sec + 0x5b4);

	v[6] = v[0x5d];
	v[7] = v[0x5e];
	for (k = 0; k < 9; k++)
		v[0x11 + k] = v[0x68 + k];
	v[0x1a] = v[0x71];
	v[0x27] = v[0x7e];
	v[0x36] = v[0x87];
	v[0x45] = v[0x90];
	for (k = 0; k < 12; k++)
		v[0x1b + k] = v[0x72 + k];
	for (k = 0; k < 8; k++) {
		v[0x28 + k] = v[0x7f + k];
		v[0x37 + k] = v[0x88 + k];
		v[0x46 + k] = v[0x91 + k];
	}
	for (j = 0; j < 3; j++)
		for (k = 0; k < 3; k++) {
			int din = (int)(v[ks[j][0] + k + 1] - v[ks[j][0] + k]);
			int dout = (int)(v[ks[j][1] + k + 1] - v[ks[j][1] + k]);

			if (din)	/* else the vendor prints "ccm error" and keeps the old values */
				t33_ccm_ks((u32)t33_iabs(dout), (u32)t33_iabs(din), &v[ks[j][2] + k], &v[ks[j][3] + k]);
		}
}

#define T33_CCM_P10(a, b)	((v[a] & 0x3ff) << 4 | (v[b] & 0x3ff) << 16)
#define T33_CCM_B3(a, b, c)	((v[a] & 0xff) << 8 | (v[b] & 0xff) << 16 | (v[c] & 0xff))
#define T33_CCM_S3(a, b, c)	((v[a] & 0xf) << 4 | (v[b] & 0xf) << 8 | (v[c] & 0xf))

/* tisp_ccm_write_reg(0): list stores (slots of t33_ccm_tmpl) */
static inline void t33_ccm_store(const u32 *v, u32 *l)
{
	unsigned int k;

	for (k = 0; k < 4; k++)
		T33_SW(l, 3 + k, (v[0x12 + 2 * k] & 0x3fff) << 16 | (v[0x11 + 2 * k] & 0x3fff));
	T33_SW(l, 7, v[0x19] & 0x3fff);
	for (k = 0; k < 3; k++)
		T33_SW(l, 9 + k, (v[0x1c + 2 * k] & 0x3fff) << 16 | (v[0x1b + 2 * k] & 0x3fff));
	T33_SW(l, 0xf, T33_CCM_B3(0x31, 0x32, 0x30));
	T33_SW(l, 0x10, T33_CCM_B3(0x4f, 0x50, 0x4e));
	T33_SW(l, 0x11, T33_CCM_B3(0x40, 0x41, 0x3f));
	T33_SW(l, 0x12, T33_CCM_B3(0x22, 0x23, 0x21));
	T33_SW(l, 0x13, T33_CCM_B3(0x25, 0x26, 0x24));
	T33_SW(l, 0xd, (v[0x45] & 1) << 4 | (v[0x27] & 1) << 8 | (v[0x36] & 1));
	T33_SW(l, 0x15, T33_CCM_P10(0x39, 0x3a));
	T33_SW(l, 0x16, T33_CCM_P10(0x3b, 0x3c));
	T33_SW(l, 0x17, T33_CCM_P10(0x3d, 0x3e));
	T33_SW(l, 0x18, T33_CCM_P10(0x37, 0x38));
	T33_SW(l, 0x19, T33_CCM_P10(0x46, 0x47));
	T33_SW(l, 0x1a, T33_CCM_P10(0x48, 0x49));
	T33_SW(l, 0x1b, T33_CCM_P10(0x4a, 0x4b));
	T33_SW(l, 0x1c, T33_CCM_P10(0x4c, 0x4d));
	T33_SW(l, 0x1d, T33_CCM_P10(0x2a, 0x2b));
	T33_SW(l, 0x1e, T33_CCM_P10(0x2c, 0x2d));
	T33_SW(l, 0x1f, T33_CCM_P10(0x2e, 0x2f));
	T33_SW(l, 0x20, T33_CCM_P10(0x28, 0x29));
	T33_SW(l, 0x22, T33_CCM_S3(0x52, 0x53, 0x51));
	T33_SW(l, 0x23, T33_CCM_S3(0x34, 0x35, 0x33));
	T33_SW(l, 0x24, T33_CCM_S3(0x43, 0x44, 0x42));
	T33_SW(l, 8, (v[6] & 1) << 16 | (v[7] & 1) << 20 | (v[0x1a] & 1) << 24);
	T33_SW(l, 1, 1);
}

#undef T33_CCM_P10
#undef T33_CCM_B3
#undef T33_CCM_S3

/*
 * Block init into list l (capacity cap words); sec: section 12, lut: its
 * payload (lutbytes); v: T33_CCM_WORDS words of block state.  Returns the
 * list length, -1 when the payload does not hold the knots.
 */
static inline int t33_ccm_init(const u8 *sec, const u8 *lut, u32 lutbytes, u32 ct, u32 ev_lo, u32 ev_hi,
			       u32 *v, u32 *l, u32 cap)
{
	int len;
	unsigned int k;

	if (!sec[0x75e] || lutbytes < sec[0x75e] * 40U)
		return -1;
	len = t33_rcfg_tmpl_init(l, cap, t33_ccm_tmpl, ARRAY_SIZE(t33_ccm_tmpl));
	for (k = 0; k < T33_CCM_WORDS; k++)
		v[k] = 0;
	/* ccm_sp (identity at 0x150), ccm_ip (8), ccm_ap (0x264) */
	v[0x54] = v[0x58] = v[0x5c] = 0x400;
	v[4] = 5000;
	v[5] = 0xff;
	v[0x9a] = v[0x9e] = v[0xa2] = 0x400;
	v[2] = ev_lo >> 10 | ev_hi << 22;
	v[3] = ev_hi >> 10;
	t33_ccm_ct(v, sec, lut, ct);
	t33_ccm_ev(v, sec, v[2], v[3]);
	t33_ccm_para(v, sec);
	if (len > 0)
		t33_ccm_store(v, l);
	return len;
}

#endif /* TX_ISP_T33_CCM_H */
