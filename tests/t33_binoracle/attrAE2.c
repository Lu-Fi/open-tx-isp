/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "attrAE2": AE ioctl attribute glue (driver/t32/tx_isp_t33_attrAE2.h) with the
 * calling convention of the vendor isra functions.  The tisp_* calls, the user copies and isp_printf are
 * extern (recorded by the oracle on both sides).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_attrAE2.h"

extern int tisp_g_ae_scence_attr(u32 ch, void *buf);
extern int tisp_s_ae_scence_attr(u32 ch, void *buf);
extern int tisp_g_ae_exprinfo_attr(u32 ch, void *buf);
extern int tisp_g_at_list(u32 ch, void *buf);
extern int tisp_s_at_list(u32 ch, void *buf);
extern int tisp_g_ev_list(u32 ch, void *buf);
extern int tisp_s_ev_list(u32 ch, void *buf);
extern int tisp_g_bv(u32 ch, void *buf);
extern int tisp_get_ae_stable(u32 ch, void *buf);
extern int private_copy_to_user(void *to, const void *from, unsigned long n);
extern int private_copy_from_user(void *to, const void *from, unsigned long n);
extern int isp_printf(int level, const char *fmt, ...);

static int w_in(void *p, u8 *d, u32 user, u32 n)
{
	(void)p;
	return private_copy_from_user(d, (const void *)(unsigned long)user, n);
}

static int w_out(void *p, u32 user, const u8 *s, u32 n)
{
	(void)p;
	return private_copy_to_user((void *)(unsigned long)user, s, n);
}

static void w_print(void *p, u32 level)
{
	(void)p;
	isp_printf((int)level, "attrAE2");
}

static int w_call(void *p, u32 id, u32 ch, u8 *buf)
{
	(void)p;
	switch (id) {
	case T33_AE2_C_SCENE_G: return tisp_g_ae_scence_attr(ch, buf);
	case T33_AE2_C_SCENE_S: return tisp_s_ae_scence_attr(ch, buf);
	case T33_AE2_C_EXPR_G: return tisp_g_ae_exprinfo_attr(ch, buf);
	case T33_AE2_C_AT_G: return tisp_g_at_list(ch, buf);
	case T33_AE2_C_AT_S: return tisp_s_at_list(ch, buf);
	case T33_AE2_C_EV_G: return tisp_g_ev_list(ch, buf);
	case T33_AE2_C_EV_S: return tisp_s_ev_list(ch, buf);
	case T33_AE2_C_BV: return tisp_g_bv(ch, buf);
	default: return tisp_get_ae_stable(ch, buf);
	}
}

static const struct t33_ae2_ops ops = { w_in, w_out, w_print, w_call, 0 };

static u8 hlc_out[8];	/* the vendor's uninitialised stack block: persists between calls */

int bo_tx_isp_ae_hlc_or_blc_g_attr_isra_28(u32 ch, u8 *blk)
{
	u8 scn[40];

	return t33_ae_hlc_or_blc_g(&ops, ch, t33_le32(blk), scn, hlc_out);
}

int bo_tx_isp_ae_only_read_g_attr_isra_58(u32 ch, u8 *blk)
{
	u8 expo[220], scn[40], stable[12], bv[4], out[48];

	memset(out, 0, sizeof(out));
	return t33_ae_only_read_g(&ops, ch, t33_le32(blk), expo, scn, stable, bv, out);
}

int bo_tx_isp_ae_scence_g_attr_isra_27(u32 ch, u8 *blk)
{
	u8 scn[40];

	return t33_ae_scene_g(&ops, ch, t33_le32(blk), scn);
}

int bo_tx_isp_ae_target_g_attr_isra_60(u32 ch, u8 *blk)
{
	u8 scn[40], at[128], ev[128], out[264];

	return t33_ae_target_g(&ops, ch, t33_le32(blk), scn, at, ev, out);
}

int bo_tx_isp_ae_target_s_attr_isra_29(u32 ch, u32 user)
{
	u8 in[264], scn[40], at[128], ev[128];

	return t33_ae_target_s(&ops, ch, user, in, scn, at, ev);
}
