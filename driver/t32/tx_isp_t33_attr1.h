/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 ioctl attribute wrappers (af weight, statis config): get / set through a stack copy of the
 * attribute block and the user copy.  Ports of tx_isp_af_weight_g_attr / _s_attr and
 * tx_isp_statis_config_g_attr / _s_attr (vendor .isra clones).  The kernel services are passed in
 * as callbacks so that the logic stays free of kernel symbols.
 */
#ifndef TX_ISP_T33_ATTR1_H
#define TX_ISP_T33_ATTR1_H

#include "tx_isp_t33_blk.h"

#define T33_AF_WEIGHT_BYTES	225U
#define T33_STATIS_CFG_BYTES	72U

struct t33_attr_ops {
	int (*copy_from)(void *dst, const void *src, u32 n);	/* private_copy_from_user */
	int (*copy_to)(void *dst, const void *src, u32 n);	/* private_copy_to_user */
	int (*get)(u32 a0, void *buf);				/* tisp_g_*_attr */
	int (*set)(u32 a0, void *buf);				/* tisp_s_*_attr */
	void (*err)(int level, int line);			/* isp_printf */
};

/* get: the getter fills the block, the first word of user is the destination user pointer */
static inline int t33_attr_get(const struct t33_attr_ops *o, u32 a0, const u8 *user, u32 len,
			       int line_get, int line_copy)
{
	u8 buf[256];
	unsigned int i;
	int r;

	for (i = 0; i < sizeof(buf); i++)
		buf[i] = 0;
	r = o->get(a0, buf);
	if (r) {
		o->err(1, line_get);
		return r;	/* getter failed */
	}
	r = o->copy_to((void *)(unsigned long)t33_le32(user), buf, len);
	if (r) {
		o->err(2, line_copy);
		return r;	/* copy to user failed */
	}
	return 0;
}

/* set: copy the block from user, hand it to the setter (its result is not returned) */
static inline int t33_attr_set(const struct t33_attr_ops *o, u32 a0, u32 user, u32 len, int line_copy)
{
	u8 buf[256];
	unsigned int i;
	int r;

	for (i = 0; i < sizeof(buf); i++)
		buf[i] = 0;
	r = o->copy_from(buf, (const void *)(unsigned long)user, len);
	if (r) {
		o->err(2, line_copy);
		return r;	/* copy from user failed */
	}
	o->set(a0, buf);
	return 0;
}

/* tx_isp_af_weight_g_attr */
static inline int t33_af_weight_g(const struct t33_attr_ops *o, u32 a0, const u8 *user)
{
	return t33_attr_get(o, a0, user, T33_AF_WEIGHT_BYTES, 774, 780);
}

/* tx_isp_af_weight_s_attr */
static inline int t33_af_weight_s(const struct t33_attr_ops *o, u32 a0, u32 user)
{
	return t33_attr_set(o, a0, user, T33_AF_WEIGHT_BYTES, 759);
}

/* tx_isp_statis_config_g_attr */
static inline int t33_statis_config_g(const struct t33_attr_ops *o, u32 a0, const u8 *user)
{
	return t33_attr_get(o, a0, user, T33_STATIS_CFG_BYTES, 1090, 1096);
}

/* tx_isp_statis_config_s_attr */
static inline int t33_statis_config_s(const struct t33_attr_ops *o, u32 a0, u32 user)
{
	return t33_attr_set(o, a0, user, T33_STATIS_CFG_BYTES, 1075);
}

#endif /* TX_ISP_T33_ATTR1_H */
