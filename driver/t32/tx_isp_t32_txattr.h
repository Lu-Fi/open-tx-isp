/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 tx-isp core-control attribute wrappers (vendor tx_isp_*_s_attr /
 * tx_isp_*_g_attr, .isra clones): copy the user block into a kernel buffer
 * and hand it to the tisp setter, or fetch it from the tisp getter and copy
 * it out to the user pointer at +0 of the control argument.
 *
 *   gamma            264 B   tisp_s_Gamma / tisp_g_Gamma
 *   module ratio     128 B   tisp_s_module_ratio_attr / tisp_g_module_ratio_attr
 *   AF weight        225 B   tisp_s_af_weight_attr / tisp_g_af_weight_attr
 *   statis config     72 B   tisp_s_statis_config_attr / tisp_g_statis_config_attr
 *   autozoom          60 B   tisp_g_autozoom_control
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C,
 * PR #15) by tests/t32_oracle (t32_txattr_oracle): hook calls and their
 * buffers, prints, user memory, return values.
 */
#ifndef TX_ISP_T32_TXATTR_H
#define TX_ISP_T32_TXATTR_H

#include "tx_isp_t32_txos.h"

#define T32_ATTR_GAMMA		264U
#define T32_ATTR_MODULE_RATIO	128U
#define T32_ATTR_AF_WEIGHT	225U
#define T32_ATTR_STATIS		72U
#define T32_ATTR_AUTOZOOM	60U
#define T32_ATTR_MAX		264U

typedef int (*t32_tx_tisp_attr_fn)(u32 ch, void *buf);

/* provided by the driver glue (kernel) or by the oracle (host) */
u32 t32_tx_os_copy_from_user(void *dst, u32 user, u32 n);
u32 t32_tx_os_copy_to_user(u32 user, const void *src, u32 n);
int t32_tx_os_tisp_s_gamma(u32 ch, void *buf);
int t32_tx_os_tisp_g_gamma(u32 ch, void *buf);
int t32_tx_os_tisp_s_module_ratio(u32 ch, void *buf);
int t32_tx_os_tisp_g_module_ratio(u32 ch, void *buf);
int t32_tx_os_tisp_s_af_weight(u32 ch, void *buf);
int t32_tx_os_tisp_g_af_weight(u32 ch, void *buf);
int t32_tx_os_tisp_s_statis_config(u32 ch, void *buf);
int t32_tx_os_tisp_g_statis_config(u32 ch, void *buf);
int t32_tx_os_tisp_g_autozoom(u32 ch, void *buf);

/* setter whose tisp result is the return value (gamma, module ratio) */
static inline int t32_tx_attr_set_checked(u32 ch, u32 user, u32 n, t32_tx_tisp_attr_fn set,
					  const char *fn, int line, const char *fail)
{
	u8 buf[T32_ATTR_MAX];
	int r = (int)t32_tx_os_copy_from_user(buf, user, n);

	if (r) {
		t32_tx_os_isp_printf(2, "[ %s:%d ] copy error!!!\n", fn, line);
		return r;
	}
	r = set(ch, buf);
	if (r)
		t32_tx_os_isp_printf(1, fail, fn, line + 4);
	return r;
}

/* setter that ignores the tisp result (AF weight, statis config) */
static inline int t32_tx_attr_set(u32 ch, u32 user, u32 n, t32_tx_tisp_attr_fn set, const char *fn, int line)
{
	u8 buf[T32_ATTR_MAX];
	int r = (int)t32_tx_os_copy_from_user(buf, user, n);

	if (r) {
		t32_tx_os_isp_printf(2, "[ %s:%d ] copy error!!!\n", fn, line);
		return r;
	}
	set(ch, buf);
	return 0;
}

/* getter: tisp result checked, then the copy to *(arg + 0) */
static inline int t32_tx_attr_get(u32 ch, u32 arg, u32 n, t32_tx_tisp_attr_fn get,
				  const char *fn, int line, const char *fail)
{
	u8 buf[T32_ATTR_MAX];
	int r = get(ch, buf);

	if (r) {
		t32_tx_os_isp_printf(1, fail, fn, line);
		return r;
	}
	r = (int)t32_tx_os_copy_to_user(t32_tx_ld(arg, 0), buf, n);
	if (r)
		t32_tx_os_isp_printf(2, "[ %s:%d ] copy error!!!\n", fn, line + 6);
	return r;
}

/* tx_isp_gamma_s_attr */
static inline int t32_tx_gamma_s_attr(u32 ch, u32 user)
{
	return t32_tx_attr_set_checked(ch, user, T32_ATTR_GAMMA, t32_tx_os_tisp_s_gamma,
				       "tx_isp_gamma_s_attr", 537, "[ %s:%d ] set gamma failed!!!\n");
}

/* tx_isp_module_ratio_s_attr */
static inline int t32_tx_module_ratio_s_attr(u32 ch, u32 user)
{
	return t32_tx_attr_set_checked(ch, user, T32_ATTR_MODULE_RATIO, t32_tx_os_tisp_s_module_ratio,
				       "tx_isp_module_ratio_s_attr", 488, "[ %s:%d ] set failed!!!\n");
}

/* tx_isp_af_weight_s_attr */
static inline int t32_tx_af_weight_s_attr(u32 ch, u32 user)
{
	return t32_tx_attr_set(ch, user, T32_ATTR_AF_WEIGHT, t32_tx_os_tisp_s_af_weight,
			       "tx_isp_af_weight_s_attr", 795);
}

/* tx_isp_statis_config_s_attr */
static inline int t32_tx_statis_config_s_attr(u32 ch, u32 user)
{
	return t32_tx_attr_set(ch, user, T32_ATTR_STATIS, t32_tx_os_tisp_s_statis_config,
			       "tx_isp_statis_config_s_attr", 1110);
}

/* tx_isp_gamma_g_attr */
static inline int t32_tx_gamma_g_attr(u32 ch, u32 arg)
{
	return t32_tx_attr_get(ch, arg, T32_ATTR_GAMMA, t32_tx_os_tisp_g_gamma,
			       "tx_isp_gamma_g_attr", 518, "[ %s:%d ] get gamma failed!!!\n");
}

/* tx_isp_autozoom_g_attr */
static inline int t32_tx_autozoom_g_attr(u32 ch, u32 arg)
{
	return t32_tx_attr_get(ch, arg, T32_ATTR_AUTOZOOM, t32_tx_os_tisp_g_autozoom,
			       "tx_isp_autozoom_g_attr", 644, "[ %s:%d ] get control failed!!!\n");
}

/* tx_isp_af_weight_g_attr */
static inline int t32_tx_af_weight_g_attr(u32 ch, u32 arg)
{
	return t32_tx_attr_get(ch, arg, T32_ATTR_AF_WEIGHT, t32_tx_os_tisp_g_af_weight,
			       "tx_isp_af_weight_g_attr", 810, "[ %s:%d ] get control failed!!!\n");
}

/* tx_isp_statis_config_g_attr */
static inline int t32_tx_statis_config_g_attr(u32 ch, u32 arg)
{
	return t32_tx_attr_get(ch, arg, T32_ATTR_STATIS, t32_tx_os_tisp_g_statis_config,
			       "tx_isp_statis_config_g_attr", 1125, "[ %s:%d ] get control failed!!!\n");
}

/* tx_isp_module_ratio_g_attr: the tisp result is not checked */
static inline int t32_tx_module_ratio_g_attr(u32 ch, u32 arg)
{
	u8 buf[T32_ATTR_MODULE_RATIO];
	int r;

	t32_tx_os_tisp_g_module_ratio(ch, buf);
	r = (int)t32_tx_os_copy_to_user(t32_tx_ld(arg, 0), buf, T32_ATTR_MODULE_RATIO);
	if (r)
		t32_tx_os_isp_printf(2, "[ %s:%d ] get failed!!!\n", "tx_isp_module_ratio_g_attr", 506);
	return r;
}

#endif
