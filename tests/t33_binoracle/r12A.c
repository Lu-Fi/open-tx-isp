/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "r12A": the core tuning device functions with the vendor calling convention.
 * The kernel callees are extern (vendor names, recorded by the oracle).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_r12A.h"

extern u32 private_copy_from_user(void *dst, u32 src, u32 n);
extern u32 private_copy_to_user(u32 dst, const void *src, u32 n);
extern int tx_isp_core_ops_s_ctrl(u32 id, void *tun, void *val);
extern int tx_isp_core_ops_g_ctrl(u32 id, void *tun, void *val);
extern void tisp_enable_tuning(void);
extern void tisp_disable_tuning(void);
extern int isp_printf(u32 level, const char *fmt, ...);
extern void tisp_day_or_night_s_ctrl(u32 ch, u32 mode, u32 mode8);
extern void private_complete(void *c);
extern void isp_frame_done_wakeup(void *data);
extern void *private_kmalloc(u32 size, u32 gfp);
extern void private_spin_lock_init(void *l);
extern void private_raw_mutex_init(void *m, const char *name, void *key);
extern void private_init_completion(void *c);

u8 *ispcore_sd;
static u8 bo_fops[4];

static void bo_log(u32 level, const char *fmt, const char *func, u32 line)
{
	isp_printf(level, fmt, func, line);
}

static const struct t33_tun_ops bo_ops = {
	private_copy_from_user, private_copy_to_user, tx_isp_core_ops_s_ctrl, tx_isp_core_ops_g_ctrl,
	tisp_enable_tuning, tisp_disable_tuning, bo_log, tisp_day_or_night_s_ctrl, private_complete,
	isp_frame_done_wakeup, private_kmalloc, private_spin_lock_init, private_raw_mutex_init,
	private_init_completion,
};

int bo_isp_core_tunning_default_ioctl(u32 tun, u32 cmd, u32 arg)
{
	(void)cmd;
	return t33_tun_default_ioctl(&bo_ops, (u8 *)(unsigned long)tun, arg);
}

int bo_isp_core_tunning_unlocked_ioctl(u8 *file, u32 cmd, u32 arg)
{
	return t33_tun_unlocked_ioctl(&bo_ops, ispcore_sd, file, cmd, arg);
}

int bo_isp_core_tunning_open(u32 inode, u8 *file)
{
	(void)inode;
	return t33_tun_open(&bo_ops, file);
}

int bo_isp_core_tunning_release(u32 inode, u8 *file)
{
	(void)inode;
	return t33_tun_release(&bo_ops, file);
}

int bo_isp_core_tuning_event(u8 *tun, u32 ev, u8 *data)
{
	return t33_tun_event(&bo_ops, tun, ev, data);
}

u32 bo_isp_core_tuning_init(u32 owner)
{
	return (u32)(unsigned long)t33_tun_init(&bo_ops, owner, (u32)(unsigned long)bo_fops,
						(u32)(unsigned long)bo_isp_core_tuning_event);
}
