/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 MSCA channel configuration callees of tisp_msca_t33_chx_cfg_load (hand port, vendor form; binary oracle
 * blocks "mscacur" and "mscanorm"; openimp #99, #643):
 *
 *  - tisp_msca_t33_scaling_algorithm (id): the scaler filter enables (param block bytes 211..216, bit 0 each)
 *    to slot 6 (mscaCfgMode 0: 0xf004c), then slot 1 = 1 (0xf0010) as a tail call;
 *  - tisp_msca_t33_curve_calc (id, ch): the channel's steps in << 14 / out into the param block
 *    (words 0x1c / 0x20 + 0x20 * ch), the ratios out << 14 / in (crop size of the param block when its flag
 *    halfword 0xb8 + 2 * ch is set; at most 0x6aaa), tisp_sin and the 2/3/4/6-point normalisation of the four
 *    coefficient sets in msca_info[id] (bytes 8.., 0x70.., 0xf0..);
 *  - tisp_msca_t33_ch_curve_write (id, ch): the four sets packed two 13-bit taps per word into the channel's
 *    slots (0xcc.. / 0xe9.. / 0x10c.., mscaCfgMode 0: port 0xf0560 + 0x10 * ch); the first words of each set
 *    come from the user curve of the param block (bytes 100.. / 124.. / 154..) when the set's flag byte is 1,
 *    and are not written at all while the channel's custom flag (byte 202 + ch) is not 1;
 *  - tisp_msca_t33_ctrl_reg_write (id, ch): the channel's sizes from stMscaAttr[id] (22 bytes per channel),
 *    its steps, 0xf0128, the strides (16-aligned crop width, 0x1000 for channel 0 when register 0x100 reads
 *    5; written direct with stIspInitPar word 226 == 1), slot 1 = 1; with word 226 == 1 then
 *    tisp_msca_t33_chx_en (id, ch, 1) and, when list 23 is idle, tisp_rcfg_module_cfg_trig (id, 23) as a tail
 *    call, else tispMscaRefresh = 1;
 *  - tisp_msca_t33_two_point_normalized / _thr_point_ / _four_ponit_ / _six_point_normalized (n, taps, c):
 *    8-phase normalisation of c[] in place (taps of phase p at |pos[t] - p|, each phase summing to 2048, the
 *    rounding error added / taken in the vendor's tap order), then the vendor's sum check (isp_printf).
 *
 * Callees and module state are passed in struct t33_mscacur.  All four vendor functions end in tail calls
 * (tisp_reg_write_t33, tisp_rcfg_module_cfg_trig, tisp_msca_t33_two_point_normalized), here plain calls.
 * The return value of ctrl_reg_write is not meaningful in the vendor code (the refresh path returns the high
 * half of tispMscaRefresh's address); here it is the trigger's value or 1.
 */
#ifndef TX_ISP_T33_MSCACUR_H
#define TX_ISP_T33_MSCACUR_H

#include "tx_isp_t33_blk.h"

int isp_printf(unsigned int level, const char *fmt, ...);

struct t33_mscacur {
	u8 *const *msca_info;	/* msca_info[]: the info block (word 0 = the MSCA parameter block, curves at 8..) */
	u8 *const *attr;	/* stMscaAttr[]: 22 bytes per channel */
	const u8 *cfg_mode;	/* mscaCfgMode */
	const u8 *init_par;	/* stIspInitPar (word 226 at byte 904) */
	u8 *refresh;		/* tispMscaRefresh */
	int (*reg_write)(u32 id, u32 mod, u32 reg, u32 val);	/* tisp_reg_write_t33 */
	void (*sin)(u32 n, u32 cnt, short *out);		/* tisp_sin */
	void (*two)(u32 n, u32 taps, short *c);			/* tisp_msca_t33_two_point_normalized */
	void (*thr)(u32 n, u32 taps, short *c);			/* tisp_msca_t33_thr_point_normalized */
	void (*four)(u32 n, u32 taps, short *c);		/* tisp_msca_t33_four_ponit_normalized */
	void (*six)(u32 n, u32 taps, short *c);			/* tisp_msca_t33_six_point_normalized */
	void (*sys_write)(u32 reg, u32 val);			/* system_reg_write */
	u32 (*sys_read)(u32 reg);				/* system_reg_read */
	void (*chx_en)(u32 id, u32 ch, u32 en);			/* tisp_msca_t33_chx_en */
	int (*dma_state)(u32 mod);				/* tisp_rcfg_get_dma_state */
	int (*cfg_trig)(u32 id, u32 mod);			/* tisp_rcfg_module_cfg_trig */
};

static inline u32 t33_mc_h(const u8 *p, u32 off)
{
	return *(const u16 *)(p + off);
}

static inline u32 t33_mc_w(const u8 *p, u32 off)
{
	return *(const u32 *)(p + off);
}

static inline u8 *t33_mc_par(const struct t33_mscacur *m, u32 id)
{
	return *(u8 **)m->msca_info[id];
}

/* tisp_msca_t33_scaling_algorithm */
static inline void t33_msca_scaling_algorithm_v(const struct t33_mscacur *m, u32 id)
{
	const u8 *p = t33_mc_par(m, id);
	u32 v = (p[211] & 1) | (p[213] & 1) << 1 | (p[215] & 1) << 2 | (p[212] & 1) << 4 | (p[214] & 1) << 5 |
		(p[216] & 1) << 6;

	m->reg_write(id, 23, *m->cfg_mode ? 6 : 0xf004c, v);
	m->reg_write(id, 23, *m->cfg_mode ? 1 : 0xf0010, 1);
}

/* tisp_msca_t33_curve_calc */
static inline void t33_msca_curve_calc_v(const struct t33_mscacur *m, u32 id, u32 ch)
{
	u32 i = id & 0xff, c = ch & 0xff, rh, rv, w, h;
	u8 *inf = m->msca_info[i], *p, *e;

	if (c > 2)
		return;
	e = m->attr[i] + 22 * c;
	p = t33_mc_par(m, i);
	*(u32 *)(p + 0x1c + 0x20 * c) = (t33_mc_h(e, 4) << 14) / t33_mc_h(e, 12);
	*(u32 *)(p + 0x20 + 0x20 * c) = (t33_mc_h(e, 2) << 14) / t33_mc_h(e, 10);
	p = t33_mc_par(m, i);
	h = t33_mc_h(e, 4);
	w = t33_mc_h(e, 2);
	if (t33_mc_h(p, 0xb8 + 2 * c)) {
		rh = (t33_mc_h(p, 0xbe + 4 * c) << 14) / w;
		rv = (t33_mc_h(p, 0xc0 + 4 * c) << 14) / h;
	} else {
		rv = (t33_mc_h(e, 12) << 14) / h;
		rh = (t33_mc_h(e, 10) << 14) / w;
	}
	if (rv > 0x6aaa)
		rv = 0x6aaa;
	if (rh > 0x6aaa)
		rh = 0x6aaa;
	if (c == 0) {
		m->sin(rh, 16, (short *)(inf + 0x2a));
		m->four(8, 4, (short *)(inf + 0x2a));
		m->sin(rv, 16, (short *)(inf + 8));
		m->four(8, 4, (short *)(inf + 8));
		m->sin(rh, 8, (short *)(inf + 0x5e));
		m->two(8, 2, (short *)(inf + 0x5e));
		m->sin(rv, 8, (short *)(inf + 0x4c));
		m->two(8, 2, (short *)(inf + 0x4c));
		return;
	}
	inf += c == 1 ? 0 : 0x80;
	m->sin(rh, 24, (short *)(inf + 0x92));
	m->six(8, 6, (short *)(inf + 0x92));
	m->sin(rv, 16, (short *)(inf + 0x70));
	m->four(8, 4, (short *)(inf + 0x70));
	m->sin(rh, 12, (short *)(inf + 0xd6));
	m->thr(8, 3, (short *)(inf + 0xd6));
	m->sin(rv, 8, (short *)(inf + 0xc4));
	m->two(8, 2, (short *)(inf + 0xc4));
}

/*
 * one coefficient set of ch_curve_write: n taps from the info block at byte info (slot slot..), the first head
 * words from the user curve at param byte cust (ncust taps) when the set's flag is 1; the last word of an odd
 * tap count carries its low tap only.
 */
struct t33_msca_cset {
	u16 slot, info;
	u8 n, head, cust, ncust;
};

static const struct t33_msca_cset t33_msca_csets[3][4] = {
	{ { 0xcc, 0x08, 17, 2, 100, 4 }, { 0xd5, 0x2a, 17, 2, 112, 4 },
	  { 0xde, 0x4c, 9, 1, 108, 2 }, { 0xe3, 0x5e, 9, 1, 120, 2 } },
	{ { 0xe9, 0x70, 17, 2, 124, 4 }, { 0xf2, 0x92, 25, 3, 136, 6 },
	  { 0xff, 0xc4, 9, 1, 132, 2 }, { 0x104, 0xd6, 13, 2, 148, 3 } },
	{ { 0x10c, 0xf0, 17, 2, 154, 4 }, { 0x115, 0x112, 25, 3, 166, 6 },
	  { 0x122, 0x144, 9, 1, 162, 2 }, { 0x127, 0x156, 13, 2, 178, 3 } },
};

static inline u32 t33_msca_cword(const u8 *src, u32 k, u32 n)
{
	u32 lo = t33_mc_h(src, 4 * k) & 0x1fff;

	return 2 * k + 1 < n ? (t33_mc_h(src, 4 * k + 2) & 0x1fff) << 16 | lo : lo;
}

/* tisp_msca_t33_ch_curve_write */
static inline void t33_msca_ch_curve_write_v(const struct t33_mscacur *m, u32 id, u32 ch)
{
	const u8 *inf = m->msca_info[id], *p;
	u32 c = ch & 0xff, s, k, slot, port = 0xf0560 + 0x10 * c;

	if (c > 2)
		return;
	for (s = 0; s < 4; s++) {
		const struct t33_msca_cset *q = &t33_msca_csets[c][s];

		slot = q->slot;
		for (k = 0; k < (q->n + 1U) / 2; k++, slot++) {
			u32 v;

			p = t33_mc_par(m, id);
			if (k < q->head) {
				if (p[202 + c] != 1)
					continue;
				v = p[211 + 2 * c + (s & 1)] == 1 ? t33_msca_cword(p + q->cust, k, q->ncust) :
								     t33_msca_cword(inf + q->info, k, q->n);
			} else {
				v = t33_msca_cword(inf + q->info, k, q->n);
			}
			m->reg_write(id, 23, *m->cfg_mode ? slot : port, v);
		}
	}
}

/* tisp_msca_t33_ctrl_reg_write */
static inline int t33_msca_ctrl_reg_write_v(const struct t33_mscacur *m, u32 id, u32 ch)
{
	u32 i = id & 0xff, c = ch & 0xff, b = 30 * c, d = c << 8, cfg = *m->cfg_mode, multi, stride;
	const u8 *e = m->attr[i] + 22 * c;

	multi = t33_mc_w(m->init_par, 904) == 1;
	m->reg_write(i, 23, cfg ? b + 0x1a : (c + 0xf01) << 8, t33_mc_h(e, 6) << 16 | t33_mc_h(e, 8));
	m->reg_write(i, 23, cfg ? (multi ? 2 * (c + 0xca) : b + 0x35) : d + 0xf0170, t33_mc_h(e, 2));
	m->reg_write(i, 23, cfg ? b + 0x1b : d + 0xf0104, t33_mc_h(e, 10) << 16 | t33_mc_h(e, 12));
	m->reg_write(i, 23, cfg ? b + 0x1c : d + 0xf0108, t33_mc_h(e, 18) << 16 | t33_mc_h(e, 20));
	m->reg_write(i, 23, cfg ? b + 0x1d : d + 0xf010c, t33_mc_h(e, 14) << 16 | t33_mc_h(e, 16));
	if (c == 0) {
		m->reg_write(i, 23, cfg ? 0x1e : 0xf0110, t33_mc_w(t33_mc_par(m, i), 0x1c));
		m->reg_write(i, 23, cfg ? 0x1f : 0xf0114, t33_mc_w(t33_mc_par(m, i), 0x20));
	} else if (c == 1 || c == 2) {
		m->reg_write(i, 23, cfg ? b + 0x1e : d + 0xf0110, t33_mc_w(t33_mc_par(m, i), 0x1c + 0x20 * c));
		m->reg_write(i, 23, cfg ? b + 0x1f : d + 0xf0114, t33_mc_w(t33_mc_par(m, i), 0x20 + 0x20 * c));
		m->reg_write(i, 23, cfg ? b + 0x36 : d + 0xf0174, t33_mc_w(t33_mc_par(m, i), 0x2c));
	}
	if (multi)
		m->sys_write(d + 0xf0128, 0);
	else
		m->reg_write(i, 23, cfg ? b + 0x23 : d + 0xf0128, 0);
	m->reg_write(i, 23, cfg ? b + 0x22 : d + 0xf0124, 0xffffffff);
	stride = t33_msca_stride(c, t33_mc_h(e, 14), c == 0 ? m->sys_read(0x100) : 0);
	if (multi) {
		m->sys_write(d + 0xf0140, stride);
		m->sys_write(d + 0xf0160, stride);
	} else {
		m->reg_write(i, 23, cfg ? b + 0x29 : d + 0xf0140, stride);
		m->reg_write(i, 23, cfg ? b + 0x31 : d + 0xf0160, stride);
	}
	m->reg_write(i, 23, cfg ? 1 : 0xf0010, 1);
	if (!multi)
		return 1;
	m->chx_en(i, c, 1);
	if (!m->dma_state(23))
		return m->cfg_trig(i, 23);
	*m->refresh = 1;
	return 1;
}

/*
 * tisp_msca_t33_{two,thr,four,six}_point_normalized core: taps positions pos[], the vendor's add / sub tap
 * orders, out + 1 output entries (at most 25: n <= 8), the function name and line of the sum check message.
 */
static inline void t33_msca_norm_v(u32 n, short *c, const int *pos, int taps, const int *add, const int *sub,
				   int out, const char *fn, int line)
{
	int tmp[25] = { 0 }, tap[6], k[6], p, t, s, err;

	for (p = 0; p != (int)n; p++) {
		s = 0;
		for (t = 0; t < taps; t++) {
			k[t] = pos[t] - p < 0 ? p - pos[t] : pos[t] - p;
			s += c[k[t]];
		}
		err = 0;
		for (t = 0; t < taps; t++) {
			int v = c[k[t]], hv = v < 0 ? -(s / 2) : s / 2;

			tap[t] = (v * 0x800 + hv) / s;
			tmp[k[t]] = tap[t];
			err += tap[t];
		}
		err -= 0x800;
		if (err < 0) {
			for (t = 0; t < -err; t++)
				tap[add[t]]++;
		} else {
			for (t = 0; t < err; t++)
				tap[sub[t]]--;
		}
		for (t = 0; t < taps; t++)
			tmp[k[t]] = tap[t];
	}
	for (p = 0; p != (int)n; p++) {
		s = 0;
		for (t = 0; t < taps; t++)
			s += tmp[pos[t] - p < 0 ? p - pos[t] : pos[t] - p];
		if (((p != 0 && p != (int)n) || ((u32)(s - 0x7ff) & ~2U)) && s != 0x800)
			isp_printf(0, "[ISP][%s, %d]: === error : data_sum = %d; === \n", fn, line, s);
	}
	for (t = 0; t <= out; t++)
		c[t] = (short)tmp[t];
}

/* tisp_msca_t33_two_point_normalized: taps at 0, n */
static inline void t33_msca_two_point_v(u32 n, u32 taps, short *c)
{
	static const int add[2] = { 0, 1 }, sub[2] = { 1, 0 };
	int pos[2] = { 0, (int)n };

	(void)taps;
	t33_msca_norm_v(n, c, pos, 2, add, sub, (int)(n & 0x7fffffff), "tisp_msca_t33_two_point_normalized", 199);
}

/* tisp_msca_t33_thr_point_normalized: taps at n - 3n/2, 2n - 3n/2, 3n - 3n/2 */
static inline void t33_msca_thr_point_v(u32 n, u32 taps, short *c)
{
	static const int add[3] = { 1, 0, 2 }, sub[3] = { 2, 0, 1 };
	u32 h = (n * 3) >> 1;
	int pos[3] = { (int)(n - h), (int)(2 * n - h), (int)(3 * n - h) };

	(void)taps;
	t33_msca_norm_v(n, c, pos, 3, add, sub, (int)h, "tisp_msca_t33_thr_point_normalized", 0x11c);
}

/* tisp_msca_t33_four_ponit_normalized: taps at -n, 0, n, 2n */
static inline void t33_msca_four_point_v(u32 n, u32 taps, short *c)
{
	static const int add[4] = { 1, 2, 0, 3 }, sub[4] = { 3, 0, 2, 1 };
	int pos[4] = { -(int)n, 0, (int)n, (int)(n << 1) };

	(void)taps;
	t33_msca_norm_v(n, c, pos, 4, add, sub, (int)((n & 0x3fffffff) << 1), "tisp_msca_t33_four_ponit_normalized",
			0x170);
}

/* tisp_msca_t33_six_point_normalized: taps at -2n .. 3n */
static inline void t33_msca_six_point_v(u32 n, u32 taps, short *c)
{
	static const int add[6] = { 2, 3, 1, 4, 0, 5 }, sub[6] = { 5, 0, 4, 1, 3, 2 };
	int pos[6] = { -2 * (int)n, -(int)n, 0, (int)n, 2 * (int)n, 3 * (int)n };

	(void)taps;
	t33_msca_norm_v(n, c, pos, 6, add, sub, (int)((n * 6) >> 1), "tisp_msca_t33_six_point_normalized", 0x1c4);
}

#endif /* TX_ISP_T33_MSCACUR_H */
