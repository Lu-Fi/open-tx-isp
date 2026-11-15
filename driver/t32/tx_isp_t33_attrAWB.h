/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 ioctl attribute glue of the AWB / CCM blocks: tx_isp_awb_only_read_attr,
 * tx_isp_awb_weight_g_attr, tx_isp_awb_weight_s_attr, tiziano_isp_ccm_attr_g_ctrl
 * (vendor *.isra.N).  The user block pointer is the first word of the argument block
 * (the caller reads it); kernel calls are hooks.
 */
#ifndef TX_ISP_T33_ATTRAWB_H
#define TX_ISP_T33_ATTRAWB_H

#include "tx_isp_t33_blk.h"

struct t33_awbattr_ops {
	int (*g_awb_attr)(u32 ch, u8 *buf);		/* tisp_g_awb_attr */
	int (*g_weight)(u32 ch, u8 *buf);		/* tisp_g_awb_weight_attr */
	int (*s_weight)(u32 ch, const u8 *buf);		/* tisp_s_awb_weight_attr */
	int (*g_ccm)(u32 ch, u8 *buf);			/* tisp_g_ccm_attr */
	int (*to_user)(u32 user, const u8 *src, u32 n);	/* private_copy_to_user */
	int (*from_user)(u8 *dst, u32 user, u32 n);	/* private_copy_from_user */
	void (*printk)(u32 level);			/* isp_printf (level only) */
};

#define T33_AWBRD_ATTR_BYTES	60U
#define T33_AWBRD_OFF	16U
#define T33_AWBRD_BYTES	4U
#define T33_AWBW_BYTES	1024U
#define T33_CCM_ATTR_BYTES	40U

/* the 1 KiB weight block: the vendor's stack frame; static in the driver (callers hold the tuning lock) */
#ifdef T33_ATTRAWB_STATIC
#define T33_AWBW_STORE	static
#else
#define T33_AWBW_STORE
#endif

/* tx_isp_awb_only_read_attr: the word at +16 of the AWB attribute to the user */
static inline int t33_awb_only_read_attr(const struct t33_awbattr_ops *ops, u32 ch, u32 user)
{
	u8 buf[T33_AWBRD_ATTR_BYTES];
	u32 v;
	int rc;

	ops->g_awb_attr(ch, buf);
	v = t33_le32(buf + T33_AWBRD_OFF);
	rc = ops->to_user(user, (const u8 *)&v, T33_AWBRD_BYTES);
	if (rc != 0)
		ops->printk(2);	/* only_read */
	return rc;
}

/* tx_isp_awb_weight_g_attr */
static inline int t33_awb_weight_g_attr(const struct t33_awbattr_ops *ops, u32 ch, u32 user)
{
	T33_AWBW_STORE u8 buf[T33_AWBW_BYTES];
	int r, rc;

	r = ops->g_weight(ch, buf);
	if (r != 0) {
		ops->printk(1);
		return r;
	}
	rc = ops->to_user(user, buf, T33_AWBW_BYTES);
	if (rc != 0) {
		ops->printk(2);	/* weight_g copy */
		return rc;
	}
	return rc;
}

/* tx_isp_awb_weight_s_attr */
static inline int t33_awb_weight_s_attr(const struct t33_awbattr_ops *ops, u32 ch, u32 user)
{
	T33_AWBW_STORE u8 buf[T33_AWBW_BYTES];
	int rc;

	rc = ops->from_user(buf, user, T33_AWBW_BYTES);
	if (rc != 0) {	/* weight_s check */
		ops->printk(2);	/* weight_s */
		return rc; /* s_weight copy failed */
	}
	ops->s_weight(ch, buf);
	return 0;
}

/* tiziano_isp_ccm_attr_g_ctrl: always 0, a failed copy only prints */
static inline int t33_ccm_attr_g_ctrl(const struct t33_awbattr_ops *ops, u32 ch, u32 user)
{
	u8 buf[T33_CCM_ATTR_BYTES];
	int rc;

	ops->g_ccm(ch, buf);
	rc = ops->to_user(user, buf, T33_CCM_ATTR_BYTES);
	if (rc != 0)
		ops->printk(2);	/* ccm */
	return 0; /* ccm */
}

#endif /* TX_ISP_T33_ATTRAWB_H */
