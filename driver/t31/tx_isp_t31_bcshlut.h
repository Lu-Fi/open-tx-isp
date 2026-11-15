/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T31 BCSH LUT writer: vendor tiziano_bcsh_lut_parameter (tx-isp-t31.ko H20221206a, 19 pointer arguments) as
 * plain C, shared by driver/t31/tx_isp_tuning.c and the binary oracle tests/t31_binoracle/bcshlut.c (openimp #550).
 *
 * Arguments in the vendor order: clip (constant au32clip), clip1, clip2, Offset0, Offset1, C, Cslope0, Cslope1,
 * Cslope2, Sthres_now, Svalue, Sstep[2], HMatrix, HDP_now, HDPslope, HBP_now, HBPslope, HLSP, HLSPslope.
 * Globals the vendor reads directly: BCSH_ev, the au32clip0 bytes (enable, low index, tail enable, high index),
 * the EvList_now and HLSP_now pointers.  Every register word is hi << 16 | lo without a mask, like the vendor.
 * T31_BL_W(reg, val) is the register write (system_reg_write in the driver).
 */
#ifndef TX_ISP_T31_BCSHLUT_H
#define TX_ISP_T31_BCSHLUT_H

static inline uint32_t t31_bl_idx(uint32_t b)
{
	if (b >= 10)
		return 9;
	return b ? b : 1;
}

static inline void t31_bcsh_lut(const uint32_t *clip, const uint32_t *clip1, const uint32_t *clip2,
				const uint32_t *off0, const uint32_t *off1, const uint32_t *c,
				const uint32_t *cslope0, const uint32_t *cslope1, const uint32_t *cslope2,
				const uint32_t *sthres, const uint32_t *svalue, const uint32_t *sstep,
				const uint32_t *hmat, const uint32_t *hdp, const uint32_t *hdpslope,
				const uint32_t *hbp, const uint32_t *hbpslope, const uint32_t *hlsp,
				const uint32_t *hlspslope, uint32_t ev_raw, const uint32_t *clip0,
				const uint32_t *evl, const uint32_t *hlsp_now)
{
	uint32_t ev = ev_raw >> 10;
	uint32_t lo = t31_bl_idx((uint8_t)clip0[1]);
	uint32_t hi = t31_bl_idx((uint8_t)clip0[3]);
	uint32_t mix = hlsp[2] << 16 | hlsp[1];

	/* vendor 0x28304..0x2855c: HLSP mix */
	if ((uint8_t)clip0[0] == 1 && ev < evl[lo - 1]) {
		uint32_t lim = evl[lo - 1];
		uint32_t n = ev < 2 ? 1 - ev : ev - 1;
		uint32_t den = lim ? lim - 1 : 1;

		mix = (n * hlsp_now[1]) / den | (n * hlsp_now[2]) / den << 16;
	} else if ((uint8_t)clip0[2] == 1) {
		uint32_t lim = evl[8];

		if (!(ev < lim)) {
			mix = 0;
		} else {
			uint32_t st = evl[hi - 1];

			if (st < ev) {
				uint32_t range = lim < st ? st - lim : lim - st;
				uint32_t h = hlsp_now[2], l = hlsp_now[1], hp = 0;

				if (h)
					hp = (h - (ev - st) * h / range) << 16;
				mix = l ? (hp | (l - (ev - st) * l / range)) : hp;
			}
		}
	}

	T31_BL_W(0x8000, clip[0] << 16 | clip[1]);
	T31_BL_W(0x8004, clip1[0] << 16 | clip1[1]);
	T31_BL_W(0x8008, clip2[0] << 16 | clip2[1]);
	T31_BL_W(0x800c, clip[2] << 16 | clip[3]);
	T31_BL_W(0x8010, clip1[2] << 16 | clip1[3]);
	T31_BL_W(0x8014, clip2[2] << 16 | clip2[3]);
	T31_BL_W(0x8018, off1[0] << 16 | off0[0]);
	T31_BL_W(0x801c, off1[1] << 16 | off0[1]);
	T31_BL_W(0x8020, off1[2] << 16 | off0[2]);
	T31_BL_W(0x8024, hmat[1] << 16 | hmat[0]);
	T31_BL_W(0x8028, hmat[2]);
	T31_BL_W(0x802c, hmat[4] << 16 | hmat[3]);
	T31_BL_W(0x8030, hmat[5]);
	T31_BL_W(0x8034, hmat[7] << 16 | hmat[6]);
	T31_BL_W(0x8038, hmat[8]);
	T31_BL_W(0x803c, hdpslope[0] << 16 | hdp[0]);
	T31_BL_W(0x8040, hdp[2] << 16 | hdp[1]);
	T31_BL_W(0x8044, hbpslope[0] << 16 | hbp[0]);
	T31_BL_W(0x8048, hbp[2] << 16 | hbp[1]);
	T31_BL_W(0x804c, hlspslope[0] << 16 | hlsp[0]);
	T31_BL_W(0x8050, mix);
	T31_BL_W(0x8054, cslope0[0] << 16 | c[0]);
	T31_BL_W(0x8058, cslope2[0] << 16 | cslope1[0]);
	T31_BL_W(0x805c, c[2] << 16 | c[1]);
	T31_BL_W(0x8060, c[4] << 16 | c[3]);
	T31_BL_W(0x8064, sstep[1] << 16 | sstep[0] << 3 | sthres[0]);
	T31_BL_W(0x8068, sthres[2] << 16 | sthres[1]);
	T31_BL_W(0x806c, svalue[0] << 16 | svalue[1]);
	T31_BL_W(0x8070, svalue[2] << 16 | svalue[3]);
}

#endif
