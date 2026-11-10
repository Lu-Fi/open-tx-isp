/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 MSCA crop paths: readable C of tisp_msca_curve_calc (the channel-state
 * wrapper around t32_msca_curve), tisp_msca_api_crop and
 * tisp_msca_api_set_fcrop.  Per set (872 bytes, tx_isp_t32_msca_api.h /
 * curve.h layout) they store the window into the set block, recompute and
 * reload the scaler (para_calc, curve_calc, init_chx_cfg) and kick the MSCA
 * with a write of 1 to 0xf0010; in the shadow mode (t32_data_d03a0 != 0)
 * the vendor passes shadow index 13 of group <channel> instead.  The register
 * writes of init_chx_cfg are modelled in direct mode only (curve.h).
 *
 * Checked against the vendor functions (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_mscah_oracle).
 */
#ifndef TX_ISP_T32_MSCA_CROP_H
#define TX_ISP_T32_MSCA_CROP_H

#include "tx_isp_t32_msca_curve.h"
#include "tx_isp_t32_msca_io.h"
#include "tx_isp_t32_msca_api.h"

/*
 * tisp_msca_curve_calc for set 0..2: the first ratio (width: sets +32 / +28) fills
 * coef[set * 50 ..], the second (+34 / +26) coef[set * 50 + 25 ..]; marks the channel pending.
 */
static inline void t32_msca_curve_calc(struct t32_msca_ch *c, const u8 *sets, unsigned int set)
{
	const u8 *s = sets + set * T32_MSCA_SET_BYTES;
	int k[T32_CURVE_TAPS];
	unsigned int dir, i;

	for (dir = 0; dir < 2; dir++) {
		u32 in = t32_msa_r16(s + (dir ? 26 : 28)), out = t32_msa_r16(s + (dir ? 34 : 32));

		t32_msca_curve(in, out, k);
		for (i = 0; i < T32_CURVE_TAPS; i++)
			c->coef[set * 50 + dir * 25 + i] = (u16)k[i];
	}
	c->pending = 1;
}

/* the kick write: 0xf0010 = 1, or shadow index 13 of group ch */
static inline void t32_msca_crop_kick(u32 ch, int shadow, struct t32_front_write *w, unsigned int *n)
{
	T32_FWR(w, *n, shadow ? T32_MS_SHADOW_IDX(ch, 13) : 0xf0010, 1);
}

/*
 * tisp_msca_api_crop: buf is 5 u16 {enable, x, y, w, h}; with enable == 1 the window goes to
 * set +40.. +46, otherwise it is cleared to the full size kept at sets + 2 * set (w) / + 6 (h).
 * The vendor returns the kick write's result (not compared).  Returns the number of writes.
 */
static inline int t32_msca_api_crop(struct t32_msca_ch *c, u8 *sets, unsigned int ch,
				    unsigned int set, const u8 *buf, int shadow,
				    struct t32_front_write *w)
{
	u8 *s = sets + set * T32_MSCA_SET_BYTES;
	unsigned int n = 0;

	if (t32_msa_r16(buf) == 1) {
		t32_msa_w16(s + 40, t32_msa_r16(buf + 2));
		t32_msa_w16(s + 42, t32_msa_r16(buf + 4));
		t32_msa_w16(s + 44, t32_msa_r16(buf + 6));
		t32_msa_w16(s + 46, t32_msa_r16(buf + 8));
	} else {
		t32_msa_w16(s + 40, 0);
		t32_msa_w16(s + 42, 0);
		t32_msa_w16(s + 44, t32_msa_r16(sets + 2 * set));
		t32_msa_w16(s + 46, t32_msa_r16(sets + 6 + 2 * set));
	}
	t32_msca_wr16(s, 38, t32_msa_r16(buf));
	t32_msca_init_chx_cfg_w(c, sets, set, w, &n);
	t32_msca_crop_kick(ch, shadow, w, &n);
	return (int)n;
}

/*
 * tisp_msca_api_set_fcrop(ch, enable, a2, a3, a4, a5): with enable (low byte) set, every set gets
 * flag 1 and a3 / a2 / a4 / a5 at +22 / +24 / +26 / +28, then para_calc, curve_calc, init_chx_cfg,
 * the channel is marked pending and the kick is written.  Without enable the vendor only tail-calls
 * isp_printf (*msgs = 1).  Returns the number of writes.
 */
static inline int t32_msca_api_set_fcrop(struct t32_msca_ch *c, u8 *sets, unsigned int ch, u32 enable,
					 u32 a2, u32 a3, u32 a4, u32 a5, int shadow,
					 struct t32_front_write *w, unsigned int *msgs)
{
	unsigned int set, n = 0;

	*msgs = 0;
	if (!(enable & 0xff)) {
		*msgs = 1;
		return 0;
	}
	for (set = 0; set < 3; set++) {
		u8 *s = sets + set * T32_MSCA_SET_BYTES;

		t32_msa_w16(s + 20, 1);
		t32_msa_w16(s + 22, a3);
		t32_msa_w16(s + 24, a2);
		t32_msa_w16(s + 26, a4);
		t32_msa_w16(s + 28, a5);
		t32_msca_para_calc(c, sets, set);
		t32_msca_curve_calc(c, sets, set);
		t32_msca_init_chx_cfg_w(c, sets, set, w, &n);
		c->pending = 1;
		t32_msca_crop_kick(ch, shadow, w, &n);
	}
	return (int)n;
}

#endif /* TX_ISP_T32_MSCA_CROP_H */
