/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "bin": default bin path get/set and bin switch
 * (driver/t32/tx_isp_t33_bin.h) with the vendor calling convention, built
 * for mipsel and run against the vendor tx-isp-t33.ko instructions.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_bin.h"

extern int private_copy_from_user(void *to, const void *from, unsigned long n);
extern int private_copy_to_user(void *to, const void *from, unsigned long n);
extern int sprintf(char *buf, const char *fmt, ...);
extern int isp_printf(int level, const char *fmt, ...);
extern unsigned long msecs_to_jiffies(unsigned int ms);
extern unsigned long private_wait_for_completion_timeout(void *done, unsigned long timeout);
extern int tisp_switch_bin(unsigned int ch, const u8 *req);

static const struct t33_bin_ops bin_ops = {
	.cfu = private_copy_from_user,
	.ctu = private_copy_to_user,
	.sprintf = (int (*)(char *, const char *, const char *))sprintf,
	.memcpy = (void *(*)(void *, const void *, unsigned long))memcpy,
	.isp_printf = isp_printf,
	.msecs_to_jiffies = msecs_to_jiffies,
	.wait_for_completion_timeout = private_wait_for_completion_timeout,
	.switch_bin = tisp_switch_bin,
};

int bo_tx_isp_get_default_bin_path(u8 *ctx, u8 *user)
{
	return t33_bin_get_path(ctx, user, &bin_ops);
}

int bo_tx_isp_set_default_bin_path(u8 *ctx, u8 *user)
{
	return t33_bin_set_path(ctx, user, &bin_ops);
}

int bo_tx_isp_switch_bin(u32 ch, u8 *ctx, u8 *user)
{
	return t33_bin_switch(ch, ctx, user, &bin_ops);
}
