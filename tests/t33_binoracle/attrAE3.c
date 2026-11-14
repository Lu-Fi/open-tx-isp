/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "attrAE3": AE weight / AWB attribute ioctl glue with the vendor calling
 * convention; the kernel callees are extern (recorded by the oracle).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_attrAE3.h"

extern int private_copy_from_user(void *dst, const void *src, unsigned long n);
extern int private_copy_to_user(void *dst, const void *src, unsigned long n);
extern void isp_printf(int level, const char *fmt, const char *fn, int line);
extern int tisp_g_ae_weight_attr(u32 ch, u8 *b);
extern int tisp_s_ae_weight_attr(u32 ch, u8 *b);
extern int tisp_g_awb_attr(u32 ch, u8 *b);
extern int tisp_s_awb_attr(u32 ch, u8 *b);

static int h_in(void *p, u8 *dst, u32 user, u32 n)
{
	(void)p;
	return private_copy_from_user(dst, (const void *)(unsigned long)user, n);
}

static int h_out(void *p, u32 user, const u8 *src, u32 n)
{
	(void)p;
	return private_copy_to_user((void *)(unsigned long)user, src, n);
}

static void h_print(void *p, u32 level)
{
	(void)p;
	isp_printf((int)level, "", "", 0);
}

static int h_g_ae(void *p, u32 ch, u8 *b) { (void)p; return tisp_g_ae_weight_attr(ch, b); }
static int h_s_ae(void *p, u32 ch, u8 *b) { (void)p; return tisp_s_ae_weight_attr(ch, b); }
static int h_g_awb(void *p, u32 ch, u8 *b) { (void)p; return tisp_g_awb_attr(ch, b); }
static int h_s_awb(void *p, u32 ch, u8 *b) { (void)p; return tisp_s_awb_attr(ch, b); }

static u32 rd32(const u8 *p) { return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; }

int bo_tx_isp_ae_weight_g_attr_isra_54(u32 ch, u8 *userp)
{
	struct t32_isra_ops o = { 0 };
	u8 loc[464], out[240];

	memset(loc, 0, sizeof(loc));
	memset(out, 0, sizeof(out));
	o.user_out = h_out; o.printk = h_print; o.call = h_g_ae;
	return t33_ae_weight_g_attr(&o, ch, rd32(userp), loc, out);
}

int bo_tx_isp_ae_weight_s_attr_isra_22(u32 ch, u32 user)
{
	struct t32_isra_ops o = { 0 };
	u8 in[240], loc[464];

	memset(in, 0, sizeof(in));
	memset(loc, 0, sizeof(loc));
	o.user_in = h_in; o.printk = h_print; o.call = h_s_ae;
	return t33_ae_weight_s_attr(&o, ch, user, in, loc);
}

int bo_tx_isp_awb_attr_g_attr_isra_17(u32 ch, u8 *userp)
{
	struct t32_isra_ops o = { 0 };
	u8 loc[64], out[64];

	memset(loc, 0, sizeof(loc));
	memset(out, 0, sizeof(out));
	o.user_out = h_out; o.printk = h_print; o.call = h_g_awb;
	return t33_awb_attr_g_attr(&o, ch, rd32(userp), loc, out);
}

int bo_tx_isp_awb_attr_s_attr_isra_19(u32 ch, u32 user)
{
	struct t32_isra_ops o = { 0 };
	u8 in[64], loc[64];

	memset(in, 0, sizeof(in));
	memset(loc, 0, sizeof(loc));
	o.user_in = h_in; o.printk = h_print; o.call = h_g_awb; o.call2 = h_s_awb;
	return t33_awb_attr_s_attr(&o, ch, user, in, loc);
}
