/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 ISP core glue (block r12I): frame-done wakeup, firmware thread, irq dispatch,
 * frame done / frame start sensor work 0, single camera frame drop check and the VIC index.
 * Readable C of the vendor functions isp_frame_done_wakeup, isp_fw_process,
 * isp_irq_handle, ispcore_irq_fd_work0, ispcore_irq_fs_work0,
 * __ispcore_single_cam_check_frame_drop and tx_isp_vic_index_cal of tx-isp-t33.ko.
 * (isp_frame_done_wait cannot run in the binary oracle and is not part of this block.)
 *
 * Addresses are 32-bit words (the module runs on MIPS32), as in tx_isp_t32_ispcore.h.  T33 layout:
 * the subdev keeps its ops table at +260 and the core private block at +276, the irq cookie is
 * subdev + 152, the pending sensor updates are at priv + 1300, the channel mode pointer at priv + 372.
 * Kernel and module calls are hooks.
 */
#ifndef TX_ISP_T33_R12C_H
#define TX_ISP_T33_R12C_H

#include "tx_isp_t32_ispcore.h"

#define T33_TASK_NORMAL		3

/* ---- isp_frame_done_wakeup ---- */
struct t33_fdwake_ops {
	void (*wake_up)(void *wq, u32 mode, u32 nr, void *key);	/* __wake_up */
};

/* isp_frame_done_wakeup(data): data word 1 = channel; count the frame, raise the flag, wake */
static inline void t33_isp_frame_done_wakeup(const struct t33_fdwake_ops *ops, const u32 *data, u32 *cnt,
					     volatile u32 *cond0, volatile u32 *cond1, void *wq0, void *wq1)
{
	u32 ch = data[1];
	u32 *c = cnt + 2 * ch;
	u32 lo = c[0];

	c[0] = lo + 1;
	c[1] += c[0] < lo;
	if (ch) {
		*cond1 = 1;
		ops->wake_up(wq1, T33_TASK_NORMAL, 1, NULL);
	} else {
		*cond0 = 1;
		ops->wake_up(wq0, T33_TASK_NORMAL, 1, NULL);
	}
}

/* ---- isp_fw_process ---- */
struct t33_fw_ops {
	int (*should_stop)(void);	/* private_kthread_should_stop */
	void (*fw_process)(u32 ch);	/* lib_tisp_fw_process */
};

static inline int t33_isp_fw_process(const struct t33_fw_ops *ops)
{
	while (!ops->should_stop())
		ops->fw_process(0);
	return 0;
}

/* ---- isp_irq_handle ---- */
#define T33_SD_IRQ_COOKIE	152
#define T33_SD_OPS		260
#define T33_SD_PRIV		276
#define T33_OPS_SENSOR		12
#define T33_SENSOR_IOCTL	8
#define T33_CORE_ISR 36
#define T33_IRQ_HANDLED 1
#define T33_IRQ_WAKE_THREAD	2

/* isp_irq_handle(irq, dev_id): the core's interrupt_service_routine decides HANDLED / WAKE_THREAD */
static inline int t33_isp_irq_handle(u32 irq, u32 dev_id)
{
	u32 sd = dev_id - T33_SD_IRQ_COOKIE, core_ops, isr;

	if (!sd)
		return T33_IRQ_HANDLED;
	core_ops = t32_ic_r32(t32_ic_r32(sd, T33_SD_OPS), 0);
	if (!core_ops)
		return T33_IRQ_HANDLED;
	isr = t32_ic_r32(core_ops, T33_CORE_ISR);
	if (!isr)
		return T33_IRQ_HANDLED;
	return ((int (*)(u32, u32, u32))(uintptr_t)isr)(sd, irq, 0) == T33_IRQ_WAKE_THREAD ?
	       T33_IRQ_WAKE_THREAD : T33_IRQ_HANDLED;
}

/* ---- ispcore_irq_fd_work0 / ispcore_irq_fs_work0 ---- */
#define T33_WORK_N		12
#define T33_PRIV_FS_WORK	1300
#define T33_PRIV_MODE		372	/* pointer, word +256 = sensor mode */
#define T33_MODE_WORD		256
#define T33_PRIV_RESET		4300	/* u8: core reset pending */
#define T33_CMD_FD		0x02000016U
#define T33_CMD_FS		0x02000015U
#define T33_CORE_OPS_EVENT	32
#define T33_CORE_EV_RESET	0x01000008U
#define T33_WK_FLIP		1
#define T33_WK_REUSE		0
#define T33_WK_NONE		0xffffffffU

struct t33_work_ops {
	int (*msca_state)(void);				/* tisp_msca_state */
	void (*msca_shd_ctrl)(u32 v);				/* tisp_msca_Shd_ctrl */
	void (*sensor_ioctl)(u32 core, u32 cmd, u32 *arg);	/* ispcore_sensor_ops_ioctl */
	void (*shvflip)(u32 ch);				/* tx_isp_core_shvflip */
	void (*fs_trige)(u32 priv, u32 ch);			/* ispcoer_debug_info_fs_trige */
	void (*core_reset)(u32 priv, u32 arg);			/* tisp_core_reset */
};

/* command per pending index (the T32 tables; not visible in the T33 disassembly) */
static const u32 t33_fd_cmd[10] = {
	0x02000007, 0x02000008, 0x02000009, 0x02000005, 0x02000006, T33_WK_FLIP,
	0x02000017, 0x02000018, T33_WK_REUSE, 0x0200000a,
};
static const u32 t33_fs_cmd[T33_WORK_N] = {
	0x02000007, 0x02000008, 0x02000009, 0x02000005, 0x02000006, T33_WK_NONE,
	0x02000017, 0x02000018, T33_WK_NONE, 0x0200000a, T33_WK_NONE, 0x02000020,
};

/* the core's sensor ops ioctl (ops table +12, entry +8), when it exists */
static inline void t33_work_notify(u32 core, u32 cmd, u32 *arg)
{
	u32 sops, fn;

	if (!core)
		return;
	sops = t32_ic_r32(t32_ic_r32(core, T33_SD_OPS), T33_OPS_SENSOR);
	fn = sops ? t32_ic_r32(sops, T33_SENSOR_IOCTL) : 0;
	if (fn)
		((int (*)(u32, u32, u32))(uintptr_t)fn)(core, cmd, T32_IC_A(arg));
}

static inline void t33_ispcore_irq_fd_work0(const struct t33_work_ops *ops, u32 core)
{
	u32 priv = t32_ic_r32(core, T33_SD_PRIV), arg[2] = { 0, 0 }, cmd = 0, idx, e, k;

	if (!ops->msca_state())
		ops->msca_shd_ctrl(0);
	t33_work_notify(core, T33_CMD_FD, arg);
	if (!priv)
		return;
	for (idx = 0; idx < T33_WORK_N; idx++) {
		e = priv + T33_PRIV_FS_WORK + 8 * idx;
		if (!t32_ic_r32(e, 0))
			continue;
		k = idx < 10 ? t33_fd_cmd[idx] : T33_WK_REUSE;
		if (k == T33_WK_FLIP) {
			ops->shvflip(0);
			continue;
		}
		if (k != T33_WK_REUSE)
			cmd = k;
		if (t32_ic_r32(t32_ic_r32(priv, T33_PRIV_MODE), T33_MODE_WORD))
			continue;
		arg[0] = 0;
		arg[1] = t32_ic_r32(e, 4);
		ops->sensor_ioctl(core, cmd, arg);
		t32_ic_w32(e, 0, 0);
	}
}

static inline void t33_ispcore_irq_fs_work0(const struct t33_work_ops *ops, u32 core)
{
	u32 priv = t32_ic_r32(core, T33_SD_PRIV), arg[2] = { 0, 0 }, idx, e, cmd, p, cops, fn;

	ops->fs_trige(priv, 0);
	t33_work_notify(core, T33_CMD_FS, arg);
	p = t32_ic_r32(core, T33_SD_PRIV);
	if (*(volatile u8 *)T32_IC_P(p + T33_PRIV_RESET)) {
		*(volatile u8 *)T32_IC_P(p + T33_PRIV_RESET) = 0;
		ops->core_reset(p, 0);
		if (core) {
			cops = t32_ic_r32(t32_ic_r32(core, T33_SD_OPS), 0);
			fn = cops ? t32_ic_r32(cops, T33_CORE_OPS_EVENT) : 0;
			if (fn)
				((int (*)(u32, u32, u32))(uintptr_t)fn)(core, T33_CORE_EV_RESET, 0);
		}
	}
	if (!priv)
		return;
	for (idx = 0; idx < T33_WORK_N; idx++) {
		e = priv + T33_PRIV_FS_WORK + 8 * idx;
		if (!t32_ic_r32(e, 0) || idx == 5)
			continue;
		if (t32_ic_r32(t32_ic_r32(priv, T33_PRIV_MODE), T33_MODE_WORD) != 1)
			continue;
		cmd = t33_fs_cmd[idx];
		if (cmd == T33_WK_NONE)
			continue;
		arg[0] = 0;
		arg[1] = t32_ic_r32(e, 4);
		ops->sensor_ioctl(core, cmd, arg);
		t32_ic_w32(e, 0, 0);
	}
}

/* ---- __ispcore_single_cam_check_frame_drop ---- */
struct t33_fd_ops {
	void (*get_drop)(u32 core);				/* __ispcore_get_frame_drop */
	void (*tisp_get)(u32 a, u32 b, u32 *attr);		/* tisp_get_frame_drop */
	void (*tisp_set)(u32 a, u32 b, u32 *attr);		/* tisp_set_frame_drop */
};

/* core: +116 channel, +268 enable (1), +272 pattern position, +280 drop mask */
static inline void t33_ispcore_single_cam_check_frame_drop(const struct t33_fd_ops *ops, u32 core)
{
	u32 attr[4] = { 0 };
	int chn;

	if (!t32_ic_r32(core, 272))
		return;
	ops->get_drop(core);
	if (t32_ic_r32(core, 268) != 1)
		return;
	if (t32_ic_r32(core, 280) != 0xffffffffU)
		return;
	chn = (int)t32_ic_r32(core, 116);
	ops->tisp_get((u32)(chn / 3), (u32)(chn % 3), attr);
	attr[0] = 0;
	ops->tisp_set((u32)(chn / 3), (u32)(chn % 3), attr);
}

/* ---- tx_isp_vic_index_cal ---- */
/* vic: +732 index copy, +780 cleared, +1092 mode (1, 2: index; 3, 4: index % 2; else error) */
static inline int t33_tx_isp_vic_index_cal(u32 vic, u32 idx, void (*warn)(u32 line))
{
	u32 mode;

	t32_ic_w32(vic, 732, idx);
	mode = t32_ic_r32(vic, 1092);
	t32_ic_w32(vic, 780, 0);
	if (mode && mode < 3)
		return (int)idx;
	if (mode >= 3 && mode < 5)
		return (int)idx % 2;
	warn(150);
	return 0;
}

#endif /* TX_ISP_T33_R12C_H */
