/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 brightness/contrast/saturation/hue block (BCSH, block 0xf000) from the
 * IQ bin at AddSensor (S4 step 8): tisp_bcsh_init -> get_new_para,
 * interp_by_ev (exposure 0, forced), interp_by_ct (5000 K, forced),
 * calc_para_refresh(0), write_reg and the block id.
 *
 * IQ bin section 19 (1204 bytes) with its colour temperature knots behind it
 * (+0x4af knots of 10 s32 { colour temperature, 3x3 matrix }, +0x4b0 the
 * hysteresis in K), and the colour space conversion section (9): its
 * matrices, offsets and mode are what the vendor's tisp_csc_api_get hands
 * the block.  The block's own user settings (brightness, contrast,
 * saturation, hue) start at 128 each, the neutral value.
 *
 * Built: the init flow of the vendor and the per-frame updates on the state it
 * keeps (struct t32_bcsh): t32_bcsh_ev_update / t32_bcsh_ct_update (interp, counters,
 * calc_para_refresh modes 1 and 2, write request) and t32_bcsh_frame_done (the write at the
 * frame end).  The transcription keeps the vendor's buffer layout (parameter set "cp" of
 * 300 bytes, user/csc set "ap") so each line can be compared with the decompiled function;
 * offsets are in bytes.  The colour space data comes from the mode table the vendor applies
 * to its copy of the CSC section (t32_bcsh_csc_apply).  The init forces the matrix
 * (tisp_bcsh_interp_by_ct) before the hue matrix is computed from it.
 * Refused: a colour space mode above 5 and a knot table shorter than the knot count says.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15)
 * by tests/t32_oracle (t32_bcsh_oracle: init, t32_bcshf_oracle: per frame).
 */
#ifndef TX_ISP_T32_BCSH_H
#define TX_ISP_T32_BCSH_H

#include "tx_isp_t32_tp.h"

#define T32_SEC_BCSH		19
#define T32_BCSH_SEC_BYTES	0x4b4U
#define T32_BCSH_CSC_BYTES	0x64U
#define T32_BCSH_ID		0x0134dae2U
#define T32_BCSH_CT		5000
#define T32_BCSH_WRITES		49
#define T32_BCSH_MAX_WRITES	49

/*
 * The vendor's per-channel state of the block (tisp_bcsh_init allocates it).
 * cp: parameter set (300 bytes, zero at start), ap: user/csc set (144 bytes,
 * compiled-in defaults d6c80), p5: the 12 exposure bands (48 bytes), m112: the
 * handed-out matrix block, and the 24-byte block of caches and counters
 * (d6d10): last exposure, last colour temperature and its band, the update
 * counters of the two paths and the write request.
 */
struct t32_bcsh {
	u8 *sec;			/* section 19, live */
	const u8 *lut;			/* colour temperature knots behind it */
	u32 lutbytes;
	u8 cp[300];
	u8 ap[0x90];
	u8 p5[48];
	u8 m112[112];
	u8 cs[T32_BCSH_CSC_BYTES];	/* the vendor's colour space section copy */
	u32 ev_lo, ev_hi;		/* exposure of the last strength update */
	u32 ct, ct_code;		/* colour temperature of the last matrix update, its band */
	u8 ctr_ev, ctr_ct;		/* update counters (wrap at 255 -> 0) */
	u8 pend;			/* write request (1..16) */
	u8 b19;				/* 1: take the user hue settings ap[136..141] */
};

/* d6c80 (data.c:25805): the user/csc set before tisp_csc_api_get overwrites its parts */
static const u32 t32_bcsh_ap_default[36] = {
	0x80808080, 0x00000100, 0x00010000, 0x00000000, 0x00000000, 0x00000000,
	0x00010000, 0x00000000, 0x00000000, 0x00000000, 0x00010000, 0x00000000,
	0x00004c8b, 0x00009646, 0x00001d2f, 0xffffd4ce, 0xffffab32, 0x00008000,
	0x00008000, 0xffff94d1, 0xffffeb2f, 0xff008000, 0x0000ff00, 0x00010000,
	0x00000000, 0x000166e9, 0x00010000, 0xffffa7e7, 0xffff492e, 0x00010000,
	0x0001c5a2, 0x00000000, 0xff008000, 0x0000ff00, 0x00000000, 0x00000000,
};

static inline int t32_bcsh_s16(const u8 *p, u32 o)
{
	return (s16)(p[o] | p[o + 1] << 8);
}

static inline int t32_bcsh_s32(const u8 *p, u32 o)
{
	return (int)(p[o] | (u32)p[o + 1] << 8 | (u32)p[o + 2] << 16 | (u32)p[o + 3] << 24);
}

static inline void t32_bcsh_p16(u8 *p, u32 o, u32 v)
{
	p[o] = (u8)v;
	p[o + 1] = (u8)(v >> 8);
}

static inline void t32_bcsh_p32(u8 *p, u32 o, u32 v)
{
	t32_bcsh_p16(p, o, v);
	t32_bcsh_p16(p, o + 2, v >> 16);
}

/* tisp_round_int64(x, x >> 63, n): x / 2^n rounded half up */
static inline long long t32_bcsh_rnd(long long x, unsigned int n)
{
	return (x >> n) + ((x >> (n - 1)) & 1);
}

/* the same, rounded away from zero (the hue block's) */
static inline long long t32_bcsh_symrnd(long long v, unsigned int n)
{
	long long h = 1LL << (n - 1);

	return v >= 0 ? (v + h) >> n : -((h - v) >> n);
}

/* tisp_bcsh_sub_interp: a + round((b - a) * f / 4096), the product in 32 bit */
static inline int t32_bcsh_si(int f, int a, int b)
{
	return a + (int)t32_bcsh_rnd((int)((u32)(b - a) * (u32)f), 12);
}

static inline int t32_bcsh_clip(int v, int lo, int hi)
{
	return v < lo ? lo : v > hi ? hi : v;
}

/* tisp_bcsh_adjust_contrast */
static inline void t32_bcsh_contrast(u32 c, const u32 *x, const u32 *y, u32 *ox, u32 *oy)
{
	int i;

	for (i = 0; i < 9; i++) {
		if (c == 0x80) {
			ox[i] = x[i];
			oy[i] = y[i];
		} else if (c & 0x80) {
			int iv = x[i] >= 0x201 ? 0x3ff : 0;

			ox[i] = (u16)t32_bcsh_rnd((int)(x[i] * (0x100 - c) + (c - 0x80) * 0x200), 7);
			oy[i] = (u16)t32_bcsh_rnd(iv * (int)(c - 0x80) + (int)(y[i] * (0x100 - c)), 7);
		} else {
			int iv = y[i] >= 0x201 ? 0x3ff : 0;

			ox[i] = (u16)t32_bcsh_rnd(iv * (int)(0x80 - c) + (int)(x[i] * c), 7);
			oy[i] = (u16)t32_bcsh_rnd((int)(y[i] * c + (0x80 - c) * 0x200), 7);
		}
	}
}

/* tisp_bcsh_matrix_multi: out = a * b (3x3, row major), rounded, >> sh */
static inline void t32_bcsh_mmul(int *out, const int *a, const int *b, unsigned int sh)
{
	long long h = 1LL << (sh - 1);
	int i, j, k;

	for (i = 0; i < 3; i++) {
		for (j = 0; j < 3; j++) {
			long long s = 0;

			for (k = 0; k < 3; k++)
				s += (long long)a[3 * i + k] * b[3 * k + j];
			if (s < 0x100000000LL && (s >> 32 != 0 || (u32)s == 0))
				out[3 * i + j] = (int)-((h - s) >> sh);
			else
				out[3 * i + j] = (int)((s + h) >> sh);
		}
	}
}

/* tisp_bcsh_itp */
static inline u32 t32_bcsh_itp(u32 x, u32 lo, u32 hi, u32 v0, u32 v1)
{
	if (lo < hi && lo < x) {
		if (x < hi)
			return ((x - lo) * v1 + (hi - x) * v0 + ((hi - lo) >> 1)) / (hi - lo);
		return v1;
	}
	return v0;
}

/* tisp_bcsh_adjust_hue: out = the colour matrix rotated by the hue (128 = none) */
static inline void t32_bcsh_hue(u32 h, const int *ccm, const int *m1, const int *m2, int *out)
{
	static const u32 sin_t[5] = { 0, 0x61f8, 0xb505, 0xec83, 0x10000 };
	int t54[9], t78[9], j;
	u32 q, a, b, c4, c5;
	long long s, c;

	h &= 0xff;
	q = h >> 4;
#define T32_BCSH_FOLD(x)	((x) <= 4 ? (x) : 8 - (x))
	a = T32_BCSH_FOLD(q & 7);
	b = T32_BCSH_FOLD((q + 1) & 7);
	c4 = T32_BCSH_FOLD((q + 4) & 7);
	c5 = T32_BCSH_FOLD((q + 5) & 7);
#undef T32_BCSH_FOLD
	s = (long long)t32_bcsh_itp(h & 0xf, 0, 0x10, sin_t[a], sin_t[b]);
	c = (long long)t32_bcsh_itp(h & 0xf, 0, 0x10, sin_t[c4], sin_t[c5]);
	if (!(h >> 7))
		s = -s;
	if (((h - 0x40) & 0xff) >= 0x80)
		c = -c;
	t32_bcsh_mmul(t54, m1, ccm, 10);
	t32_bcsh_mmul(t78, t54, m2, 16);
	for (j = 0; j < 3; j++) {
		out[j] = (int)t32_bcsh_symrnd(t78[j], 6);
		out[3 + j] = (int)t32_bcsh_symrnd(t78[3 + j] * c + t78[6 + j] * s, 22);
		out[6 + j] = (int)t32_bcsh_symrnd(t78[6 + j] * c - t78[3 + j] * s, 22);
	}
}

/*
 * The vendor's tisp_csc_para_refresh_by_mode overwrites the colour space section
 * copy with the table of the mode (92 bytes, data.c d7ffc, d7fa0, d7f44, d7ee8,
 * d7e8c, d7e30 for the modes 0..5): the mode byte at +0x5b, the bytes +0x52..0x57
 * and +0x5c..0x61, the first matrix at +4 and the second at +0x28.  What the block
 * reads through tisp_csc_api_get comes from that overwritten copy, not from the bin.
 */
static const u32 t32_bcsh_csc_tab[6][23] = {
	{ /* d7ffc */
		0x00000000, 0x00004c8b, 0x00009646, 0x00001d2f, 0xffffd4ce, 0xffffab32,
		0x00008000, 0x00008000, 0xffff94d1, 0xffffeb2f, 0xff008000, 0x0000ff00,
		0x00010000, 0x00000000, 0x000166e9, 0x00010000, 0xffffa7e7, 0xffff492e,
		0x00010000, 0x0001c5a2, 0x00000000, 0xff008000, 0x0000ff00,
	},
	{ /* d7fa0 */
		0x00000001, 0x000041bd, 0x0000810f, 0x00001910, 0xffffda0e, 0xffffb581,
		0x00007070, 0x00007070, 0xffffa1d9, 0xffffedb7, 0xeb108010, 0x0000f010,
		0x00012a15, 0x00000000, 0x00019895, 0x00012a15, 0xffff9bb5, 0xffff2fe1,
		0x00012a15, 0x00020469, 0x00000000, 0xeb108010, 0x0000f010,
	},
	{ /* d7f44 */
		0x00000002, 0x0000366d, 0x0000b717, 0x0000127c, 0xffffe2ab, 0xffff9d55,
		0x00008000, 0x00008000, 0xffff8bbd, 0xfffff443, 0xff008000, 0x0000ff00,
		0x00010000, 0x00000000, 0x00019326, 0x00010000, 0xffffd00c, 0xffff8829,
		0x00010000, 0x0001db09, 0x00000000, 0xff008000, 0x0000ff00,
	},
	{ /* d7ee8 */
		0x00000003, 0x00002ebe, 0x00009d3e, 0x00000fe0, 0xffffe63c, 0xffffa953,
		0x00007070, 0x00007070, 0xffff99df, 0xfffff5b1, 0xeb108010, 0x0000f010,
		0x00012a15, 0x00000000, 0x0001caf1, 0x00012a15, 0xffffc969, 0xffff7793,
		0x00012a15, 0x00021cc6, 0x00000000, 0xeb108010, 0x0000f010,
	},
	{ /* d7e8c */
		0x00000004, 0x00004340, 0x0000ad91, 0x00000f2e, 0xffffdc41, 0xffffa3bf,
		0x00008000, 0x00008000, 0xffff8a4b, 0xfffff5b5, 0xff008000, 0x0000ff00,
		0x00010000, 0x00000000, 0x0001797f, 0x00010000, 0xffffd5e0, 0xffff6dbc,
		0x00010000, 0x0001e1a3, 0x00000000, 0xff008000, 0x0000ff00,
	},
	{ /* d7e30 */
		0x00000005, 0x000039c2, 0x00009510, 0x00000d0a, 0xffffe09a, 0xffffaef6,
		0x00007070, 0x00007070, 0xffff989b, 0xfffff6f5, 0xeb108010, 0x0000f010,
		0x00012a15, 0x00000000, 0x0001adbe, 0x00012a15, 0xffffd00b, 0xffff597e,
		0x00012a15, 0x0002244b, 0x00000000, 0xeb108010, 0x0000f010,
	},
};

/* apply the table of mode (0..5) to the colour space section copy cs (T32_BCSH_CSC_BYTES) */
static inline void t32_bcsh_csc_apply(u8 *cs, unsigned int mode)
{
#define T32_BCSH_TB(i)	((u8)(t32_bcsh_csc_tab[mode][(i) / 4] >> (8 * ((i) % 4))))
	unsigned int i;

	cs[0x5b] = T32_BCSH_TB(0);
	for (i = 0; i < 6; i++) {
		cs[0x52 + i] = T32_BCSH_TB(40 + i);
		cs[0x5c + i] = T32_BCSH_TB(84 + i);
	}
	for (i = 0; i < 36; i++) {
		cs[4 + i] = T32_BCSH_TB(4 + i);
		cs[0x28 + i] = T32_BCSH_TB(48 + i);
	}
#undef T32_BCSH_TB
}

/*
 * tisp_bcsh_get_new_para: the exposure band table p5 from section words and
 * the neutral limits.
 */
static inline void t32_bcsh_new_para(struct t32_bcsh *st)
{
	u8 *sec = st->sec, *p5 = st->p5;
	int k;

	if (t32_tp_le16(sec + 0x108) <= t32_tp_le16(sec + 0x106)) {
		t32_bcsh_p16(sec, 0x106, 0);
		t32_bcsh_p16(sec, 0x108, 1);
	}
	for (k = 0; k < 12; k++) {
		u32 lo = ((t32_tp_le16(sec + 0x262 + 0x18 + 2 * k) & 0x7fff) >> 10) * 0x20 +
			 ((t32_tp_le16(sec + 0x262 + 2 * k) & 0x7fff) >> 10);
		u32 hi = ((t32_tp_le16(sec + 0x262 + 0xf0 + 2 * k) & 0x7fff) >> 10) * 0x20 +
			 ((t32_tp_le16(sec + 0x262 + 0xd8 + 2 * k) & 0x7fff) >> 10);

		if (hi <= lo) {
			lo = 0;
			hi = 1;
		}
		t32_bcsh_p16(p5, 2 * k, lo);
		t32_bcsh_p16(p5, 2 * k + 0x18, hi);
	}
}

/*
 * tisp_bcsh_interp_by_ev: the exposure dependent parameters.  The exposure
 * (hi:lo) must have moved by more than the section's threshold u32 +0x64 (64 bit
 * distance, limited to 32 bit) unless forced.  The bands: section byte 0x4b1
 * clear: ascending thresholds at +4 against hi << 22 | lo >> 10; set:
 * descending thresholds at +0x34 against lo.  i18 / i17 are the two neighbouring
 * entries and f the weight (12 bit) of the second; below the first threshold and
 * past the last both are the same entry.  Returns 0 when updated, 1 when not.
 */
static inline u32 t32_bcsh_ev_interp(struct t32_bcsh *st, u32 lo, u32 hi, u32 force)
{
	u8 *sec = st->sec, *cp = st->cp, *ap = st->ap, *p5 = st->p5;
	unsigned long long now = (unsigned long long)hi << 32 | lo;
	unsigned long long old = (unsigned long long)st->ev_hi << 32 | st->ev_lo, d;
	u32 dv, ev = hi << 22 | lo >> 10;
	int i17 = 0, i18 = 0, f = 0, a, b, k;

	d = ((int)hi < (int)st->ev_hi || (hi == st->ev_hi && lo < st->ev_lo)) ? old - now : now - old;
	dv = (int)(u32)(d >> 32) < 1 ? (u32)d : 0xffffffffU;
	if (!(t32_front_le32(sec + 100) < dv) && !(force & 0xff))
		return 1;

	for (k = 0; k < 12; k++) {
		u32 t = t32_front_le32(sec + (sec[0x4b1] ? 0x34 : 4) + 4 * k), prev;
		int w;

		if (sec[0x4b1] ? t < lo : ev < t) {
			if (k) {
				prev = t32_front_le32(sec + (sec[0x4b1] ? 0x34 : 4) + 4 * (k - 1));
				i18 = k - 1;
				i17 = k;
				w = (int)(sec[0x4b1] ? prev - t : t - prev);
				if (w > 0) {
					int dist = (int)(sec[0x4b1] ? prev - lo : ev - prev);

					f = (int)t32_tp_div64((unsigned long long)((long long)dist * 4096 + (w >> 1)),
							      (unsigned long long)(long long)w);
				}
			}
			break;
		}
		if (k == 11)
			i18 = i17 = 11;
	}
	a = i18 * 2;
	b = i17 * 2;
#define SIH(dst, o, mask) \
	t32_bcsh_p16(cp, 2 * (dst), t32_bcsh_si(f, t32_tp_le16(sec + a + (o)) & (mask), t32_tp_le16(sec + b + (o)) & (mask)))
#define SIS(dst, o) \
	t32_bcsh_p16(cp, 2 * (dst), t32_bcsh_si(f, (s16)t32_tp_le16(sec + a + (o)), (s16)t32_tp_le16(sec + b + (o))))
	SIS(0x81, 0x412);
	SIS(0x82, 0x42a);
	SIS(0x83, 0x442);
	SIH(0x25, 0x262, 0x3ff);
	SIH(0x26, 0x27a, 0x3ff);
	for (k = 0; k < 7; k++)
		SIH(0x27 + k, 0x292 + 0x18 * k, 0xffff);
	SIH(0x2e, 0x33a, 0x3ff);
	SIH(0x2f, 0x352, 0x3ff);
	for (k = 0; k < 7; k++)
		SIH(0x30 + k, 0x36a + 0x18 * k, 0xffff);
	switch (sec[0x45c]) {
	case 0:
		cp[0x24] = 0;
		break;
	case 1:
		cp[0x24] = 1;
		SIH(0x13, 0xd6, 0xffff);
		SIH(0x14, 0xee, 0xffff);
		break;
	case 2:
		cp[0x24] = 1;
		t32_bcsh_p16(cp, 0x26, t32_tp_le16(sec + 0xd2));
		t32_bcsh_p16(cp, 0x28, t32_tp_le16(sec + 0xd4));
		break;
	}
	if (sec[0x45a] == 1) {
		SIH(0, 0x9e, 0xffff);
		SIH(1, 0xb6, 0xffff);
	}
	for (k = 0; k < 10; k++)
		SIH(4 + k, 0x172 + 0x18 * k, 0xffff);
	switch (sec[0x45d]) {
	case 0:
		for (k = 0x19; k <= 0x1c; k++)
			t32_bcsh_p16(cp, 2 * k, 0x400);
		break;
	case 1:
		t32_bcsh_p16(cp, 2 * 0x17, t32_bcsh_si(f, t32_tp_le16(p5 + a), t32_tp_le16(p5 + b)));
		t32_bcsh_p16(cp, 2 * 0x18, t32_bcsh_si(f, t32_tp_le16(p5 + a + 0x18), t32_tp_le16(p5 + b + 0x18)));
		SIH(0x19, 0x112, 0xffff);
		SIH(0x1a, 0x12a, 0xffff);
		SIH(0x1b, 0x142, 0xffff);
		SIH(0x1c, 0x15a, 0xffff);
		break;
	case 2:
		for (k = 0; k < 6; k++)
			t32_bcsh_p16(cp, 2 * (0x17 + k), t32_tp_le16(sec + 0x106 + 2 * k));
		break;
	}
	if (sec[0x461] == 1 || (sec[0x461] == 0 && sec[0x462] != 2)) {
		if (sec[0x45d] <= 2)
			cp[0x2c] = sec[0x45d] ? 1 : 0;
		cp[0x3a] = sec[0x45e];
	} else {
		cp[0x2c] = 1;
		cp[0x3a] = ap[2];
	}
	if (sec[0x467] == 1 || (sec[0x467] == 0 && sec[0x468] != 2)) {
		switch (sec[0x466]) {
		case 0:
			cp[0x48] = 0;
			cp[0x49] = 0x80;
			break;
		case 1:
			cp[0x48] = 1;
			cp[0x49] = (u8)t32_bcsh_si(f, sec[i18 + 0x46c], sec[i17 + 0x46c]);
			break;
		case 2:
			cp[0x48] = 1;
			cp[0x49] = sec[0x46b];
			break;
		}
	} else {
		cp[0x48] = 1;
		cp[0x49] = ap[1];
	}
	cp[0xb2] = (u8)t32_bcsh_si(f, sec[i18 + 0x47a], sec[i17 + 0x47a]);
	cp[0xb3] = (u8)t32_bcsh_si(f, sec[i18 + 0x486], sec[i17 + 0x486]);
	if (sec[0x49e] == 1 || (sec[0x49e] == 0 && sec[0x49f] != 2)) {
		switch (sec[0x49d]) {
		case 0:
			cp[0x100] = 0x80;
			break;
		case 1:
			cp[0x100] = (u8)t32_bcsh_si(f, sec[i18 + 0x4a3], sec[i17 + 0x4a3]);
			break;
		case 2:
			cp[0x100] = sec[0x4a2];
			break;
		}
	} else {
		cp[0x100] = ap[0];
	}
#undef SIH
#undef SIS
	st->ev_lo = lo;
	st->ev_hi = hi;
	return 0;
}

/* the matrix at colour temperature ct from the knots, signed compares (tisp_bcsh_interp_by_ct) */
static inline void t32_bcsh_knot_matrix(const u8 *lut, int knots, int hyst, int ct, int *mat)
{
	int i, idx;

	if (knots == 1 || !((int)t32_front_le32(lut) < ct)) {
		for (i = 0; i < 9; i++)
			mat[i] = t32_bcsh_s32(lut, 4 + 4 * i);
	} else if (!(ct < (int)t32_front_le32(lut + 40 * (knots - 1)))) {
		for (i = 0; i < 9; i++)
			mat[i] = t32_bcsh_s32(lut, 40 * (knots - 1) + 4 + 4 * i);
	} else {
		for (idx = 1; idx < knots && (int)t32_front_le32(lut + 40 * idx) < ct; idx++)
			;
		{
			const u8 *lo = lut + 40 * (idx - 1), *hi = lut + 40 * idx;
			int clo = (int)t32_front_le32(lo), chi = (int)t32_front_le32(hi);

			if ((int)((u32)hyst + (u32)clo) < ct && ct < (int)((u32)chi - (u32)hyst)) {
				int den = chi - clo - 2 * hyst, num = ct - clo - hyst;

				for (i = 0; i < 9; i++) {
					u32 a = t32_front_le32(lo + 4 + 4 * i), b = t32_front_le32(hi + 4 + 4 * i);
					int s = (int)((u32)(den - num) * a + (u32)num * b);
					u32 r = (u32)(den >> 1);

					s = (int)(s < 1 ? (u32)s - r : (u32)s + r) / den;
					mat[i] = s < -0x2000 ? -0x2000 : s > 0x1fff ? 0x1fff : s;
				}
			} else {
				const u8 *k = (int)((u32)hyst + (u32)clo) < ct ? hi : lo;

				for (i = 0; i < 9; i++)
					mat[i] = t32_bcsh_s32(k, 4 + 4 * i);
			}
		}
	}
}

/*
 * tisp_bcsh_interp_by_ct: the matrix at 0xb8 for the colour temperature ct.  The
 * switches mirror the CCM's: section bytes 0x493 / 0x494 and the API flag
 * ap[4] choose between the tuning and the API matrix (ap +8, 6 bit fraction).
 * Tuned: the same cache rule as the CCM (even band done, odd band and band
 * changes need |dct| > u16 +0x9c), then byte 0x492 selects 0 unit gains,
 * 1 the knots, 2 the section's own matrix at +0x68, other: unchanged.  Byte 0x496
 * = 1 publishes the matrix to the API (shifted left 6).  Returns 0 or 1.
 */
static inline u32 t32_bcsh_ct_interp(struct t32_bcsh *st, u32 ct, u32 force)
{
	u8 *sec = st->sec, *cp = st->cp, *ap = st->ap;
	int knots = sec[1199], hyst = sec[1200], i, tuned;
	u32 band, m;

	force &= 0xff;
	tuned = sec[0x493] == 1 || (sec[0x493] == 0 && sec[0x494] < 2) || (s8)ap[4] == 0;
	if (!tuned) {
		if (force != 1)
			return 1;
		for (i = 0; i < 9; i++)
			t32_bcsh_p32(cp, 0xb8 + 4 * i, (u32)t32_bcsh_rnd((long long)t32_bcsh_s32(ap, 8 + 4 * i), 6));
		memcpy(st->m112, cp + 0xb8, 36);
		return 0;
	}
	{
		/* band of ct: 0 below the first knot, 2k on a knot, 2k + 1 between, signed compares */
		const u8 *lut = st->lut;

		band = knots;
		if (!((int)((u32)hyst + t32_front_le32(lut)) < (int)ct)) {
			band = 0;
		} else if (!((int)ct < (int)(t32_front_le32(lut + 40 * (knots - 1)) - (u32)hyst))) {
			band = 2U * (u32)(knots - 1);
		} else {
			for (i = 1; i < knots; i++) {
				u32 c = t32_front_le32(lut + 40 * i);

				if ((int)ct < (int)(c - (u32)hyst)) {
					if ((int)((u32)hyst + t32_front_le32(lut + 40 * (i - 1))) < (int)ct) {
						band = 2U * (u32)(i - 1) + 1;
						break;
					}
				} else if (!((int)((u32)hyst + c) < (int)ct)) {
					band = 2U * (u32)i;
					break;
				}
			}
		}
	}
	if (!force) {
		if (band == st->ct_code && !(band & 1))
			return 1;
		m = (u32)((int)(ct - st->ct) >> 31);
		if (!((int)t32_tp_le16(sec + 156) < (int)(((ct - st->ct) ^ m) - m)))
			return 1;
	}
	st->ct = ct;
	st->ct_code = band;
	switch (sec[0x492]) {
	case 0:
		memset(cp + 0xb8, 0, 36);
		t32_bcsh_p32(cp, 0xb8, 1024);
		t32_bcsh_p32(cp, 0xb8 + 16, 1024);
		t32_bcsh_p32(cp, 0xb8 + 32, 1024);
		break;
	case 1: {
		int mat[9];

		t32_bcsh_knot_matrix(st->lut, knots, hyst, (int)ct, mat);
		for (i = 0; i < 9; i++)
			t32_bcsh_p32(cp, 0xb8 + 4 * i, (u32)mat[i]);
		break;
	}
	case 2:
		memcpy(cp + 0xb8, sec + 0x68, 36);
		break;
	}
	memcpy(st->m112, cp + 0xb8, 36);
	if (sec[0x496] == 1)
		for (i = 0; i < 9; i++)
			t32_bcsh_p32(st->ap, 8 + 4 * i, t32_front_le32(cp + 0xb8 + 4 * i) << 6);
	return 0;
}

/*
 * tisp_bcsh_calc_para_refresh(mode): derived parameters.  Mode 0 all parts, 1 the
 * exposure part (gains, curves; with ap[136..141] taken first when b19 is 1), 2 the
 * colour temperature part (hue matrix from the matrix at 0xb8 only), 3 the tail
 * (limits, 0x8a..0x95 pairs); anything else does nothing.
 */
static inline void t32_bcsh_calc(struct t32_bcsh *st, u32 mode)
{
	u8 *sec = st->sec, *cp = st->cp, *ap = st->ap;
	int i;

	mode &= 0xff;
	if (mode < 2) {
		if (st->b19 == 1) {
			t32_bcsh_p16(cp, 258, t32_tp_le16(ap + 136));
			t32_bcsh_p16(cp, 260, t32_tp_le16(ap + 138));
			t32_bcsh_p16(cp, 262, t32_tp_le16(ap + 140));
		}
		{
			int m[9], in[3], o[3], d, v, p;
			u32 x[9], y[9], ox[9], oy[9], u7, u5;

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
			p = sec[0x466] ? 0x2e : 0x25;
			for (i = 0; i < 9; i++) {
				x[i] = t32_tp_le16(cp + 2 * (0x25 + i));
				y[i] = t32_tp_le16(cp + 2 * (p + i));
			}
			for (i = 0; i < 9; i++) {
				ox[i] = 0;
				oy[i] = 0;
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
					v = ((int)t32_tp_le16(cp + 2 * (p + 9)) - (int)t32_tp_le16(cp + 2 * (p + 8))) * 0x400;
				if (d > 0)
					v = (v + (d >> 1)) / d;
				t32_bcsh_p16(cp, 2 * (p + 0x11), t32_bcsh_clip(v, 0, 0x7fff));
			}
			u7 = cp[0x3a];
			if (sec[0x460] && cp[0x49] < 0x80)
				u7 = (u8)t32_bcsh_rnd(u7 * cp[0x49], 7);
			if (sec[0x45f] && cp[0x100] < 0x80)
				u7 = (u8)t32_bcsh_rnd(u7 * cp[0x100], 7);
			u5 = t32_tp_le16(cp + 2 * 0x19);
			t32_bcsh_p16(cp, 2 * 0x1e, t32_bcsh_rnd(u5 * u7, 7));
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


		}
	}
	if (mode == 0 || mode == 2) {
		int mat[9], m1[9], m2[9], hm[9], i, v, d;
		u32 hue;

		(void)v;
		(void)d;
		for (i = 0; i < 9; i++)
			mat[i] = t32_bcsh_s32(cp, 0xb8 + 4 * i);
		if (sec[0x498] == 1 || (sec[0x498] == 0 && sec[0x499] != 2))
			hue = sec[0x497] ? sec[0x49c] : 0x80;
		else
			hue = ap[3];
		cp[0xb6] = (u8)hue;
		for (i = 0; i < 9; i++) {
			m1[i] = t32_bcsh_s32(ap, 0x30 + 4 * i);
			m2[i] = t32_bcsh_s32(ap, 0x5c + 4 * i);
		}
		t32_bcsh_hue(hue, mat, m1, m2, hm);
		for (i = 0; i < 9; i++)
			t32_bcsh_p32(cp, 0xdc + 4 * i, (u32)hm[i]);
	}
	if (mode == 0 || mode == 3) {
		int v, d;

		d = (int)t32_tp_le16(sec + 0xd0) - (int)t32_tp_le16(sec + 0xce);
		t32_bcsh_p16(cp, 6, t32_bcsh_clip(d > 0 ? ((d >> 1) + 0x1000) / d : 0, 0, 0x3ff));
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
	(void)i;
}

/* tisp_bcsh_write_reg: all registers of the block; returns the number of writes */
static inline unsigned int t32_bcsh_write(const struct t32_bcsh *st, struct t32_front_write *w)
{
	const u8 *sec = st->sec, *cp = st->cp;
	unsigned int n = 0;
	int k;

	/* tisp_bcsh_write_reg */
#define P(i)		t32_tp_le16(cp + 2 * (i))
#define HM(i)		(t32_front_le32(cp + 0xdc + 4 * (i)) & 0x3fff)
	T32_FWR(w, n, 0xf040, (P(0x8a) & 0x3ff) << 16 | (P(0x8d) & 0x3ff));
	T32_FWR(w, n, 0xf044, (P(0x8b) & 0x3ff) << 16 | (P(0x8e) & 0x3ff));
	T32_FWR(w, n, 0xf048, (P(0x8c) & 0x3ff) << 16 | (P(0x8f) & 0x3ff));
	T32_FWR(w, n, 0xf04c, (P(0x90) & 0x3ff) << 16 | (P(0x93) & 0x3ff));
	T32_FWR(w, n, 0xf050, (P(0x91) & 0x3ff) << 16 | (P(0x94) & 0x3ff));
	T32_FWR(w, n, 0xf054, (P(0x92) & 0x3ff) << 16 | (P(0x95) & 0x3ff));
	T32_FWR(w, n, 0xf058, (P(0x87) & 0x7ff) << 16 | (P(0x84) & 0x7ff));
	T32_FWR(w, n, 0xf05c, (P(0x88) & 0x7ff) << 16 | (P(0x85) & 0x7ff));
	T32_FWR(w, n, 0xf060, (P(0x89) & 0x7ff) << 16 | (P(0x86) & 0x7ff));
	T32_FWR(w, n, 0xf064, HM(1) << 16 | HM(0));
	T32_FWR(w, n, 0xf068, HM(2));
	T32_FWR(w, n, 0xf06c, HM(4) << 16 | HM(3));
	T32_FWR(w, n, 0xf070, HM(5));
	T32_FWR(w, n, 0xf074, HM(7) << 16 | HM(6));
	T32_FWR(w, n, 0xf078, HM(8));
	T32_FWR(w, n, 0xf07c, (P(2) & 0x3ff) << 16 | (sec[0x45a] & 1U));
	T32_FWR(w, n, 0xf080, (P(1) & 0x3ff) << 16 | (P(0) & 0x3ff));
	T32_FWR(w, n, 0xf084, (P(3) & 0x3ff) << 16 | (sec[0x45b] & 1U));
	T32_FWR(w, n, 0xf088, (t32_tp_le16(sec + 0xd0) & 0x3ff) << 16 | (t32_tp_le16(sec + 0xce) & 0x3ff));
	T32_FWR(w, n, 0xf08c, (P(0x15) & 0x3ff) << 16 | (cp[0x24] & 1U));
	T32_FWR(w, n, 0xf090, (P(0x14) & 0x3ff) << 16 | (P(0x13) & 0x3ff));
	for (k = 0; k < 4; k++)
		T32_FWR(w, n, 0xf094 + 4 * k, (P(0x40 + 2 * k) & 0x3ff) << 16 | (P(0x3f + 2 * k) & 0x3ff));
	T32_FWR(w, n, 0xf0a4, (cp[0x48] & 1U) << 16 | (P(0x47) & 0x3ff));
	for (k = 0; k < 4; k++)
		T32_FWR(w, n, 0xf0a8 + 4 * k, (P(0x49 + 2 * k) & 0x3ff) << 16 | (P(0x48 + 2 * k) & 0x3ff));
	T32_FWR(w, n, 0xf0b8, P(0x50) & 0x3ff);
	for (k = 0; k < 4; k++)
		T32_FWR(w, n, 0xf0bc + 4 * k, (P(0x52 + 2 * k) & 0x7fff) << 16 | (P(0x51 + 2 * k) & 0x7fff));
	T32_FWR(w, n, 0xf0cc, (P(0x22) & 0x1fffU) << 3 | (P(0x23) & 0x1fffU) << 16 | (cp[0x2c] & 1U));
	T32_FWR(w, n, 0xf0d0, (P(0x18) & 0x3ff) << 16 | (P(0x17) & 0x3ff));
	T32_FWR(w, n, 0xf0d4, (P(0x1e) & 0x1fffU) << 16 | (P(0x1f) & 0x1fff));
	T32_FWR(w, n, 0xf0d8, (P(0x20) & 0x1fffU) << 16 | (P(0x21) & 0x1fff));
	T32_FWR(w, n, 0xf0dc, (P(5) & 0x3ff) << 16 | (P(4) & 0x3ff));
	T32_FWR(w, n, 0xf0e0, (P(7) & 0x3ff) << 16 | (P(6) & 0x3ff));
	T32_FWR(w, n, 0xf0e4, (sec[0x465] & 1U) << 16 | (P(8) & 0x3ff));
	T32_FWR(w, n, 0xf0e8, (P(10) & 0x3ff) << 16 | (P(9) & 0x3ff));
	T32_FWR(w, n, 0xf0ec, (P(12) & 0x3ff) << 16 | (P(11) & 0x3ff));
	T32_FWR(w, n, 0xf0f0, P(13) & 0x3ff);
	T32_FWR(w, n, 0xf0f4, t32_front_le32(cp + 0x1c));
	T32_FWR(w, n, 0xf0f8, t32_front_le32(cp + 0x20));
	T32_FWR(w, n, 0xf0fc, (cp[0xb3] & 0x3fU) << 4 | (P(0x5a) & 0x3ffU) << 16 | (cp[0xb2] & 3U));
#undef P
#undef HM
	return n;
}

/*
 * tisp_bcsh_set_write_reg_event: a write is requested once both update paths ran
 * (their counters have the same parity).
 */
static inline void t32_bcsh_set_write_event(struct t32_bcsh *st)
{
	if ((st->ctr_ev ^ st->ctr_ct) & 1)
		return;
	st->pend = st->pend < 16 ? st->pend + 1 : 1;
}

/* tisp_bcsh_irq_frame_down: the pending request is written at the frame end */
static inline int t32_bcsh_frame_done(struct t32_bcsh *st, struct t32_front_write *w)
{
	if (!st->pend)
		return 0;
	st->pend = 0;
	return (int)t32_bcsh_write(st, w);
}

/*
 * tisp_bcsh_ev_update: the exposure changed.  Exposure limited to 256 << 32; with
 * section byte 0x4b1 set the caller's raw lo2:hi2 is interpolated, forced.  The
 * counter of this path counts every call; a recomputation refreshes the derived
 * parameters (mode 1) and requests a write.
 */
static inline void t32_bcsh_ev_update(struct t32_bcsh *st, u32 lo, u32 hi, u32 lo2, u32 hi2)
{
	u32 rc;

	if (hi > 256 || (hi == 256 && lo != 0)) {
		lo = 0;
		hi = 256;
	}
	rc = st->sec[0x4b1] ? t32_bcsh_ev_interp(st, lo2, hi2, 1) : t32_bcsh_ev_interp(st, lo, hi, 0);
	st->ctr_ev = st->ctr_ev != 255 ? st->ctr_ev + 1 : 0;
	if (rc)
		return;
	t32_bcsh_calc(st, 1);
	t32_bcsh_set_write_event(st);
}

/* tisp_bcsh_ct_update: the colour temperature changed (not forced), mode 2 refresh */
static inline void t32_bcsh_ct_update(struct t32_bcsh *st, u32 ct)
{
	u32 rc = t32_bcsh_ct_interp(st, ct, 0);

	st->ctr_ct = st->ctr_ct != 255 ? st->ctr_ct + 1 : 0;
	if (rc)
		return;
	t32_bcsh_calc(st, 2);
	t32_bcsh_set_write_event(st);
}

/*
 * tisp_bcsh_init with the state kept.  sec: writable copy of section 19 (the block
 * clears ten of its bytes), lut: the knots behind it (lutbytes long), csc: the colour
 * space section.  Returns the number of writes (49), -1 on a refused section.
 */
static inline int t32_bcsh_start(struct t32_bcsh *st, u8 *sec, const u8 *lut, u32 lutbytes, const u8 *csc,
				 struct t32_front_write *w)
{
	static const u16 clear[] = { 0x49e, 0x49f, 0x467, 0x468, 0x461, 0x462, 0x498, 0x499, 0x493, 0x494 };
	u8 *ap = st->ap;
	unsigned int n;
	int k;

	if (!sec[0x4af] || lutbytes < (u32)sec[0x4af] * 40U)
		return -1;	/* the vendor would read beyond the knot table */
	memset(st, 0, sizeof(*st));
	st->sec = sec;
	st->lut = lut;
	st->lutbytes = lutbytes;
	for (k = 0; k < 36; k++)
		t32_bcsh_p32(ap, 4 * k, t32_bcsh_ap_default[k]);
	st->ct = T32_BCSH_CT;
	st->ct_code = 255;

	/* tisp_csc_api_get into the user/csc set; the four user settings start neutral */
	memcpy(st->cs, csc, T32_BCSH_CSC_BYTES);
	st->cs[0x62] = 0;		/* tisp_csc_init clears the two path bytes of its copy */
	st->cs[0x63] = 0;
	if (st->cs[0x5b] > 5)
		return -1;
	t32_bcsh_csc_apply(st->cs, st->cs[0x5b]);
	csc = st->cs;
	memset(ap, 0x80, 4);
	t32_bcsh_p32(ap, 0x2c, csc[0x5b]);
	memcpy(ap + 0x54, csc + 0x52, 6);
	memcpy(ap + 0x80, csc + 0x5c, 6);
	memcpy(ap + 0x30, csc + 4, 0x24);
	memcpy(ap + 0x5c, csc + 0x28, 0x24);
	for (k = 0; k < (int)(sizeof(clear) / sizeof(clear[0])); k++)
		sec[clear[k]] = 0;

	t32_bcsh_new_para(st);
	t32_bcsh_ev_interp(st, 0, 0, 1);
	t32_bcsh_ct_interp(st, st->ct, 1);
	t32_bcsh_calc(st, 0);
	n = t32_bcsh_write(st, w);
	T32_FWR(w, n, 0xf000, T32_BCSH_ID);
	return (int)n;
}

#ifdef __KERNEL__
#define T32_BCSH_FN	static noinline_for_stack int
#else
#define T32_BCSH_FN	static inline int
#endif
T32_BCSH_FN t32_bcsh_init(u8 *sec, const u8 *lut, u32 lutbytes, const u8 *csc, struct t32_front_write *w)
{
	struct t32_bcsh st;

	return t32_bcsh_start(&st, sec, lut, lutbytes, csc, w);
}

#endif /* TX_ISP_T32_BCSH_H */
