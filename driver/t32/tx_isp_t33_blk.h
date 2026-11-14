/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) ISP blocks programmed through their register lists
 * (tx_isp_t32_rcfg33.h), from the PRJ008 IQ bin.
 *
 * Each T33 module list has a fixed layout (the commands of the vendor's
 * tisp_rcfg_<block>_reg_init); the block code fills the values in it.  Here
 * a layout is a table of commands { first register, count, port } and the
 * block functions return the register writes, which t33_rcfg_build_tmpl
 * puts into the layout (the k-th write of a register fills its k-th slot).
 *
 * Section pointers: the vendor's tparamsP holds the 28 sections back to
 * back from byte 4 on (no payloads); a block's "tparamsP + X" is the IQ bin
 * section whose cumulative length + 4 is X (YDNS 0x88ac = section 17, SDNS
 * 0x9b20 = 22, BLC 0x44e8 = 8 for the PRJ008 sizes).  The section pointer
 * includes the 4-byte section id.
 *
 * Gains are 16.16 (index = integer part into the 11-point tables); at init
 * the vendor uses the init gain 0x10000.  QEMU trace of the vendor module
 * (openimp #388): YDNS (list 17), SDNS (20) and BLC (4) word-equal.
 */
#ifndef TX_ISP_T33_BLK_H
#define TX_ISP_T33_BLK_H

#include "tx_isp_t32_rcfg33.h"
#include "tx_isp_t32_tp.h"
#include "tx_isp_t32_iq.h"

struct t33_tcmd {
	u32 reg;	/* first register */
	u16 n;		/* values */
	u16 port;	/* 1: all values to reg (bit 31 clear) */
};

#define T33_TMPL_MAX_WRITES	1024

/*
 * Fill a module layout.  Slot k of register R takes the k-th write to R in
 * w (the last one again when the block wrote R fewer times, 0 when never).
 * Returns the list length in words, -1 on overflow or when a write has no
 * slot in the layout (the block code and the layout disagree).
 */
static inline int t33_rcfg_build_tmpl(u32 *buf, u32 cap, const struct t33_tcmd *t, unsigned int nt,
				      const struct t32_front_write *w, unsigned int n)
{
	static u8 used[T33_TMPL_MAX_WRITES];	/* callers are serialised (stream-on / list refresh) */
	u32 len = 0, r, v;
	unsigned int c, k, i, last;

	if (n > T33_TMPL_MAX_WRITES || cap < 1)
		return -1;
	for (i = 0; i < n; i++)
		used[i] = 0;
	for (c = 0; c < nt; c++) {
		if (!t[c].n || len + 1 + t[c].n + 1 > cap)
			return -1;
		buf[++len] = (t[c].port ? 0 : T32_RCFG_CMD) | (u32)(t[c].n - 1) << T32_RCFG_COUNT_SHIFT |
			     t[c].reg >> 2;
		for (k = 0; k < t[c].n; k++) {
			r = t[c].reg + (t[c].port ? 0 : 4 * k);
			v = 0;
			last = n;
			for (i = 0; i < n; i++) {
				if (w[i].reg != r)
					continue;
				if (!used[i])
					break;
				last = i;
			}
			if (i < n) {
				used[i] = 1;
				v = w[i].val;
			} else if (last < n) {
				v = w[last].val;
			}
			buf[++len] = v;
		}
	}
	for (i = 0; i < n; i++)
		if (!used[i] && w[i].reg & 0xfff)	/* block id writes (0x?000) have no slot */
			return -1;
	buf[0] = len;
	return (int)len;
}

/*
 * The vendor's own fill: tisp_rcfg_<blk>_reg_init writes the command words
 * of the layout, and the block code stores each value at its slot
 * (tisp_reg_write_t33(id, list, slot, val): word slot + 1 of the list, the
 * <blk>CfgMode path; the last store to a slot wins).  t33_rcfg_tmpl_init
 * writes the layout with all values 0 and returns its length in words.
 */
static inline int t33_rcfg_tmpl_init(u32 *buf, u32 cap, const struct t33_tcmd *t, unsigned int nt)
{
	u32 len = 0;
	unsigned int c, k;

	for (c = 0; c < nt; c++) {
		if (!t[c].n || len + 1 + t[c].n + 1 > cap)
			return -1;
		buf[++len] = (t[c].port ? 0 : T32_RCFG_CMD) | (u32)(t[c].n - 1) << T32_RCFG_COUNT_SHIFT |
			     t[c].reg >> 2;
		for (k = 0; k < t[c].n; k++)
			buf[++len] = 0;
	}
	buf[0] = len;
	return (int)len;
}

#define T33_SW(l, s, x)	((l)[(s) + 1] = (x))

/* tparamsP offset of section idx (vendor block pointers), from the bin's table */
static inline u32 t33_tp_offset(const u8 *bin, unsigned int idx)
{
	u32 off = 4;
	unsigned int i;

	for (i = 0; i < idx && i < T32_IQ_SECTIONS; i++) {
		const u8 *e = bin + T32_IQ_TABLE + 4 * i;

		if ((e[0] | e[1] << 8) == 1)
			off += e[2] | e[3] << 8;
	}
	return off;
}

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a)	(sizeof(a) / sizeof((a)[0]))
#endif

/*
 * Block state as the vendor keeps it: a word array (the vendor's kmalloc'ed
 * <blk>Info, indexed by word).  Section copies and gain interpolations of the
 * vendor's tisp_<blk>_bin_par_update / tisp_<blk>_par_refresh are tables of
 * { bits (8 / 16), section byte offset, word }.
 */
#define T33_VB_S8	0x88	/* signed section byte / half word */
#define T33_VB_S16	0x90
struct t33_vmap {
	u8 bits;
	u16 off;
	u16 word;
};

/* v[word] = v[from] << shift + add (func_<blk>_para_update copies) */
struct t33_vcopy {
	u16 word;
	u16 from;
	u8 shift;
	u8 add;
};

static inline void t33_vb_bin(u32 *v, const u8 *s, const struct t33_vmap *m, unsigned int n)
{
	unsigned int k;

	for (k = 0; k < n; k++)
		v[m[k].word] = m[k].bits == 8 ? s[m[k].off] :
			       m[k].bits == 16 ? t32_tp_le16(s + m[k].off) :
			       m[k].bits == T33_VB_S8 ? (u32)(int)(signed char)s[m[k].off] :
			       (u32)(int)(short)t32_tp_le16(s + m[k].off);
}

static inline void t33_vb_intp(u32 *v, const u8 *s, u32 gain, const struct t33_vmap *m, unsigned int n)
{
	u32 i = gain >> 16, f = gain & 0xffff;
	unsigned int k;

	for (k = 0; k < n; k++)
		v[m[k].word] = m[k].bits == 8 ? t32_tp_intp_u8(i, f, s + m[k].off) :
						t32_tp_intp_u16(i, f, s + m[k].off);
}

static inline void t33_vb_copy(u32 *v, const struct t33_vcopy *c, unsigned int n)
{
	unsigned int k;

	for (k = 0; k < n; k++)
		v[c[k].word] = (v[c[k].from] << c[k].shift) + c[k].add;
}

static inline int t33_iabs(int x)
{
	return x < 0 ? -x : x;
}

/*
 * func_<blk>_isp_itp_update_ks via isp_<blk>_update_value_ks (smax - 1, bits - 1, 1):
 * the largest shift smax..2 with 0 <= (a << shift + b / 2) / b < 2^bits.
 */
static inline void t33_itp_ks(int a, int b, u32 bits, int smax, u32 *val, u32 *shift)
{
	u32 max = (1U << bits) - 1, q;
	int s;

	*val = 0;
	*shift = 0;
	if (b < 1)
		return;
	if (b == 1) {
		*shift = 2;
		return;
	}
	for (s = smax; s >= 2; s--) {
		q = (u32)((int)((a << s) + (b >> 1)) / b);
		if ((int)q < 0 || q <= max) {
			*val = q;
			*shift = (u32)s;
			return;
		}
	}
}

static inline u32 t33_le32(const u8 *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24;
}

static inline int t33_s32(const u8 *p)
{
	return (int)t33_le32(p);
}

/* tisp_sub_interp: a + (b - a) * f / 4096, rounded */
static inline int t33_sub_interp(u32 f, int a, int b)
{
	u32 x = (u32)(b - a) * f;

	return a + ((int)x >> 12) + (int)((x >> 11) & 1);
}

/*
 * tisp_ev_intp_uint8_t: 11-point table t over the EV points e (11 LE u32);
 * EV at or above the last point gives t[10], below the first t[0].
 */
static inline u8 t33_ev_intp_u8(u32 ev, const u8 *e, const u8 *t)
{
	u32 k, lo, hi, d;
	unsigned long long f;

	for (k = 0; k < 11; k++)
		if (ev < t33_le32(e + 4 * k))
			break;
	if (k == 11)
		return t[10];
	if (!k)
		return t[0];
	hi = t33_le32(e + 4 * k);
	lo = t33_le32(e + 4 * k - 4);
	d = hi - lo;
	if (!d)
		return 2;
	f = (unsigned long long)(ev - lo) * 0x1000 + (d >> 1);
	return (u8)t33_sub_interp((u32)t32_tp_div64(f, (unsigned long long)d), t[k - 1], t[k]);
}

static inline void t33_vb_evintp(u32 *v, const u8 *s, u32 ev, const u8 *e, const struct t33_vmap *m,
				 unsigned int n)
{
	unsigned int k;

	for (k = 0; k < n; k++)
		v[m[k].word] = t33_ev_intp_u8(ev, e, s + m[k].off);
}

/* func_mdns_fixpoint_sqrt: Newton from g (at most iters steps), then rounded */
static inline int t33_fix_sqrt(int x, int g, int iters)
{
	int k, y, c;

	if (x < 2)
		return x;
	for (k = 0; k < iters; k++) {
		y = (x / g + g + 1) / 2;
		if (y == g)
			break;
		g = y;
	}
	c = 2 * g + 1;
	if (c * c / 4 <= x)
		return g + 1;
	c = 2 * g - 1;
	return g - (x < c * c / 4);
}

/* cti_func_slope_calc: (0x100 - x) * 0x1000 / y as mantissa (<= 0x100) and shift */
static inline void t33_slope(int x, int y, u32 *val, u32 *shift)
{
	int q, k;

	if (y < 1)
		y = 1;
	q = ((0x100 - x) * 0x1000 + (y >> 1)) / y;
	if (q < 0x101) {
		*shift = 12;
		*val = (u32)q;
		return;
	}
	for (k = 1; k <= 12; k++)
		if (q <= 0x100 << k) {
			*shift = (u32)(12 - k);
			*val = (u32)((q + (1 << (k - 1))) >> k);
			return;
		}
}

#define T33_FWR(w, n, r, v) do { (w)[(n)].reg = (r); (w)[(n)].val = (v); (n)++; } while (0)

/* ---- YDNS, module 17 (section 17, 128 bytes) ---------------------------- */
#define T33_MOD_YDNS		17
#define T33_SEC_YDNS		17
#define T33_YDNS_SEC_BYTES	128U
static const struct t33_tcmd t33_ydns_tmpl[] = {
	{ 0x0e010, 1, 0 }, { 0x0e050, 4, 0 },
};

/* tisp_ydns_par_refresh (+ func_ydns_bin_para_update / func_ydns_para_update) */
static inline void t33_ydns_par(const u8 *sec, u32 gain, u32 *p)
{
	u32 i = gain >> 16, f = gain & 0xffff;
	unsigned int k;

	p[0] = sec[4];
	for (k = 0; k < 11; k++)
		p[1 + k] = t32_tp_intp_u8(i, f, sec + 5 + 11 * k);
}

/* ydnsInfo (0x6c bytes): word 0 the bin byte, 1..11 the interpolations, 12..23 func_ydns_para_update's copies */
#define T33_YDNS_INFO_WORDS	27
static inline void t33_ydns_info(const u8 *sec, u32 gain, u32 *inf)
{
	unsigned int k;

	t33_ydns_par(sec, gain, inf + 12);
	for (k = 0; k < 12; k++)
		inf[k] = inf[12 + k];
}

static inline int t33_ydns_init(const u8 *sec, u32 gain, struct t32_front_write *w)
{
	u32 p[12];
	unsigned int n = 0;

	t33_ydns_par(sec, gain, p);
	T33_FWR(w, n, 0xe054, (p[2] & 3) << 8 | (p[3] & 3) << 12 | (p[1] & 0xff));
	T33_FWR(w, n, 0xe058, (p[4] & 0xff) | p[7] << 24 | (p[5] & 0xff) << 8 | (p[6] & 0xff) << 16);
	T33_FWR(w, n, 0xe05c, (p[8] & 0xff) | p[11] << 24 | (p[9] & 0xff) << 8 | (p[10] & 0xff) << 16);
	T33_FWR(w, n, 0xe050, p[0] & 1);
	T33_FWR(w, n, 0xe010, 1);
	return (int)n;
}

/* ---- SDNS, module 20 (section 22, 152 bytes) ---------------------------- */
#define T33_MOD_SDNS		20
#define T33_SEC_SDNS		22
#define T33_SDNS_SEC_BYTES	152U
static const struct t33_tcmd t33_sdns_tmpl[] = {
	{ 0x13010, 1, 0 }, { 0x13020, 7, 0 },
};

/* sdns_dns_str_quantify: 0..109 identity, then a coarser tail */
static const u8 t33_sdns_quant_tail[18] = {
	111, 113, 115, 117, 119, 121, 123, 125, 127, 130, 133, 136, 140, 144, 155, 170, 195, 251,
};
/* sdns_func_val_lut_int weights: the vendor's are these, negated, as s64 (<< 23) */
static const u32 t33_sdns_w[4] = { 1120142, 3942676, 8227792, 17443619 };

static inline u32 t33_sdns_quant(u32 v)
{
	if (v < 110)
		return v;
	return v < 128 ? t33_sdns_quant_tail[v - 110] : 0;
}

/* the vendor's integer square root: Newton from 1 (at most 15 steps), then rounded */
static inline int t33_isqrt_round(int v)
{
	int x = 1, y = 1, k, c;

	if (v < 2)
		return v;
	for (k = 0; k < 15; k++) {
		y = (v / x + x + 1) / 2;
		if (y == x)
			break;
		x = y;
	}
	x = y;
	c = 2 * x + 1;
	if (v < c * c / 4) {
		c = 2 * x - 1;
		return x - (v < c * c / 4);
	}
	return x + 1;
}

/* tisp_sdns_par_refresh (+ other_params_transform, func_sdns_para_update) */
static inline void t33_sdns_par(const u8 *sec, u32 gain, u32 *p, u32 *s)
{
	static const u8 tab[13][2] = {
		{ 1, 0x05 }, { 2, 0x10 }, { 4, 0x1c }, { 5, 0x27 }, { 6, 0x32 }, { 7, 0x3d }, { 8, 0x48 },
		{ 9, 0x53 }, { 10, 0x5e }, { 11, 0x69 }, { 13, 0x75 }, { 14, 0x80 }, { 16, 0x8c },
	};
	u32 q, i = gain >> 16, f = gain & 0xffff;
	unsigned int k;
	int v;

	p[0] = sec[4];
	p[3] = sec[0x1b];
	p[12] = sec[0x74];
	p[15] = sec[0x8b];
	for (k = 0; k < 13; k++)
		p[tab[k][0]] = t32_tp_intp_u8(i, f, sec + tab[k][1]);
	q = t33_sdns_quant(p[2]);
	q *= q;
	for (k = 0; k < 4; k++) {
		/* p1^2 - floor(-w * q / 2^23), as the vendor's 64-bit product */
		v = (int)(p[1] * p[1] + (u32)(((unsigned long long)t33_sdns_w[k] * q + 0x7fffffULL) >> 23));
		s[k] = v < 0xfe01 ? (u32)t33_isqrt_round(v) : 0xff;
	}
}

/* sdnsInfo (160 bytes): func_sdns_para_update copies p[0], p[3..16] to words 0x11, 0x12..0x1f, s to 0x20..0x23 */
#define T33_SDNS_INFO_WORDS	40
static inline void t33_sdns_info(const u8 *sec, u32 gain, u32 *inf)
{
	u32 p[17], s[4];
	unsigned int k;

	t33_sdns_par(sec, gain, p, s);
	inf[0x11] = p[0];
	for (k = 3; k <= 16; k++)
		inf[0x12 + k - 3] = p[k];
	for (k = 0; k < 4; k++)
		inf[0x20 + k] = s[k];
}

static inline int t33_sdns_init(const u8 *sec, u32 gain, struct t32_front_write *w)
{
	u32 p[17], s[4];
	unsigned int n = 0;

	t33_sdns_par(sec, gain, p, s);
	/* func_sdns_para_update copies p[0], p[3..16] to 0x11, 0x12..0x1f */
	T33_FWR(w, n, 0x13028, (p[4] & 0xff) | p[7] << 24 | (p[5] & 0xff) << 8 | (p[6] & 0xff) << 16);
	T33_FWR(w, n, 0x1302c, (p[8] & 0xff) | p[11] << 24 | (p[9] & 0xff) << 8 | (p[10] & 0xff) << 16);
	T33_FWR(w, n, 0x13030, (p[13] & 0xff) << 8 | (p[14] & 0xff) << 16 | (p[12] & 0x3f) | (p[15] & 0x3f) << 24);
	T33_FWR(w, n, 0x13034, p[16] & 0x1f);
	T33_FWR(w, n, 0x13038, (s[0] & 0xff) | s[3] << 24 | (s[1] & 0xff) << 8 | (s[2] & 0xff) << 16);
	T33_FWR(w, n, 0x13020, p[0] & 3);
	T33_FWR(w, n, 0x13024, p[3] & 3);
	T33_FWR(w, n, 0x13010, 1);
	return (int)n;
}

/* ---- BLC, module 4 (section 8, 136 bytes) ------------------------------- */
#define T33_MOD_BLC		4
#define T33_SEC_BLC		8
#define T33_BLC_SEC_BYTES	136U
static const struct t33_tcmd t33_blc_tmpl[] = {
	{ 0x03010, 1, 0 }, { 0x03044, 6, 0 },
};

/*
 * tisp_blc_init + tisp_blc_interp_by_tgain + tisp_blc_write_reg: five black
 * levels (11-point u16 tables at +8, +0x1e, +0x34, +0x4a, +0x60, or the
 * manual values at +0x76 when the u32 at +4 is set), placed per Bayer order
 * (bayer & 0x1f, 0..3), and the gain 0x1000 / (0x1000 - max level).
 */
static inline int t33_blc_init(const u8 *sec, u32 bayer, u32 gain, struct t32_front_write *w)
{
	/* channel byte offsets (0x20 + 2 * index) of the eight slots per Bayer order */
	static const u8 map[4][8] = {
		{ 0, 1, 0, 1, 2, 3, 2, 3 },
		{ 3, 2, 3, 2, 1, 0, 1, 0 },
		{ 1, 0, 1, 0, 3, 2, 3, 2 },
		{ 2, 3, 2, 3, 0, 1, 0, 1 },
	};
	u32 lv[5], max = 0, i = gain >> 16, f = gain & 0xffff, g, idx = bayer & 0x1f;
	unsigned int n = 0, k;
	const u8 *m;

	if (idx > 3)
		return -1;
	m = map[idx];
	for (k = 0; k < 5; k++) {
		if (!t32_tp_le24(sec + 4) && !sec[7])
			lv[k] = t32_tp_intp_u16(i, f, sec + 8 + 0x16 * k);
		else
			lv[k] = t32_tp_le16(sec + 0x76 + 2 * k);
		if (lv[k] > max)
			max = lv[k];
	}
	g = 0x1fff;
	if (!(max >> 11)) {
		g = 0x2000000U / (0x1000U - max);
		g = (g >> 1) + (g & 1);		/* tisp_round_int64(x, 0, 1) */
	}
	T33_FWR(w, n, 0x3044, (lv[m[1]] & 0xfff) << 16 | (lv[m[0]] & 0xfff));
	T33_FWR(w, n, 0x3048, (lv[m[3]] & 0xfff) << 16 | (lv[m[2]] & 0xfff));
	T33_FWR(w, n, 0x3058, g & 0x1fff);
	T33_FWR(w, n, 0x304c, (lv[m[5]] & 0xfff) << 16 | (lv[m[4]] & 0xfff));
	T33_FWR(w, n, 0x3050, (lv[m[7]] & 0xfff) << 16 | (lv[m[6]] & 0xfff));
	T33_FWR(w, n, 0x3054, sec[0x80] & 1);
	T33_FWR(w, n, 0x3010, 3);
	return (int)n;
}

#undef T33_FWR

#endif /* TX_ISP_T33_BLK_H */
