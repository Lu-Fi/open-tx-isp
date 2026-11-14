/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "attrAE1": AE explist / expoinfo ioctl attr glue. */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_attrAE1.h"

extern int tisp_g_ae_explist(u32 ch, u8 *buf);
extern int tisp_s_ae_explist(u32 ch, u8 *buf);
extern int tisp_g_ae_exprinfo_attr(u32 ch, u8 *buf);
extern int tisp_s_ae_exprinfo_attr(u32 ch, u8 *buf);
extern int private_copy_to_user(void *to, const void *from, unsigned long n);
extern int private_copy_from_user(void *to, const void *from, unsigned long n);
extern int isp_printf(int level, const char *fmt, ...);

static int bo_pr(int level, const char *fmt, const char *fn, int line)
{
	return isp_printf(level, fmt, fn, line);
}

int bo_tx_isp_ae_explist_g_attr(u32 ch, u8 *arg)
{
	return t33_ae_explist_get(ch, arg, tisp_g_ae_explist, private_copy_to_user, bo_pr);
}

int bo_tx_isp_ae_explist_s_attr(u32 ch, u32 user)
{
	return t33_ae_explist_set(ch, (const void *)(unsigned long)user, tisp_s_ae_explist,
				  private_copy_from_user, bo_pr);
}

int bo_tx_isp_ae_expoinfo_g_attr(u32 ch, u8 *arg)
{
	return t33_ae_expoinfo_get(ch, arg, tisp_g_ae_exprinfo_attr, private_copy_to_user, bo_pr);
}

int bo_tx_isp_ae_expoinfo_s_attr(u32 ch, u32 user)
{
	return t33_ae_expoinfo_set(ch, (const void *)(unsigned long)user, tisp_s_ae_exprinfo_attr,
				   private_copy_from_user, bo_pr);
}
