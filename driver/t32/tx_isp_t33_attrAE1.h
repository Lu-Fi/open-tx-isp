/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 AE ioctl attribute glue: exposure list and exposure info, get / set
 * (vendor tx_isp_ae_explist_g_attr / _s_attr / tx_isp_ae_expoinfo_g_attr /
 * _s_attr, the *.isra.N copies).  The callees (tisp getters / setters, user
 * copies, isp_printf) are passed in by the caller.
 */
#ifndef TX_ISP_T33_ATTRAE1_H
#define TX_ISP_T33_ATTRAE1_H

#include "tx_isp_t33_blk.h"

#define T33_AE_EXPLIST_BYTES	324U
#define T33_AE_EXPOINFO_BYTES	204U
#define T33_ATTR_LVL_CALL	1
#define T33_ATTR_LVL_COPY	2

typedef int (*t33_ae_call_fn)(u32 ch, u8 *buf);
typedef int (*t33_ae_to_user_fn)(void *user, const void *src, unsigned long n);
typedef int (*t33_ae_from_user_fn)(void *dst, const void *user, unsigned long n);
typedef int (*t33_ae_pr_fn)(int level, const char *fmt, const char *fn, int line);

static inline void t33_ae_copy_bytes(u8 *dst, const u8 *src, unsigned int n)
{
	unsigned int k;

	for (k = 0; k < n; k++)
		dst[k] = src[k];
}

/* tx_isp_ae_explist_g_attr: getter, then copy out to the pointer in the first word of arg */
static inline int t33_ae_explist_get(u32 ch, const u8 *arg, t33_ae_call_fn get, t33_ae_to_user_fn out,
				     t33_ae_pr_fn pr)
{
	u8 buf[T33_AE_EXPLIST_BYTES];
	int eg_r, eg_cp;

	eg_r = get(ch, buf);
	if (eg_r) {
		pr(T33_ATTR_LVL_CALL, "%s", "", 1318);
		return eg_r;
	}
	eg_cp = out((void *)(unsigned long)t33_le32(arg), buf, T33_AE_EXPLIST_BYTES);
	if (eg_cp)
		pr(T33_ATTR_LVL_COPY, "%s", "", 1324);
	return eg_cp;
}

/* tx_isp_ae_explist_s_attr: copy in, setter is called even after a failed copy */
static inline int t33_ae_explist_set(u32 ch, const void *user, t33_ae_call_fn set, t33_ae_from_user_fn in,
				     t33_ae_pr_fn pr)
{
	u8 buf[T33_AE_EXPLIST_BYTES];
	int es_cp;

	es_cp = in(buf, user, T33_AE_EXPLIST_BYTES);
	if (es_cp)
		pr(T33_ATTR_LVL_COPY, "%s", "", 1299);
	set(ch, buf);
	if (es_cp)
		pr(T33_ATTR_LVL_CALL, "%s", "", 1304);
	return es_cp;
}

/* tx_isp_ae_expoinfo_g_attr */
static inline int t33_ae_expoinfo_get(u32 ch, const u8 *arg, t33_ae_call_fn get, t33_ae_to_user_fn out,
				      t33_ae_pr_fn pr)
{
	u8 buf[T33_AE_EXPOINFO_BYTES], cp[T33_AE_EXPOINFO_BYTES];
	int ig_r, ig_cp;

	ig_r = get(ch, buf);
	if (ig_r) {
		pr(T33_ATTR_LVL_CALL, "%s", "", 971);
		return ig_r;
	}
	t33_ae_copy_bytes(cp, buf, T33_AE_EXPOINFO_BYTES);
	ig_cp = out((void *)(unsigned long)t33_le32(arg), cp, T33_AE_EXPOINFO_BYTES);
	if (ig_cp)
		pr(T33_ATTR_LVL_COPY, "%s", "", 978);
	return ig_cp;
}

/* tx_isp_ae_expoinfo_s_attr: a failed copy stops before the setter */
static inline int t33_ae_expoinfo_set(u32 ch, const void *user, t33_ae_call_fn set, t33_ae_from_user_fn in,
				      t33_ae_pr_fn pr)
{
	u8 buf[T33_AE_EXPOINFO_BYTES], cp[T33_AE_EXPOINFO_BYTES];
	int is_cp;

	is_cp = in(cp, user, T33_AE_EXPOINFO_BYTES);
	if (is_cp) {
		pr(T33_ATTR_LVL_COPY, "%s", "", 954);
		return is_cp;
	}
	t33_ae_copy_bytes(buf, cp, T33_AE_EXPOINFO_BYTES);
	set(ch, buf);
	return 0;
}

#endif /* TX_ISP_T33_ATTRAE1_H */
