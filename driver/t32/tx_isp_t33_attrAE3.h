/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 ioctl attribute glue of the AE weight and AWB attribute blocks
 * (vendor tx_isp_ae_weight_g_attr.isra.54 / tx_isp_ae_weight_s_attr.isra.22 /
 * tx_isp_awb_attr_g_attr.isra.17 / tx_isp_awb_attr_s_attr.isra.19).
 *
 * Same flow as the T32 glue (struct t32_isra_ops of tx_isp_t32_stats.h): the
 * copy helpers, printk and the tisp_[sg]_* calls are hooks.  AE weight: the
 * 232 byte user block is word 0 -> loc + 4, then 225 weight bytes -> loc + 233.
 * AWB: the 60 byte user block is loc[0..16) + loc[20..64).
 */
#ifndef TX_ISP_T33_ATTRAE3_H
#define TX_ISP_T33_ATTRAE3_H

#include "tx_isp_t32_stats.h"

#define T33_AE3_W_LEN	232U
#define T33_AE3_W_WORD	4U
#define T33_AE3_W_OFF	233U
#define T33_AE3_W_N	225U

#define T33_AWB3_LEN	60U
#define T33_AWB3_HEAD	16U
#define T33_AWB3_SKIP	4U

/* tx_isp_ae_weight_g_attr: getter, then copy out; a failed getter stops (level 1) */
static inline int t33_ae_weight_g_attr(const struct t32_isra_ops *o, u32 ch, u32 user, u8 *loc, u8 *out)
{
	int r = o->call(o->priv, ch, loc);

	if (r) {
		o->printk(o->priv, 1); /* ae weight g: getter failed */
		return r;
	}
	t32_aea_cpy(out, loc + T33_AE3_W_WORD, 4);
	t32_aea_cpy(out + 4, loc + T33_AE3_W_OFF, T33_AE3_W_N);
	r = o->user_out(o->priv, user, out, T33_AE3_W_LEN);
	if (r)
		o->printk(o->priv, 2); /* ae weight g: copy failed */
	return r;
}

/* tx_isp_ae_weight_s_attr: copy in (a failure stops, level 2), then the setter */
static inline int t33_ae_weight_s_attr(const struct t32_isra_ops *o, u32 ch, u32 user, u8 *in, u8 *loc)
{
	int r = o->user_in(o->priv, in, user, T33_AE3_W_LEN);

	if (r) {
		o->printk(o->priv, 2); /* ae weight s: copy failed */
		return r; /* ae s copy failed */
	}
	t32_aea_cpy(loc + T33_AE3_W_WORD, in, 4);
	t32_aea_cpy(loc + T33_AE3_W_OFF, in + 4, T33_AE3_W_N);
	o->call(o->priv, ch, loc);
	return 0;
}

/* tx_isp_awb_attr_g_attr */
static inline int t33_awb_attr_g_attr(const struct t32_isra_ops *o, u32 ch, u32 user, u8 *loc, u8 *out)
{
	int r = o->call(o->priv, ch, loc);

	if (r) {
		o->printk(o->priv, 1); /* awb g: getter failed */
		return r;
	}
	t32_aea_cpy(out, loc, T33_AWB3_HEAD);
	t32_aea_cpy(out + T33_AWB3_HEAD, loc + T33_AWB3_HEAD + T33_AWB3_SKIP, T33_AWB3_LEN - T33_AWB3_HEAD);
	r = o->user_out(o->priv, user, out, T33_AWB3_LEN);
	if (r)
		o->printk(o->priv, 2); /* awb g: copy failed */
	return r;
}

/* tx_isp_awb_attr_s_attr: copy in, read the attributes, overlay, write them */
static inline int t33_awb_attr_s_attr(const struct t32_isra_ops *o, u32 ch, u32 user, u8 *in, u8 *loc)
{
	int r = o->user_in(o->priv, in, user, T33_AWB3_LEN);

	if (r) {
		o->printk(o->priv, 2); /* awb s: copy failed */
		return r;
	}
	o->call(o->priv, ch, loc);
	t32_aea_cpy(loc, in, T33_AWB3_HEAD);
	t32_aea_cpy(loc + T33_AWB3_HEAD + T33_AWB3_SKIP, in + T33_AWB3_HEAD, T33_AWB3_LEN - T33_AWB3_HEAD);
	o->call2(o->priv, ch, loc);
	return 0;
}

#endif /* TX_ISP_T33_ATTRAE3_H */
