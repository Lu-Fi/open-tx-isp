/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 MSCA parameter array and OSD attribute setters: readable C of
 * tisp_msca_param_array_get / _set and tisp_msca_api_set_osd_out_attribute_cfg.
 *
 * The parameter array is 188 bytes of the channel's parameter block followed
 * by the 300 byte coefficient area of its context (488 bytes in all);
 * setting it rewrites the 188 bytes and runs params_update.
 *
 * Checked against the vendor functions (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_mscaf_oracle).
 */
#ifndef TX_ISP_T32_MSCA_ARR_H
#define TX_ISP_T32_MSCA_ARR_H

#include "tx_isp_t32_msca_curve.h"
#include "tx_isp_t32_msca_api.h"

#define T32_MSCA_ARR_PAR	188
#define T32_MSCA_ARR_COEF	300
#define T32_MSCA_ARR_LEN	(T32_MSCA_ARR_PAR + T32_MSCA_ARR_COEF)

/* tisp_msca_param_array_get: returns the length stored through the vendor's a2 */
static inline unsigned int t32_msca_param_array_get(const struct t32_msca_ch *c, u8 *dst)
{
	unsigned int i;

	for (i = 0; i < T32_MSCA_ARR_PAR; i++)
		dst[i] = c->par[i];
	for (i = 0; i < T32_MSCA_COEFS; i++)
		t32_msa_w16(dst + T32_MSCA_ARR_PAR + 2 * i, c->coef[i]);
	return T32_MSCA_ARR_LEN;
}

/* tisp_msca_param_array_set: the 188 parameter bytes, then params_update; returns the number of writes */
static inline int t32_msca_param_array_set(struct t32_msca_ch *c, const u8 *sets, const u8 *src,
					   struct t32_front_write *w)
{
	unsigned int i;

	for (i = 0; i < T32_MSCA_ARR_PAR; i++)
		c->par[i] = src[i];
	return t32_msca_params_update(c, sets, w);
}

/*
 * tisp_msca_api_set_osd_out_attribute_cfg: buf[29] picks the entry (0 or 1);
 * returns -1 (after two isp_printf messages) for anything else.
 */
static inline int t32_msca_api_set_osd_out_attr_cfg(u8 *sb, const u8 *buf)
{
	unsigned int i = buf[29], k;

	if (i >= 2)
		return -1;
	sb[i + 844] = buf[0];
	sb[840] = buf[28];
	sb[i + 842] = (u8)t32_msa_r32(buf + 4);
	sb[i + 846] = (u8)t32_msa_r32(buf + 8);
	for (k = 0; k < 4; k++) {
		sb[i * 16 + 848 + k] = buf[12 + k];
		sb[i * 16 + 852 + k] = buf[16 + k];
		sb[i * 16 + 856 + k] = buf[20 + k];
		sb[i * 16 + 860 + k] = buf[24 + k];
	}
	t32_msa_w32(sb + 880, 255);
	return 0;
}

#endif /* TX_ISP_T32_MSCA_ARR_H */
