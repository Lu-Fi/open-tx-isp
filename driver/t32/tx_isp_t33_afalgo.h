/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) AF per-frame algorithm (openimp #437 slice 2): vendor
 * tisp_af_alogrithm (tx-isp-t33.ko, Ghidra 0x45a74 / objdump 0x35954),
 * run from the AF interrupt (system irq 6, tisp_af_interrupt_static ->
 * event 15 -> tisp_af_calc_process).
 *
 * Inputs as the vendor's:
 *  info  af_info[ch] (0x4f0 bytes): word ring at +0 .. +0x3fc, ring shift
 *        u16 at +0x400, metric select byte at +0x403, section pointer at +0x4e8
 *  sec   the AF parameter block (the IQ section copy, tparamsP + 0x9fec):
 *        rows u16 +0x52, columns u16 +0x56, filter mix u16 +0x1e0 / +0x1e2,
 *        metric weights u16 +0x1e4 / +0x1e6, fixpoint bits u16 +0x1e8,
 *        block weights u8 +0x21f (15 per row); outputs: history words
 *        +0x04 .. +0x3c, metrics +0x3c / +0x40 (what GetAFMetrics reads at
 *        +0x40 / +0x44), +0x44 / +0x48 / +0x4c
 *  stat  the statistics bank (reg 0x1811c & 3) * 0x1800 of the 0x6000-byte
 *        AF DMA buffer: 24-byte block records, filter sums as LE u32 at
 *        +8 / +12 / +16 / +20; the record is complete when the u16 at +0
 *        equals the u16 at +0x1520 (else nothing is updated)
 *  fv_old the previous FV (per channel)
 *
 * The fixpoint helpers are the vendor's fix_point_mult2_32 /
 * fix_point_div_32 (same code as tx_isp_t33_r13C.h, own names here because
 * tx_isp_t33_math.h declares another t33_fix_point_mult2_32).  A zero
 * weight sum divides by zero in the vendor (divu, no trap); here the
 * quotient is 0xffffffff, what the MIPS emulation gives; the section's
 * weights are never all zero.
 */
#ifndef TX_ISP_T33_AFALGO_H
#define TX_ISP_T33_AFALGO_H

#include "tx_isp_t33_blk.h"

#define T33_AFA_INFO_BYTES	0x4f0U
#define T33_AFA_BANK_BYTES	0x1800U
#define T33_AFA_TAG_END		0x1520U

static inline u32 t33_afa_rd16(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8; }
static inline u32 t33_afa_rd32(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }
static inline void t33_afa_wr32(u8 *p, u32 v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

/* fix_point_mult2_32 (shift counts modulo 32 as srlv / sllv) */
static inline u32 t33_afa_mult2(u32 s, u32 a, u32 b)
{
	u32 sh = s & 31, mask = 0xffffffffU >> ((0U - s) & 31);
	u32 ah = a >> sh, bh = b >> sh, al = a & mask, bl = b & mask;

	return ((ah * bh) << sh) + (al * bh + ah * bl) + ((al * bl) >> sh);
}

/* fix_point_div_32 */
static inline u32 t33_afa_div(u32 s, u32 a, u32 b)
{
	u32 q, rem, rem2, frac = 0, i;

	q = b ? a / b : 0;
	rem = a - b * q;
	for (i = 0; i < s; i++) {
		rem2 = rem << 1;
		frac <<= 1;
		if (b < rem2) {
			rem = rem2 - b;
			frac |= 1;
		} else if (b == rem2) {
			frac |= 1;
			frac <<= ((s - 1 - i) & 31);
			break;
		} else {
			rem = rem2;
		}
	}
	return (u32)((unsigned long long)q << s) | frac;
}

static inline u32 t33_afa_divu(u32 a, u32 b)
{
	return b ? a / b : 0xffffffffU;
}

/* shift the words [first + 4, end) down by one word (vendor: p[-1] = *p) */
static inline void t33_afa_shift(u8 *first, u8 *end)
{
	u8 *p;

	for (p = first + 4; p != end; p += 4)
		t33_afa_wr32(p - 4, t33_afa_rd32(p));
}

/* tisp_af_alogrithm */
static inline void t33_af_alogrithm(u8 *info, u8 *sec, const u8 *stat, u32 *fv_old)
{
	u32 ring_sh = t33_afa_rd16(info + 0x400);
	u32 cols = t33_afa_rd16(sec + 0x56), rows = t33_afa_rd16(sec + 0x52);
	u32 w6 = t33_afa_rd16(sec + 0x1e6), c5 = t33_afa_rd16(sec + 0x1e0), c6 = t33_afa_rd16(sec + 0x1e2);
	u32 w4 = t33_afa_rd16(sec + 0x1e4), fb = t33_afa_rd16(sec + 0x1e8), sh = fb & 31;
	u32 wsum = 0, sa = 0, sb = 0, sc = 0, sd = 0, r, c, one, v1, v2, h1, h2, m, sel_v, d, fv, ring_v, area;
	const u8 *wr = sec + 0x21f, *row = stat;
	u8 c_sel;

	for (r = 0; r < rows; r++) {
		const u8 *blk = row;

		for (c = 0; c < cols; c++) {
			u32 w = wr[c];

			wsum += w;
			sa += w * t33_afa_rd32(blk + 16);
			sb += w * t33_afa_rd32(blk + 20);
			sc += w * t33_afa_rd32(blk + 8);
			sd += w * t33_afa_rd32(blk + 12);
			blk += 24;
		}
		row += rows * 24;	/* the vendor steps rows by the row count */
		wr += 15;
	}
	if (t33_afa_rd16(stat) != t33_afa_rd16(stat + T33_AFA_TAG_END))
		return;
	one = 1U << sh;
	sa = t33_afa_mult2(fb, sa << sh, c5);
	sc = t33_afa_mult2(fb, sc << sh, one - c5);
	sb = t33_afa_mult2(fb, sb << sh, c6);
	sd = t33_afa_mult2(fb, sd << sh, one - c6);
	v1 = t33_afa_divu(sa + sc, wsum);
	v2 = t33_afa_divu(sb + sd, wsum);
	h1 = t33_afa_mult2(fb, v1 << sh, w4);
	h2 = t33_afa_mult2(fb, v2 << sh, w6);
	m = (h1 + h2) >> sh;
	h1 = t33_afa_mult2(fb, ((sa + sc) >> 3) << sh, w4);
	h2 = t33_afa_mult2(fb, ((sb + sd) >> 3) << sh, w6);
	t33_afa_shift(sec + 4, sec + 0x40);
	t33_afa_wr32(sec + 0x3c, m);
	t33_afa_wr32(sec + 0x40, (h1 + h2) >> sh);
	area = cols * rows;
	v1 *= area;
	t33_afa_wr32(sec + 0x44, m * area);
	fv = *fv_old;
	t33_afa_wr32(sec + 0x48, v1);
	d = v1 > fv ? v1 - fv : fv - v1;
	t33_afa_wr32(sec + 0x4c, v2 * area);
	if (!fv)
		*fv_old = 1;
	ring_v = t33_afa_div(0x10, d, *fv_old);
	c_sel = info[0x403];
	if (c_sel > 3) {
		*fv_old = t33_afa_rd32(sec + 0x48);
		return;
	}
	t33_afa_shift(info, info + 0x3fc);
	sel_v = c_sel == 0 ? ring_v << (ring_sh & 31) : t33_afa_rd32(sec + (c_sel == 1 ? 0x44 : c_sel == 2 ? 0x48 : 0x4c));
	t33_afa_wr32(info + 0x3f8, sel_v);
	*fv_old = t33_afa_rd32(sec + 0x48);
}

#endif /* TX_ISP_T33_AFALGO_H */
