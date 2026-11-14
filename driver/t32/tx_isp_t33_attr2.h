/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 ioctl attribute wrappers (gamma, autozoom, module ratio): copy the attribute
 * block to / from user space around the tisp_g_* / tisp_s_* calls.
 * The callees are passed in (module functions and kernel imports).
 */
#ifndef TX_ISP_T33_ATTR2_H
#define TX_ISP_T33_ATTR2_H

#include "tx_isp_t33_blk.h"

typedef int (*t33_attr_fn)(u32 a0, void *attr);
typedef int (*t33_attr_copy_fn)(void *to, void *from, unsigned long n);
typedef int (*t33_attr_print_fn)(int level, const char *fmt, const char *func, int line);

#define T33_ATTR_MAX	264U

/* get: tisp_g_*(a0, buf), then copy buf to the user pointer stored in the first word of user_blk */
static inline int t33_attr_get(u32 a0, const u8 *user_blk, u32 size, int check, int line1, int line2,
			       t33_attr_fn get, t33_attr_copy_fn copy_to, t33_attr_print_fn pr)
{
	u8 buf[T33_ATTR_MAX];
	int ret;

	ret = get(a0, buf);
	if (check && ret) {
		pr(1, "get attr failed", "attr_g", line1);
		return ret;
	}
	ret = copy_to((void *)(unsigned long)t33_le32(user_blk), buf, size);
	if (ret)
		pr(2, "copy_to_user failed", "attr_g", line2);
	return ret;
}

/* set: copy size bytes from the user pointer into buf, then tisp_s_*(a0, buf) */
static inline int t33_attr_set(u32 a0, u32 user, u32 size, int line1, int line2,
			       t33_attr_fn set, t33_attr_copy_fn copy_from, t33_attr_print_fn pr)
{
	u8 buf[T33_ATTR_MAX];
	int ret;

	ret = copy_from(buf, (void *)(unsigned long)user, size);
	if (ret) {
		pr(2, "copy_from_user failed", "attr_s", line1);
		return ret;
	}
	ret = set(a0, buf);
	if (ret)
		pr(1, "set attr failed", "attr_s", line2);
	return ret;
}

/* tx_isp_gamma_g_attr.isra.40 */
static inline int t33_gamma_g_attr(u32 a0, const u8 *user_blk, t33_attr_fn get, t33_attr_copy_fn copy_to,
				   t33_attr_print_fn pr)
{
	return t33_attr_get(a0, user_blk, 264, 1, 518, 524, get, copy_to, pr);
}

/* tx_isp_gamma_s_attr.isra.8 */
static inline int t33_gamma_s_attr(u32 a0, u32 user, t33_attr_fn set, t33_attr_copy_fn copy_from,
				   t33_attr_print_fn pr)
{
	return t33_attr_set(a0, user, 264, 537, 541, set, copy_from, pr);
}

/* tx_isp_autozoom_g_attr.isra.42 */
static inline int t33_autozoom_g_attr(u32 a0, const u8 *user_blk, t33_attr_fn get, t33_attr_copy_fn copy_to,
				      t33_attr_print_fn pr)
{
	return t33_attr_get(a0, user_blk, 60, 1, 608, 614, get, copy_to, pr);
}

/* tx_isp_module_ratio_g_attr.isra.56: the getter's result is not checked */
static inline int t33_ratio_g_attr(u32 a0, const u8 *user_blk, t33_attr_fn get, t33_attr_copy_fn copy_to,
				   t33_attr_print_fn pr)
{
	return t33_attr_get(a0, user_blk, 128, 0, 0, 506, get, copy_to, pr);
}

/* tx_isp_module_ratio_s_attr.isra.9 */
static inline int t33_ratio_s_attr(u32 a0, u32 user, t33_attr_fn set, t33_attr_copy_fn copy_from,
				   t33_attr_print_fn pr)
{
	return t33_attr_set(a0, user, 128, 488, 492, set, copy_from, pr);
}

#endif /* TX_ISP_T33_ATTR2_H */
