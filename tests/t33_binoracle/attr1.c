/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "attr1": ioctl attribute wrappers (af weight, statis config) with the
 * calling convention of the vendor .isra clones; kernel services are extern with the vendor names.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_attr1.h"

extern unsigned long private_copy_from_user(void *dst, const void *src, unsigned long n);
extern unsigned long private_copy_to_user(void *dst, const void *src, unsigned long n);
extern int isp_printf(int level, const char *fmt, ...);
extern int tisp_g_af_weight_attr(u32 a0, void *buf);
extern int tisp_s_af_weight_attr(u32 a0, void *buf);
extern int tisp_g_statis_config_attr(u32 a0, void *buf);
extern int tisp_s_statis_config_attr(u32 a0, void *buf);

static int w_from(void *d, const void *s, u32 n) { return (int)private_copy_from_user(d, s, n); }
static int w_to(void *d, const void *s, u32 n) { return (int)private_copy_to_user(d, s, n); }
static void w_err(int level, int line) { isp_printf(level, "attr %d", line); }
static int w_g_af(u32 a0, void *b) { return tisp_g_af_weight_attr(a0, b); }
static int w_s_af(u32 a0, void *b) { return tisp_s_af_weight_attr(a0, b); }
static int w_g_st(u32 a0, void *b) { return tisp_g_statis_config_attr(a0, b); }
static int w_s_st(u32 a0, void *b) { return tisp_s_statis_config_attr(a0, b); }

int bo_af_weight_g(u32 a0, u8 *user)
{
	struct t33_attr_ops o;

	o.copy_from = w_from; o.copy_to = w_to; o.get = w_g_af; o.set = w_s_af; o.err = w_err;
	return t33_af_weight_g(&o, a0, user);
}

int bo_af_weight_s(u32 a0, u32 user)
{
	struct t33_attr_ops o;

	o.copy_from = w_from; o.copy_to = w_to; o.get = w_g_af; o.set = w_s_af; o.err = w_err;
	return t33_af_weight_s(&o, a0, user);
}

int bo_statis_config_g(u32 a0, u8 *user)
{
	struct t33_attr_ops o;

	o.copy_from = w_from; o.copy_to = w_to; o.get = w_g_st; o.set = w_s_st; o.err = w_err;
	return t33_statis_config_g(&o, a0, user);
}

int bo_statis_config_s(u32 a0, u32 user)
{
	struct t33_attr_ops o;

	o.copy_from = w_from; o.copy_to = w_to; o.get = w_g_st; o.set = w_s_st; o.err = w_err;
	return t33_statis_config_s(&o, a0, user);
}
