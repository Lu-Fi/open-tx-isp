/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T31 tiziano_bcsh_update, vendor-equal to tx-isp-t31.ko (H20221206a) 0x29f20 (openimp #533; oracle
 * tests/t31_binoracle/bcshup.conf).
 *
 * ev = raw EV >> 10 (vendor BCSH_ev); ev <= evl[0] takes entry 0, ev >= evl[8] entry 8, else the first i with
 * evl[i] <= ev <= evl[i + 1] blends the eight lists (S min / max small, S min / max medium, Cxl, Cxh, Cyl, Cyh)
 * as a + / - |ev - evl[i]| * |b - a| / |evl[i + 1] - evl[i]| (32-bit, unsigned; the direction from b < a).
 * A zero-width segment is a vendor divide by zero; ours keeps entry i (beyond vendor guard).  No segment
 * found (unsorted list): nothing is stored, as in the vendor.  Then Comp2Orig, the CT interpolation with the
 * raw CT (vendor BCSH_ct), RGB2YUV, offset RGB2YUV, TransitParam, the LUT parameters and BCSH_real = 0.
 *
 * Hooks: by default the vendor names (the binary oracle records them); the driver defines T31_BCSHUP_HOOKS.
 */
#ifndef TX_ISP_T31_BCSHUP_H
#define TX_ISP_T31_BCSHUP_H

#ifndef T31_BCSHUP_HOOKS
extern int tiziano_bcsh_Tccm_Comp2Orig(void);
extern int tiziano_ct_bcsh_interpolation(uint32_t ct);
extern int tiziano_bcsh_Tccm_RGB2YUV(void *a, void *b);
extern int tiziano_bcsh_Toffset_RGB2YUV(void *a, void *b);
extern int tiziano_bcsh_TransitParam(void);
extern int tiziano_bcsh_lut_parameter(uint32_t a, uint32_t b, uint32_t c, uint32_t d);
#endif

static inline uint32_t t31_bcsh_seg(uint32_t a, uint32_t b, uint32_t lo, uint32_t hi, uint32_t ev)
{
	uint32_t d = ev > lo ? ev - lo : lo - ev, s = hi < lo ? lo - hi : hi - lo;

	if (!s)
		return a;
	if (b < a)
		return a - d * (a - b) / s;
	return d * (b - a) / s + a;
}

/* src[k][0..8] -> *dst[k], k = 0..7 */
static inline void t31_bcsh_ev_select(uint32_t ev, const uint32_t *evl, const uint32_t *const *src, uint32_t *const *dst)
{
	int i, k;

	if (!(evl[0] < ev)) {
		for (k = 0; k < 8; k++)
			*dst[k] = src[k][0];
		return;
	}
	if (!(ev < evl[8])) {
		for (k = 0; k < 8; k++)
			*dst[k] = src[k][8];
		return;
	}
	for (i = 0; i < 8; i++) {
		if (ev < evl[i] || evl[i + 1] < ev)
			continue;
		for (k = 0; k < 8; k++)
			*dst[k] = t31_bcsh_seg(src[k][i], src[k][i + 1], evl[i], evl[i + 1], ev);
		return;
	}
}

static inline int t31_bcsh_update(uint32_t ev_raw, uint32_t ct, const uint32_t *evl, const uint32_t *const *src,
				  uint32_t *const *dst)
{
	t31_bcsh_ev_select(ev_raw >> 10, evl, src, dst);
	T31_BU_COMP2ORIG();
	T31_BU_CTINTP(ct);
	T31_BU_RGB2YUV();
	T31_BU_TOFFSET();
	T31_BU_TRANSIT();
	T31_BU_LUT();
	T31_BU_REAL_CLEAR();
	return 0;
}

#endif /* TX_ISP_T31_BCSHUP_H */
