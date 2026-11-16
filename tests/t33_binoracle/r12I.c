/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "r12I": frame done wakeup, fw process, irq handle, fd / fs work 0,
 * single cam frame drop check and vic index of driver/t32/tx_isp_t33_r12I.h with the vendor calling
 * convention (spec: r12I.conf).  Kernel / module callees are extern with the vendor names.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_r12I.h"

extern void __wake_up(void *wq, unsigned int mode, int nr, void *key);
extern int private_kthread_should_stop(void);
extern void lib_tisp_fw_process(u32 ch);
extern int tisp_msca_state(void);
extern void tisp_msca_Shd_ctrl(u32 v);
extern int ispcore_sensor_ops_ioctl(u32 core, u32 cmd, u32 *arg);
extern void tx_isp_core_shvflip(u32 ch);
extern void ispcoer_debug_info_fs_trige(u32 priv, u32 ch);
extern void tisp_core_reset(u32 priv, u32 arg);
extern int __ispcore_get_frame_drop(u8 *core);
extern void tisp_get_frame_drop(u32 a, u32 b, u32 *attr);
extern void tisp_set_frame_drop(u32 a, u32 b, u32 *attr);
extern int isp_printf(int level, const char *fmt, ...);

u32 t33_frame_done_cnt[4] = { 0 };
volatile u32 t33_frame_done_cond = 0;
volatile u32 t33_frame_done_cond_sec = 0;
u32 t33_frame_done_wq[2] = { 0 };
u32 t33_frame_done_wq_sec[2] = { 0 };
u32 t33_ispcore_sd = 0;

static void h_wake(void *wq, u32 mode, u32 nr, void *key) { __wake_up(wq, mode, (int)nr, key); }

void bo_isp_frame_done_wakeup(u8 *data)
{
	struct t33_fdwake_ops ops;

	ops.wake_up = h_wake;
	t33_isp_frame_done_wakeup(&ops, (const u32 *)data, t33_frame_done_cnt, &t33_frame_done_cond,
				  &t33_frame_done_cond_sec, t33_frame_done_wq, t33_frame_done_wq_sec);
}

static int h_stop(void) { return private_kthread_should_stop(); }
static void h_fw(u32 ch) { lib_tisp_fw_process(ch); }

int bo_isp_fw_process(u32 data)
{
	struct t33_fw_ops ops;

	(void)data;
	ops.should_stop = h_stop;
	ops.fw_process = h_fw;
	return t33_isp_fw_process(&ops);
}

int bo_isp_irq_handle(u32 irq, u8 *dev_id, u32 cb)
{
	(void)cb;
	return t33_isp_irq_handle(irq, T32_IC_A(dev_id));
}

static int h_msca_state(void) { return tisp_msca_state(); }
static void h_msca_shd(u32 v) { tisp_msca_Shd_ctrl(v); }
static void h_ioctl(u32 core, u32 cmd, u32 *arg) { ispcore_sensor_ops_ioctl(core, cmd, arg); }
static void h_flip(u32 ch) { tx_isp_core_shvflip(ch); }
static void h_trige(u32 priv, u32 ch) { ispcoer_debug_info_fs_trige(priv, ch); }
static void h_reset(u32 priv, u32 a) { tisp_core_reset(priv, a); }

static void work_ops(struct t33_work_ops *ops)
{
	ops->msca_state = h_msca_state;
	ops->msca_shd_ctrl = h_msca_shd;
	ops->sensor_ioctl = h_ioctl;
	ops->shvflip = h_flip;
	ops->fs_trige = h_trige;
	ops->core_reset = h_reset;
}

void bo_ispcore_irq_fd_work0(u32 work)
{
	struct t33_work_ops ops;

	(void)work;
	work_ops(&ops);
	t33_ispcore_irq_fd_work0(&ops, t33_ispcore_sd);
}

void bo_ispcore_irq_fs_work0(u32 work)
{
	struct t33_work_ops ops;

	(void)work;
	work_ops(&ops);
	t33_ispcore_irq_fs_work0(&ops, t33_ispcore_sd);
}

static void h_get_drop(u32 core) { __ispcore_get_frame_drop((u8 *)(uintptr_t)core); }
static void h_tget(u32 a, u32 b, u32 *attr) { tisp_get_frame_drop(a, b, attr); }
static void h_tset(u32 a, u32 b, u32 *attr) { tisp_set_frame_drop(a, b, attr); }

void bo___ispcore_single_cam_check_frame_drop(u8 *core)
{
	struct t33_fd_ops ops;

	ops.get_drop = h_get_drop;
	ops.tisp_get = h_tget;
	ops.tisp_set = h_tset;
	t33_ispcore_single_cam_check_frame_drop(&ops, T32_IC_A(core));
}

static void h_warn(u32 line)
{
	isp_printf(2, "[ %s:%d ] vic index error\n", "tx_isp_vic_index_cal", (int)line);
}

int bo_tx_isp_vic_index_cal(u8 *vic, u32 idx)
{
	return t33_tx_isp_vic_index_cal(T32_IC_A(vic), idx, h_warn);
}
