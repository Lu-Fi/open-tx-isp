/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 MSCA channel attribute setter: readable C of lib_tisp_msca_channel_attr_set.
 * The vendor gets one index n (channel = n / 3, set = n % 3) and a 52 byte attribute
 * block; it fills the set's block (sb + set * 872, sb = t32_data_e2768[channel]):
 *   +22..+28  autozoom window (attr +36..+48; or zero and the channel default def[0], def[1]),
 *             only while the channel's autozoom flag (t32_data_e2760) is clear; the same
 *             values (and attr +32 = enable) are saved into t32_data_e2650[channel] words
 *             set + 3 * k (k = 0..4)
 *   +30 / +34 / +32  scale flag, width, height   (attr +0 / +4 / +8, or 0 / +26 / +28)
 *   +40..+46  crop window                         (attr +16..+28, or 0, 0, +34, +32)
 *   +36 / +37 cleared; the set's crop size is copied to sb + 2 * set (width) and
 *   sb + 6 + 2 * set (height).
 * Returns the vendor's v0: the crop height of the set (+46).
 *
 * Checked against the vendor function (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_mscai_oracle).
 */
#ifndef TX_ISP_T32_MSCA_ATTR_H
#define TX_ISP_T32_MSCA_ATTR_H

#include "tx_isp_t32_msca_api.h"

static inline u32 t32_msca_channel_attr_set(u8 *sb, u8 *saved, const u8 *def, int az_flag,
					    unsigned int set, const u8 *attr)
{
	u8 *s = sb + set * T32_MSCA_SET_SZ;
	unsigned int k;

	if (!az_flag) {
		if (t32_msa_r32(attr + 32) == 1) {
			for (k = 1; k <= 4; k++)
				t32_msa_w16(s + 20 + 2 * k, t32_msa_r32(attr + 32 + 4 * k));
			for (k = 0; k <= 4; k++)
				t32_msa_w32(saved + (set + 3 * k) * 4, t32_msa_r32(attr + 32 + 4 * k));
		} else {
			t32_msa_w16(s + 22, 0);
			t32_msa_w16(s + 24, 0);
			t32_msa_w16(s + 26, t32_msa_r32(def));
			t32_msa_w16(s + 28, t32_msa_r32(def + 4));
			for (k = 0; k <= 4; k++)
				t32_msa_w32(saved + (set + 3 * k) * 4, k == 3 ? t32_msa_r32(def) :
					    k == 4 ? t32_msa_r32(def + 4) : 0);
		}
	}
	if (t32_msa_r32(attr)) {
		t32_msa_w16(s + 30, 1);
		t32_msa_w16(s + 34, t32_msa_r32(attr + 4));
		t32_msa_w16(s + 32, t32_msa_r32(attr + 8));
	} else {
		t32_msa_w16(s + 30, 0);
		t32_msa_w16(s + 34, t32_msa_r16(s + 26));
		t32_msa_w16(s + 32, t32_msa_r16(s + 28));
	}
	if (t32_msa_r32(attr + 12)) {
		t32_msa_w16(s + 40, t32_msa_r32(attr + 16));
		t32_msa_w16(s + 42, t32_msa_r32(attr + 20));
		t32_msa_w16(s + 44, t32_msa_r32(attr + 24));
		t32_msa_w16(s + 46, t32_msa_r32(attr + 28));
	} else {
		t32_msa_w16(s + 40, 0);
		t32_msa_w16(s + 42, 0);
		t32_msa_w16(s + 44, t32_msa_r16(s + 34));
		t32_msa_w16(s + 46, t32_msa_r16(s + 32));
	}
	s[36] = 0;
	s[37] = 0;
	t32_msa_w16(sb + 2 * set, t32_msa_r16(s + 44));
	t32_msa_w16(sb + 6 + 2 * set, t32_msa_r16(s + 46));
	return t32_msa_r16(s + 46);
}

#endif /* TX_ISP_T32_MSCA_ATTR_H */
