/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) BCSH EV interpolation, module list 18 (openimp #99).
 *
 * The vendor's tisp_bcsh_interp_by_ev looks up bcsh_para[ch], reads the
 * section pointer from state word 0, applies the hysteresis (sec + 0x64)
 * to the distance from the kept EV (BV mode sec[0x4c0]: the kept BV),
 * runs the shared EV interpolation and keeps the new 64-bit value.
 * It returns 1 when it skipped (not forced, within the hysteresis), else 0.
 * tisp_bcsh_sub_interp is the shared tisp_sub_interp.  tisp_bcsh_get_new_para
 * is the shared t33_bcsh_new_para.
 */
#ifndef TX_ISP_T33_BCSHI_H
#define TX_ISP_T33_BCSHI_H

#include "tx_isp_t33_bcsh.h"

/* the vendor's bcsh_para[]: one state pointer per channel */
extern void *bcsh_para[];

/* tisp_bcsh_sub_interp: a + round((b - a) * f, 12) */
static inline int t33_bcsh_sub_interp(u32 f, int a, int b)
{
	return t33_sub_interp(f, a, b);
}

/* tisp_bcsh_get_new_para: the 12 lo / 12 hi CT table into t (24 u16) */
static inline void t33_bcsh_get_new_para(u8 *sec, u8 *t)
{
	t33_bcsh_new_para(sec, t);
}

/*
 * tisp_bcsh_interp_by_ev: EV (or BV) interpolation and state update.
 * The original a1 is unused; the 64-bit value is a2:a3 (o32 ABI).
 */
static inline int t33_bcsh_interp_by_ev(u32 ch, u32 unused, u32 ev_lo, u32 ev_hi, u32 force)
{
	u8 *st = bcsh_para[ch];
	u8 *sec = *(u8 **)st;
	unsigned long long ev = (unsigned long long)ev_hi << 32 | ev_lo;
	unsigned long long d;
	long long n, o;
	u32 x;

	(void)unused;
	if (sec[0x4c0]) {
		n = (long long)(unsigned long long)ev_lo;
		o = (long long)((unsigned long long)t33_le32(st + 0x14) << 32 | t33_le32(st + 0x10));
	} else {
		n = (long long)((unsigned long long)ev_hi << 32 | ev_lo);
		o = (long long)((unsigned long long)t33_le32(st + 0xc) << 32 | t33_le32(st + 8));
	}
	d = n < o ? (unsigned long long)o - (unsigned long long)n : (unsigned long long)n - (unsigned long long)o;
	if (sec[0x4c0])
		x = (u32)(d >> 32) ? 0xffffffffU : (u32)d;
	else
		x = (int)(u32)(d >> 32) < 1 ? (u32)d : 0xffffffffU;

	if (!force && x <= t33_le32(sec + 0x64))
		return 1;

	t33_bcsh_ev(st, sec, ev);
	if (sec[0x4c0]) {
		t32_bcsh_p32(st, 0x10, ev_lo);
		t32_bcsh_p32(st, 0x14, ev_hi);
	} else {
		t32_bcsh_p32(st, 8, ev_lo);
		t32_bcsh_p32(st, 0xc, ev_hi);
	}
	return 0;
}

#endif /* TX_ISP_T33_BCSHI_H */
