/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) BCSH (brightness / contrast / saturation / hue), module
 * list 18, IQ bin section 19 (0x4c4 bytes, version 0x01350181) and its
 * payload (sec[0x4bf] knots of 10 s32: colour temperature, 3x3 matrix):
 * tisp_bcsh_init -> get_new_para, interp_by_ev, interp_by_ct,
 * calc_para_refresh(0), tisp_params_transform and the slot stores.
 *
 * The math is the T32 block's (tx_isp_t32_bcsh.h); the T33 deltas:
 *  - section: T32 offsets below 0x8c unchanged, from 0x8c on +0x10 (a u16
 *    per-knot CT hysteresis inserted at 0x8c; 0xac the CT threshold);
 *  - interp_by_ct: matrix mode sec[0x4a2] (0 identity, 1 knots with the
 *    per-knot hysteresis, 2 sec+0x68);
 *  - get_new_para: a section with sec[0x118] <= sec[0x116] gets 0 / 1 and
 *    no table; a pair with hi <= lo takes sec[0x116] / sec[0x118];
 *  - the EV interpolation in general (the init EV is the tisp init EV,
 *    shifted >> 10 twice as the vendor does);
 *  - registers: tisp_params_transform permutes the state into a u16 block
 *    (bcshInfo, 0xb2 bytes) and tisp_bcsh_init stores it into the layout
 *    of tisp_rcfg_bcsh_reg_init (0xf098 / 0xf040 / 0xf058 / 0xf064 ports).
 * The state is a byte copy of the vendor's bcsh_para (0x290 bytes): cp at
 * 0x30, ap (user + csc set) at 0x1cc, the get_new_para table at 0x25c.
 * QEMU trace of the vendor module (openimp #388): list 18 word-equal.
 */
#ifndef TX_ISP_T33_BCSH_H
#define TX_ISP_T33_BCSH_H

#include "tx_isp_t33_blk.h"
#include "tx_isp_t32_bcsh.h"

#define T33_MOD_BCSH		18
#define T33_SEC_BCSH		19
#define T33_BCSH_SEC_BYTES	0x4c4U
#define T33_BCSH_ID		0x01350181U
#define T33_BCSH_ST_BYTES	0x290U
#define T33_BCSH_INFO_BYTES	0xb2U

#ifdef __KERNEL__
#define T33_BCSH_FN	static noinline_for_stack __maybe_unused int
#else
#define T33_BCSH_FN	static inline int
#endif

static const struct t33_tcmd t33_bcsh_tmpl[] = {
	{ 0x0f07c, 7, 0 }, { 0x0f0cc, 13, 0 }, { 0x0f040, 6, 1 }, { 0x0f058, 3, 1 }, { 0x0f064, 5, 1 },
	{ 0x0f098, 13, 1 },
};

/* tisp_params_transform: { info byte offset, byte, from section, source offset } */
struct t33_bcsh_tf {
	u8 dst;
	u8 byte;
	u8 sec;
	u16 src;
};

static const struct t33_bcsh_tf t33_bcsh_tf[] = {
	{ 0x2c, 0, 0, 0x030 }, { 0x3a, 0, 0, 0x032 }, { 0x0e, 0, 0, 0x056 }, { 0x5c, 0, 0, 0x058 },
	{ 0x1e, 0, 0, 0x0ae }, { 0x96, 0, 0, 0x0b0 }, { 0x2e, 0, 0, 0x0b2 }, { 0x64, 0, 0, 0x0b4 },
	{ 0x2a, 0, 0, 0x0b6 }, { 0xa8, 0, 0, 0x0b8 }, { 0x14, 0, 0, 0x0ba }, { 0x7c, 0, 0, 0x0bc },
	{ 0x7e, 0, 0, 0x0be }, { 0x54, 0, 0, 0x0c0 }, { 0x04, 0, 0, 0x0c2 }, { 0x06, 0, 0, 0x0c4 },
	{ 0x0c, 0, 0, 0x0c6 }, { 0x9c, 0, 0, 0x0c8 }, { 0x22, 0, 0, 0x0ca }, { 0x58, 0, 0, 0x0cc },
	{ 0xa2, 0, 0, 0x0ce }, { 0x12, 0, 0, 0x0d0 }, { 0x42, 0, 0, 0x05e }, { 0xa0, 0, 0, 0x060 },
	{ 0x38, 0, 0, 0x06e }, { 0x8e, 0, 0, 0x06c }, { 0x46, 0, 0, 0x072 }, { 0x8c, 0, 0, 0x070 },
	{ 0x26, 0, 0, 0x038 }, { 0x4e, 0, 0, 0x03a }, { 0x9e, 0, 0, 0x03c }, { 0x52, 0, 0, 0x03e },
	{ 0x48, 0, 0, 0x040 }, { 0xac, 1, 1, 0x475 }, { 0x00, 0, 0, 0x042 }, { 0x20, 0, 0, 0x044 },
	{ 0x90, 0, 0, 0x046 }, { 0x50, 0, 0, 0x048 }, { 0x08, 0, 0, 0x04a }, { 0xaf, 1, 0, 0x0e2 },
	{ 0xae, 1, 0, 0x0e3 }, { 0x72, 0, 0, 0x0e4 }, { 0x28, 0, 0, 0x14a }, { 0x16, 0, 0, 0x144 },
	{ 0x3c, 0, 0, 0x14c }, { 0xa4, 0, 0, 0x146 }, { 0x36, 0, 0, 0x14e }, { 0x40, 0, 0, 0x148 },
	{ 0x74, 0, 0, 0x156 }, { 0x0a, 0, 0, 0x150 }, { 0x66, 0, 0, 0x158 }, { 0x84, 0, 0, 0x152 },
	{ 0x60, 0, 0, 0x15a }, { 0x1c, 0, 0, 0x154 }, { 0x4a, 0, 0, 0x138 }, { 0x32, 0, 0, 0x13e },
	{ 0x44, 0, 0, 0x13a }, { 0x6a, 0, 0, 0x140 }, { 0x1a, 0, 0, 0x13c }, { 0x94, 0, 0, 0x142 },
	{ 0x7a, 0, 0, 0x10c }, { 0x82, 0, 0, 0x110 }, { 0x18, 0, 0, 0x114 }, { 0x30, 0, 0, 0x118 },
	{ 0x4c, 0, 0, 0x11c }, { 0x62, 0, 0, 0x120 }, { 0x80, 0, 0, 0x124 }, { 0x24, 0, 0, 0x128 },
	{ 0x5a, 0, 0, 0x12c }, { 0xad, 1, 1, 0x46a }, { 0x56, 0, 0, 0x034 }, { 0xaa, 1, 1, 0x46b },
	{ 0x68, 0, 0, 0x036 }, { 0x3e, 0, 1, 0x0de }, { 0x02, 0, 1, 0x0e0 }, { 0xb1, 1, 0, 0x054 },
	{ 0xa6, 0, 0, 0x05a }, { 0xab, 1, 0, 0x078 }, { 0x92, 0, 0, 0x0d2 }, { 0x86, 0, 0, 0x0d4 },
	{ 0x98, 0, 0, 0x0d6 }, { 0x76, 0, 0, 0x0d8 }, { 0x10, 0, 0, 0x0da }, { 0x78, 0, 0, 0x0dc },
	{ 0x9a, 0, 0, 0x0de }, { 0x88, 0, 0, 0x0e0 }, { 0xb0, 1, 0, 0x05c }, { 0x34, 0, 0, 0x074 },
	{ 0x8a, 0, 0, 0x076 }, { 0x5e, 0, 0, 0x04c }, { 0x6e, 0, 0, 0x04e }, { 0x70, 0, 0, 0x050 },
	{ 0x6c, 0, 0, 0x052 },
};

/* bcsh_ap: the vendor's defaults of the user / csc set (tisp_csc_get overwrites 0x2c..0x86) */
static const u8 t33_bcsh_ap_def[0x90] = {
	0x80, 0x80, 0x80, 0x80, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x8b, 0x4c, 0x00, 0x00, 0x46, 0x96, 0x00, 0x00, 0x2f, 0x1d, 0x00, 0x00,
	0xce, 0xd4, 0xff, 0xff, 0x32, 0xab, 0xff, 0xff, 0x00, 0x80, 0x00, 0x00,
	0x00, 0x80, 0x00, 0x00, 0xd1, 0x94, 0xff, 0xff, 0x2f, 0xeb, 0xff, 0xff,
	0x00, 0x80, 0x00, 0xff, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
	0x00, 0x00, 0x00, 0x00, 0xe9, 0x66, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00,
	0xe7, 0xa7, 0xff, 0xff, 0x2e, 0x49, 0xff, 0xff, 0x00, 0x00, 0x01, 0x00,
	0xa2, 0xc5, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0xff,
	0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/* tisp_bcsh_get_new_para into the table t (12 lo, 12 hi u16) */
static inline void t33_bcsh_new_para(u8 *sec, u8 *t)
{
	u32 a = t32_tp_le16(sec + 0x116), b = t32_tp_le16(sec + 0x118), lo, hi, k;

	if (b <= a) {
		t32_bcsh_p16(sec, 0x116, 0);
		t32_bcsh_p16(sec, 0x118, 1);
		return;
	}
	for (k = 0; k < 12; k++) {
		const u8 *p = sec + 0x272 + 2 * k;

		lo = ((t32_tp_le16(p + 0x18) & 0x7fff) >> 10) * 0x20 + ((t32_tp_le16(p) & 0x7fff) >> 10);
		hi = ((t32_tp_le16(p + 0xf0) & 0x7fff) >> 10) * 0x20 + ((t32_tp_le16(p + 0xd8) & 0x7fff) >> 10);
		if (hi <= lo) {
			lo = a;
			hi = b;
		}
		t32_bcsh_p16(t, 2 * k, lo);
		t32_bcsh_p16(t, 2 * k + 0x18, hi);
	}
}

/* tisp_bcsh_interp_by_ev (forced), ev = the state's EV (init EV >> 10) */
static inline void t33_bcsh_ev(u8 *st, const u8 *sec, unsigned long long ev)
{
	u8 *cp = st + 0x30, *ap = st + 0x1cc, *p5 = st + 0x25c;
	u32 x = (u32)(ev >> 10), f = 0, k, a, b;
	int lo = 11, hi = 11, d;

	if (sec[0x4c0])
		x = (u32)ev;	/* BV mode: falling thresholds at 0x34 */
	for (k = 0; k < 12; k++) {
		u32 t = t33_le32(sec + (sec[0x4c0] ? 0x34 : 4) + 4 * k);

		if (sec[0x4c0] ? t < x : x < t) {
			if (!k) {
				lo = hi = 0;
			} else {
				u32 tp = t33_le32(sec + (sec[0x4c0] ? 0x34 : 4) + 4 * (k - 1));

				lo = (int)k - 1;
				hi = (int)k;
				d = sec[0x4c0] ? (int)(tp - t) : (int)(t - tp);
				if (d > 0) {
					u32 n = sec[0x4c0] ? tp - x : x - tp;

					f = (u32)t32_tp_div64((unsigned long long)n * 0x1000 + (u32)(d >> 1),
							      (unsigned long long)d);
				}
			}
			break;
		}
	}
	a = 2 * (u32)lo;
	b = 2 * (u32)hi;
#define SIS(dst, o)	t32_bcsh_p16(cp, 2 * (dst), t33_sub_interp(f, t32_bcsh_s16(sec, a + (o)), t32_bcsh_s16(sec, b + (o))))
#define SIM(dst, o)	t32_bcsh_p16(cp, 2 * (dst), t33_sub_interp(f, t32_tp_le16(sec + a + (o)) & 0x3ff, \
								   t32_tp_le16(sec + b + (o)) & 0x3ff))
#define SIU(dst, o)	t32_bcsh_p16(cp, 2 * (dst), t33_sub_interp(f, t32_tp_le16(sec + a + (o)), t32_tp_le16(sec + b + (o))))
#define SIB(o)		((u8)t33_sub_interp(f, sec[lo + (o)], sec[hi + (o)]))
	SIS(0x81, 0x422);
	SIS(0x82, 0x43a);
	SIS(0x83, 0x452);
	SIM(0x25, 0x272);
	SIM(0x26, 0x28a);
	for (k = 0; k < 7; k++)
		SIU(0x27 + k, 0x2a2 + 0x18 * k);
	SIM(0x2e, 0x34a);
	SIM(0x2f, 0x362);
	for (k = 0; k < 7; k++)
		SIU(0x30 + k, 0x37a + 0x18 * k);
	switch (sec[0x46c]) {
	case 0:
		cp[0x24] = 0;
		break;
	case 1:
		cp[0x24] = 1;
		SIU(0x13, 0xe6);
		SIU(0x14, 0xfe);
		break;
	case 2:
		cp[0x24] = 1;
		t32_bcsh_p16(cp, 0x26, t32_tp_le16(sec + 0xe2));
		t32_bcsh_p16(cp, 0x28, t32_tp_le16(sec + 0xe4));
		break;
	}
	if (sec[0x46a] == 1) {
		SIU(0, 0xae);
		SIU(1, 0xc6);
	}
	for (k = 0; k < 10; k++)
		SIU(4 + k, 0x182 + 0x18 * k);
	switch (sec[0x46d]) {
	case 0:
		for (k = 0x19; k <= 0x1c; k++)
			t32_bcsh_p16(cp, 2 * k, 0x400);
		break;
	case 1:
		t32_bcsh_p16(cp, 2 * 0x17, t33_sub_interp(f, t32_tp_le16(p5 + a), t32_tp_le16(p5 + b)));
		t32_bcsh_p16(cp, 2 * 0x18, t33_sub_interp(f, t32_tp_le16(p5 + a + 0x18), t32_tp_le16(p5 + b + 0x18)));
		SIU(0x19, 0x122);
		SIU(0x1a, 0x13a);
		SIU(0x1b, 0x152);
		SIU(0x1c, 0x16a);
		break;
	case 2:
		for (k = 0; k < 6; k++)
			t32_bcsh_p16(cp, 2 * (0x17 + k), t32_tp_le16(sec + 0x116 + 2 * k));
		break;
	}
	if (sec[0x471] == 1 || (sec[0x471] == 0 && sec[0x472] != 2)) {
		if (sec[0x46d] <= 2)
			cp[0x2c] = sec[0x46d] ? 1 : 0;
		cp[0x3a] = sec[0x46e];
	} else {
		cp[0x2c] = 1;
		cp[0x3a] = ap[2];
	}
	if (sec[0x477] == 1 || (sec[0x477] == 0 && sec[0x478] != 2)) {
		switch (sec[0x476]) {
		case 0:
			cp[0x48] = 0;
			cp[0x49] = 0x80;
			break;
		case 1:
			cp[0x48] = 1;
			cp[0x49] = SIB(0x47c);
			break;
		case 2:
			cp[0x48] = 1;
			cp[0x49] = sec[0x47b];
			break;
		}
	} else {
		cp[0x48] = 1;
		cp[0x49] = ap[1];
	}
	cp[0xb2] = SIB(0x48a);
	cp[0xb3] = SIB(0x496);
	if (sec[0x4ae] == 1 || (sec[0x4ae] == 0 && sec[0x4af] != 2)) {
		switch (sec[0x4ad]) {
		case 0:
			cp[0x100] = 0x80;
			break;
		case 1:
			cp[0x100] = SIB(0x4b3);
			break;
		case 2:
			cp[0x100] = sec[0x4b2];
			break;
		}
	} else {
		cp[0x100] = ap[0];
	}
#undef SIS
#undef SIM
#undef SIU
#undef SIB
}

/* tisp_bcsh_interp_by_ct (forced) into cp + 0xb8; lut: the knots (payload) */
static inline void t33_bcsh_ct(u8 *st, const u8 *sec, const u8 *lut, int ct)
{
	u8 *cp = st + 0x30;
	int n = sec[0x4bf], i, k, m[9];
	const u8 *src = NULL;

#define KCT(i)	t33_s32(lut + 40 * (i))
#define HY(i)	((int)t32_tp_le16(sec + 0x8c + 2 * (i)))
	t32_bcsh_p32(st, 0x20, (u32)ct);
	switch (sec[0x4a2]) {
	case 0:
		for (i = 0; i < 9; i++)
			m[i] = (i % 4) ? 0 : 0x400;
		break;
	case 1:
		if (n == 1 || ct <= KCT(0)) {
			src = lut + 4;
		} else if (ct >= KCT(n - 1)) {
			src = lut + 40 * (n - 1) + 4;
		} else {
			for (i = 1; i < n; i++) {
				if (ct > KCT(i))
					continue;
				if (ct <= KCT(i - 1) + HY(i - 1)) {
					src = lut + 40 * (i - 1) + 4;
				} else if (ct < KCT(i) - HY(i)) {
					int d = KCT(i) - KCT(i - 1) - HY(i) - HY(i - 1), nn = ct - KCT(i - 1) - HY(i - 1);

					for (k = 0; k < 9; k++) {
						int s = (d - nn) * t33_s32(lut + 40 * (i - 1) + 4 + 4 * k) +
							nn * t33_s32(lut + 40 * i + 4 + 4 * k);
						int r = s < 1 ? -(d >> 1) : d >> 1;

						m[k] = t32_bcsh_clip((s + r) / d, -0x2000, 0x1fff);
					}
				} else {
					src = lut + 40 * i + 4;
				}
				break;
			}
			if (i == n)
				return;
		}
		break;
	case 2:
		src = sec + 0x68;
		break;
	default:
		return;
	}
#undef KCT
#undef HY
	if (src)
		for (k = 0; k < 9; k++)
			m[k] = t33_s32(src + 4 * k);
	for (k = 0; k < 9; k++) {
		t32_bcsh_p32(cp, 0xb8 + 4 * k, (u32)m[k]);
		t32_bcsh_p32(st, 0x15c + 4 * k, (u32)m[k]);
		if (sec[0x4a6] == 1)
			t32_bcsh_p32(st, 0x1d4 + 4 * k, (u32)m[k]);
	}
}

/* tisp_bcsh_calc_para_refresh(0) */
static inline void t33_bcsh_calc(u8 *st, const u8 *sec)
{
	u8 *cp = st + 0x30, *ap = st + 0x1cc;
	int m[9], m1[9], m2[9], mat[9], hm[9], in[3], o[3], d, v, p, i;
	u32 x[9], y[9], ox[9], oy[9], u7, hue;

	for (i = 0; i < 9; i++)
		m[i] = t32_bcsh_s32(ap, 0x30 + 4 * i);
	for (i = 0; i < 3; i++)
		in[i] = t32_bcsh_s16(cp, 2 * (0x81 + i));
	for (i = 0; i < 3; i++)
		o[i] = (int)t32_bcsh_rnd((long long)in[0] * m[3 * i] + (long long)in[1] * m[3 * i + 1] +
					 (long long)in[2] * m[3 * i + 2], 16);
	t32_bcsh_p16(cp, 2 * 0x87, o[0] + ap[0x54] * 4 + ((int)cp[0x100] - 0x80) * 8);
	t32_bcsh_p16(cp, 2 * 0x88, o[1]);
	t32_bcsh_p16(cp, 2 * 0x89, o[2]);
	d = (int)t32_tp_le16(cp + 0x28) - (int)t32_tp_le16(cp + 0x26);
	t32_bcsh_p16(cp, 2 * 0x15, t32_bcsh_clip(d > 0 ? ((d >> 1) + 0x1000) / d : 0, 0, 0x3ff));
	p = sec[0x476] ? 0x2e : 0x25;
	for (i = 0; i < 9; i++) {
		x[i] = t32_tp_le16(cp + 2 * (0x25 + i));
		y[i] = t32_tp_le16(cp + 2 * (p + i));
		ox[i] = oy[i] = 0;
	}
	t32_bcsh_contrast(cp[0x49], x, y, ox, oy);
	for (i = 0; i < 9; i++) {
		t32_bcsh_p16(cp, 2 * (0x3f + i), ox[i]);
		t32_bcsh_p16(cp, 2 * (0x48 + i), oy[i]);
	}
	for (p = 0x40; p < 0x48; p++) {
		d = (int)t32_tp_le16(cp + 2 * p) - (int)t32_tp_le16(cp + 2 * (p - 1));
		v = 0;
		if (d > 0)
			v = (((int)t32_tp_le16(cp + 2 * (p + 9)) - (int)t32_tp_le16(cp + 2 * (p + 8))) * 0x400 +
			     (d >> 1)) / d;
		t32_bcsh_p16(cp, 2 * (p + 0x11), t32_bcsh_clip(v, 0, 0x7fff));
	}
	u7 = cp[0x3a];
	if (sec[0x470] && cp[0x49] < 0x80)
		u7 = (u8)t32_bcsh_rnd(u7 * cp[0x49], 7);
	if (sec[0x46f] && cp[0x100] < 0x80)
		u7 = (u8)t32_bcsh_rnd(u7 * cp[0x100], 7);
	t32_bcsh_p16(cp, 2 * 0x1e, t32_bcsh_rnd(t32_tp_le16(cp + 2 * 0x19) * u7, 7));
	t32_bcsh_p16(cp, 2 * 0x1f, t32_bcsh_rnd(t32_tp_le16(cp + 2 * 0x1a) * u7, 7));
	t32_bcsh_p16(cp, 2 * 0x20, t32_bcsh_rnd(t32_tp_le16(cp + 2 * 0x1b) * u7, 7));
	u7 = (u16)t32_bcsh_rnd(t32_tp_le16(cp + 2 * 0x1c) * u7, 7);
	t32_bcsh_p16(cp, 2 * 0x21, u7);
	d = (int)t32_tp_le16(cp + 2 * 0x18) - (int)t32_tp_le16(cp + 2 * 0x17);
	if (d < 1) {
		t32_bcsh_p16(cp, 2 * 0x22, 0);
		t32_bcsh_p16(cp, 2 * 0x23, 0);
	} else {
		int d1 = (int)t32_tp_le16(cp + 2 * 0x1f) - (int)t32_tp_le16(cp + 2 * 0x1e);
		int d2 = (int)u7 - (int)t32_tp_le16(cp + 2 * 0x20);

		t32_bcsh_p16(cp, 2 * 0x22, ((d1 < 0 ? -d1 : d1) + (d >> 1)) / d);
		t32_bcsh_p16(cp, 2 * 0x23, ((d2 < 0 ? -d2 : d2) + (d >> 1)) / d);
	}
	v = cp[0xb3];
	t32_bcsh_p16(cp, 2 * 0x5a, t32_bcsh_clip(v ? ((v >> 1) + 0x400) / v : 0, 0, 0x3ff));
	d = (int)t32_tp_le16(cp + 2) - (int)t32_tp_le16(cp);
	t32_bcsh_p16(cp, 4, t32_bcsh_clip(d > 0 ? ((d >> 1) + 0x1000) / d : 0, 0, 0x3ff));
	for (p = 5; p < 9; p++) {
		d = (int)t32_tp_le16(cp + 2 * p) - (int)t32_tp_le16(cp + 2 * (p - 1));
		v = 0;
		if (d > 0) {
			v = (int)t32_tp_le16(cp + 2 * (p + 5)) - (int)t32_tp_le16(cp + 2 * (p + 4));
			v = ((v < 0 ? -v : v) * 0x400 + (d >> 1)) / d;
		}
		t32_bcsh_p16(cp, 2 * (p + 9), t32_bcsh_clip(v, 0, 0x7fff));
	}
	/* hue */
	if (sec[0x4a8] == 1 || (sec[0x4a8] == 0 && sec[0x4a9] != 2))
		hue = sec[0x4a7] ? sec[0x4ac] : 0x80;
	else
		hue = ap[3];
	cp[0xb6] = (u8)hue;
	for (i = 0; i < 9; i++) {
		m1[i] = t32_bcsh_s32(ap, 0x30 + 4 * i);
		m2[i] = t32_bcsh_s32(ap, 0x5c + 4 * i);
		mat[i] = t32_bcsh_s32(cp, 0xb8 + 4 * i);
	}
	t32_bcsh_hue(hue, mat, m1, m2, hm);
	for (i = 0; i < 9; i++)
		t32_bcsh_p32(cp, 0xdc + 4 * i, (u32)hm[i]);
	d = (int)t32_tp_le16(sec + 0xe0) - (int)t32_tp_le16(sec + 0xde);
	t32_bcsh_p16(cp, 6, t32_bcsh_clip(d > 0 ? ((d >> 1) + 0x1000) / d : 0, 0, 0x3ff));
	/* RGB offsets */
	t32_bcsh_p16(cp, 2 * 0x8a, (u32)ap[0x56] << 2);
	t32_bcsh_p16(cp, 2 * 0x8b, 0);
	v = ap[0x57] == 0xff ? 0x3ff : 0x3ac;
	t32_bcsh_p16(cp, 2 * 0x8d, v);
	t32_bcsh_p16(cp, 2 * 0x8c, (u32)ap[0x56] << 2);
	t32_bcsh_p16(cp, 2 * 0x8f, v);
	t32_bcsh_p16(cp, 2 * 0x8e, ((int)ap[0x57] - (int)ap[0x56]) * 4);
	t32_bcsh_p16(cp, 2 * 0x90, (u32)ap[0x58] << 2);
	t32_bcsh_p16(cp, 2 * 0x91, 0);
	v = ap[0x59] == 0xff ? 0x3ff : 0x3c0;
	t32_bcsh_p16(cp, 2 * 0x93, v);
	t32_bcsh_p16(cp, 2 * 0x92, (u32)ap[0x58] << 2);
	t32_bcsh_p16(cp, 2 * 0x95, v);
	t32_bcsh_p16(cp, 2 * 0x94, ((int)ap[0x59] - (int)ap[0x58]) * 4);
	t32_bcsh_p16(cp, 2 * 0x84, 0);
	t32_bcsh_p16(cp, 2 * 0x85, 0);
	t32_bcsh_p16(cp, 2 * 0x86, 0);
}

/* tisp_params_transform + the slot stores of tisp_bcsh_init */
static inline void t33_bcsh_store(const u8 *st, const u8 *sec, u32 *l)
{
	u8 in[T33_BCSH_INFO_BYTES];
	unsigned int k;

	memset(in, 0, sizeof(in));
	for (k = 0; k < ARRAY_SIZE(t33_bcsh_tf); k++) {
		const u8 *s = (t33_bcsh_tf[k].sec ? sec : st) + t33_bcsh_tf[k].src;

		in[t33_bcsh_tf[k].dst] = s[0];
		if (!t33_bcsh_tf[k].byte)
			in[t33_bcsh_tf[k].dst + 1] = s[1];
	}
#define H(i)	((u32)t32_tp_le16(in + 2 * (i)))
#define B(o)	((u32)in[o])
#define HH(a, b, m)	((H(a) & (m)) << 16 | (H(b) & (m)))
	T33_SW(l, 2, HH(0x1d, 0x16, 0x3ff));
	T33_SW(l, 6, HH(0x2e, 7, 0x3ff));
	T33_SW(l, 0x28, HH(0x4b, 0xf, 0x3ff));
	T33_SW(l, 0x29, HH(0x32, 0x17, 0x3ff));
	T33_SW(l, 0x2a, HH(0x54, 0x15, 0x3ff));
	T33_SW(l, 0x2b, HH(0x3e, 10, 0x3ff));
	T33_SW(l, 0x2c, HH(9, 0x3f, 0x3ff));
	T33_SW(l, 0x2d, HH(2, 0x2a, 0x3ff));
	T33_SW(l, 0x2e, HH(6, 3, 0x3ff));
	T33_SW(l, 0x2f, HH(0x11, 0x4e, 0x3ff));
	T33_SW(l, 0x30, HH(0x51, 0x2c, 0x3ff));
	T33_SW(l, 10, HH(0x50, 0x21, 0x3ff));
	T33_SW(l, 0xb, HH(0x47, 0x1c, 0x1fff));
	T33_SW(l, 0xc, HH(0x46, 0x23, 0x1fff));
	T33_SW(l, 0xd, HH(0x27, 0x13, 0x3ff));
	T33_SW(l, 0xe, HH(0x29, 0x4f, 0x3ff));
	T33_SW(l, 0x10, HH(0x10, 0, 0x3ff));
	T33_SW(l, 0x11, HH(0x28, 0x48, 0x3ff));
	T33_SW(l, 0x12, H(4) & 0x3ff);
	T33_SW(l, 0x17, HH(0xb, 0x14, 0x3ff));
	T33_SW(l, 0x18, HH(0x52, 0x1e, 0x3ff));
	T33_SW(l, 0x19, HH(0x20, 0x1b, 0x3ff));
	T33_SW(l, 0x1a, HH(5, 0x3a, 0x3ff));
	T33_SW(l, 0x1b, HH(0x42, 0x33, 0x3ff));
	T33_SW(l, 0x1c, HH(0xe, 0x30, 0x3ff));
	T33_SW(l, 0x1e, HH(0x19, 0x25, 0x7ff));
	T33_SW(l, 0x1f, HH(0x35, 0x22, 0x7ff));
	T33_SW(l, 0x20, HH(0x4a, 0xd, 0x7ff));
	T33_SW(l, 0x22, HH(0x41, 0x3d, 0x3fff));
	T33_SW(l, 0x23, HH(0x18, 0xc, 0x3fff));
	T33_SW(l, 0x24, HH(0x31, 0x26, 0x3fff));
	T33_SW(l, 0x25, HH(0x12, 0x40, 0x3fff));
	T33_SW(l, 0x26, H(0x2d) & 0x3fff);
	T33_SW(l, 1, (H(0x2b) & 0x3ff) << 16 | (B(0xad) & 1));
	T33_SW(l, 3, (H(0x34) & 0x3ff) << 16 | (B(0xaa) & 1));
	T33_SW(l, 4, HH(1, 0x1f, 0x3ff));
	T33_SW(l, 5, (H(0x53) & 0x3ff) << 16 | (B(0xb1) & 1));
	T33_SW(l, 7, (B(0xab) & 1) << 16);
	T33_SW(l, 0x31, HH(0x43, 0x49, 0x7fff));
	T33_SW(l, 0x32, HH(0x3b, 0x4c, 0x7fff));
	T33_SW(l, 0x33, HH(0x3c, 8, 0x7fff));
	T33_SW(l, 0x34, HH(0x44, 0x4d, 0x7fff));
	T33_SW(l, 9, (H(0x1a) & 0x1fff) << 3 | (H(0x45) & 0x1fff) << 16 | (B(0xb0) & 1));
	T33_SW(l, 0xf, (B(0xac) & 1) << 16 | (H(0x24) & 0x3ff));
	T33_SW(l, 0x13, H(0x37) << 16 | H(0x2f));
	T33_SW(l, 0x14, H(0x36) << 16 | H(0x38));
	T33_SW(l, 0x15, (B(0xae) & 0x3f) << 4 | (H(0x39) & 0x3ff) << 16 | (B(0xaf) & 3));
#undef H
#undef B
#undef HH
}

/*
 * Block init into list l (capacity cap words): sec a writable copy of
 * section 19 (the block clears ten flags and may fix 0x116/0x118 in it),
 * lut / lutbytes its payload, csc the colour space section (9), st
 * T33_BCSH_ST_BYTES of block state, ct / ev the tisp init colour
 * temperature and EV.  Returns the list length, -1 on a refused section.
 */
T33_BCSH_FN t33_bcsh_init(u8 *sec, const u8 *lut, u32 lutbytes, const u8 *csc, int ct,
				unsigned long long ev, u8 *st, u32 *l, u32 cap)
{
	static const u16 clear[] = { 0x4ae, 0x4af, 0x477, 0x478, 0x471, 0x472, 0x4a8, 0x4a9, 0x4a3, 0x4a4 };
	u8 *ap = st + 0x1cc;
	unsigned int k;
	int len;

	if (t33_le32(sec) != T33_BCSH_ID || sec[0x4a2] > 2)
		return -1;
	if (!(csc[0x62] == 1 || (csc[0x62] == 0 && csc[0x63] < 2)))
		return -1;
	if (sec[0x4a2] == 1 && (!sec[0x4bf] || lutbytes < sec[0x4bf] * 40U))
		return -1;
	len = t33_rcfg_tmpl_init(l, cap, t33_bcsh_tmpl, ARRAY_SIZE(t33_bcsh_tmpl));
	if (len <= 0)
		return len;
	memset(st, 0, T33_BCSH_ST_BYTES);
	/* bcsh_ap defaults (user settings 128), tisp_csc_get into ap + 0x2c; bcsh_cp / sp are 0 */
	memcpy(ap, t33_bcsh_ap_def, sizeof(t33_bcsh_ap_def));
	t32_bcsh_p32(ap, 0x2c, csc[0x5b]);
	memcpy(ap + 0x54, csc + 0x52, 6);
	memcpy(ap + 0x80, csc + 0x5c, 6);
	memcpy(ap + 0x30, csc + 4, 0x24);
	memcpy(ap + 0x5c, csc + 0x28, 0x24);
	for (k = 0; k < ARRAY_SIZE(clear); k++)
		sec[clear[k]] = 0;
	ev >>= 10;
	t32_bcsh_p32(st, 8, (u32)ev);
	t32_bcsh_p32(st, 0xc, (u32)(ev >> 32));
	t32_bcsh_p32(st, 0x24, 0xff);
	t33_bcsh_new_para(sec, st + 0x25c);
	t33_bcsh_ev(st, sec, ev);
	t33_bcsh_ct(st, sec, lut, ct);
	t33_bcsh_calc(st, sec);
	t33_bcsh_store(st, sec, l);
	return len;
}

#endif /* TX_ISP_T33_BCSH_H */
