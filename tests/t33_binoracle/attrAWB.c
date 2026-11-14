/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "attrAWB": AWB / CCM ioctl attribute glue with the vendor calling convention
 * (ch, u32 *arg) / (ch, user).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_attrAWB.h"

extern int tisp_g_awb_attr(u32 ch, void *buf);
extern int tisp_g_awb_weight_attr(u32 ch, void *buf);
extern int tisp_s_awb_weight_attr(u32 ch, const void *buf);
extern int tisp_g_ccm_attr(u32 ch, void *buf);
extern int private_copy_to_user(void *to, const void *from, unsigned long n);
extern int private_copy_from_user(void *to, const void *from, unsigned long n);
extern int isp_printf(int level, const char *fmt, ...);

static int w_g_awb(u32 ch, u8 *b) { return tisp_g_awb_attr(ch, b); }
static int w_g_weight(u32 ch, u8 *b) { return tisp_g_awb_weight_attr(ch, b); }
static int w_s_weight(u32 ch, const u8 *b) { return tisp_s_awb_weight_attr(ch, b); }
static int w_g_ccm(u32 ch, u8 *b) { return tisp_g_ccm_attr(ch, b); }
static int w_to_user(u32 user, const u8 *s, u32 n) { return private_copy_to_user((void *)(uintptr_t)user, s, n); }
static int w_from_user(u8 *d, u32 user, u32 n) { return private_copy_from_user(d, (const void *)(uintptr_t)user, n); }
static void w_printk(u32 level) { isp_printf((int)level, "x"); }

static const struct t33_awbattr_ops bo_ops = {
	w_g_awb, w_g_weight, w_s_weight, w_g_ccm, w_to_user, w_from_user, w_printk,
};

int bo_tx_isp_awb_only_read_attr(u32 ch, u32 *arg) { return t33_awb_only_read_attr(&bo_ops, ch, arg[0]); }
int bo_tx_isp_awb_weight_g_attr(u32 ch, u32 *arg) { return t33_awb_weight_g_attr(&bo_ops, ch, arg[0]); }
int bo_tx_isp_awb_weight_s_attr(u32 ch, u32 user) { return t33_awb_weight_s_attr(&bo_ops, ch, user); }
int bo_tiziano_isp_ccm_attr_g_ctrl(u32 ch, u32 *arg) { return t33_ccm_attr_g_ctrl(&bo_ops, ch, arg[0]); }
