/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 AE exposure allocation: readable C of the vendor functions
 * tisp_ae_api_ev_alloc_calc, tisp_ae_ev_alloc_calc and tisp_ae_ev_list_alloc_calc
 * (Matt Davis' recovered C, PR #15 @ 2e2e06bc).  The fixed point maths are
 * hooks (the vendor's fix_point_* functions); every function is checked
 * against the vendor C by tests/t32_oracle (t32_aev*_oracle).
 */
#ifndef TX_ISP_T32_AEV_H
#define TX_ISP_T32_AEV_H

#include "tx_isp_t32_stats.h"

struct t32_aev_ops {
	u32 (*div32)(u32 shift, u32 a, u32 b);
	u32 (*mult2_32)(u32 shift, u32 a, u32 b);
	t32_u64 (*div64)(u32 shift, t32_u64 a, t32_u64 b);
	t32_u64 (*mult2_64)(u32 shift, t32_u64 a, t32_u64 b);
	t32_u64 (*mult3_64)(u32 shift, t32_u64 a, t32_u64 b, t32_u64 c);
};

/* low word of a 64 bit logical shift right (vendor __lshrdi3 on a 32 bit value) */
static inline u32 t32_aev_lsr(u32 v, u32 sh)
{
	return (u32)((t32_u64)v >> (sh & 63));
}

/*
 * tisp_ae_api_ev_alloc_calc: splits the target x = a3:a2 into the three
 * allocation outputs *o_a (gain-free exposure), *o_b, *o_c from the channel state
 * st (words 608..636, table at 1700), the tuning (shift 1736, mode 2920, 2922,
 * 2924) and buf[12842].  Returns *o_c.
 */
static inline u32 t32_aev_api_ev_alloc_calc(const u8 *tune, const u8 *buf, const u8 *st, u32 a2, u32 a3,
					    u32 *o_a, u32 *o_b, u32 *o_c, const struct t32_aev_ops *ops)
{
	u32 sh = t32_st16(tune, 1736), mode = t32_st16(tune, 2920), t5 = t32_st16(tune, 2922);
	u32 t6 = t32_st16(tune, 2924), n = buf[12842];
	u32 sB = t32_ld32(st, 612), sC = t32_ld32(st, 620), sx = sh & 31;
	u32 s0 = t32_ld32(st, 624) << sx, amax = t32_ld32(st, 608) << sx;
	u32 s1 = ops->div32(sh, t32_ld32(st, 636), sC);
	u32 m = ops->mult2_32(sh, sB, sC);
	u32 s2 = ops->div32(sh, t32_ld32(st, 628), sB);
	t32_u64 xx = ops->div64(sh, (t32_u64)a3 << 32 | a2, m), px;
	u32 lo = (u32)xx, hi = (u32)(xx >> 32), th, v, r;

	if (hi == 0 && lo < amax) {
		lo = amax;
		hi = 0;
	}
	xx = (t32_u64)hi << 32 | lo;
	if (mode == 1) {
		th = t32_ld32(st, 1700) << sx;
		if (hi == 0 && lo < th) {
			if (t5 == 0) {
				v = t32_aev_lsr(lo, sh) << sx;
				if (v < s0)
					s0 = v;
				lo = ops->div32(sh, lo, s0);
				if (!(s2 < lo)) {
					s1 = mode << sx;
					s2 = lo;
				} else if (!(ops->mult2_32(sh, s2, s1) < lo)) {
					s1 = ops->div32(sh, lo, s2);
				}
			} else if (t5 == mode) {
				if (th < s0)
					s0 = th;
				s2 = t5 << sx;
				s1 = s2;
			} else {
				int f = th < s0;

				r = ops->div32(sh, t6 << sx, 50U << sx);
				r = ops->mult2_32(sh, th, r);
				r = (r >> sx) << sx;
				if (f)
					s0 = th;
				if (r < s0)
					s0 = r;
				s2 = mode << sx;
				if (!(lo < s0)) {
					s0 = t32_aev_lsr(lo, sh) << sx;
					s2 = ops->div32(sh, lo, s0);
				}
				s1 = 1U << sx;
			}
		} else {
			const u8 *tbl = st + 1700;
			u32 idx = 1, lim;

			v = ops->div32(sh, t32_ld32(tbl, 4 * n) << sx, amax);
			lim = (20U << sx) + s0;
			for (;;) {
				if (!(lim < v))
					break;
				n = (n - 1) & 0xffff;
				if (n == 0) {
					n = 1;
					break;
				}
				v = t32_ld32(tbl, 4 * n) << sx;
			}
			while (!(n < idx)) {
				if (hi != 0 || !(lo < (t32_ld32(tbl, 4 * idx) << sx))) {
					idx = (idx + 1) & 0xff;
					continue;
				}
				break;
			}
			v = t32_ld32(tbl, 4 * (idx - 1)) << sx;
			if (v < s0)
				s0 = v;
			v = (u32)ops->div64(sh, xx, s0);
			if (!(s2 < v)) {
				s1 = 1U << sx;
				s2 = v;
			} else {
				r = ops->div32(sh, v, s2);
				if (r < s1)
					s1 = r;
			}
		}
	} else {
		r = ops->mult2_32(sh, m, amax);
		if (a3 == 0 && !(r < a2)) {
			s1 = sC;
			s2 = sB;
			s0 = amax;
		} else if (hi == 0 && !(s0 < lo)) {
			s0 = t32_aev_lsr(lo, sh) << sx;
			s1 = (u32)ops->div64(sh, lo, s0);
			s2 = 1024;
		} else {
			px = ops->mult2_64(sh, s0, s2);
			if (!(px < xx)) {
				s2 = (u32)ops->div64(sh, xx, s0);
				s1 = 1U << sx;
			} else if (!(ops->mult3_64(sh, s0, s2, s1) < xx)) {
				px = ops->mult2_64(sh, s0, s2);
				s1 = (u32)ops->div64(sh, xx, px);
			}
		}
	}
	*o_a = s0 >> sx;
	*o_b = ops->mult2_32(sh, s2, sB);
	*o_c = ops->mult2_32(sh, s1, sC);
	return *o_c;
}

#endif /* TX_ISP_T32_AEV_H */
