/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP core glue, part 6: the frame-done and frame-start sensor work items.
 * Readable C of the vendor functions ispcore_irq_fd_work0..3 (0xb4fd4, 0xb5188, 0xb532c,
 * 0xb54d0) and ispcore_irq_fs_work0..3 (0xbb49c, 0xbb634, 0xbb774, 0xbb8b8).
 *
 * Per channel ch the core private block (core subdev +280) keeps 12 pending sensor updates of
 * { pending, value } at +1304 + 96 * ch (set by ispcore_update_fs_work) and a pointer at
 * +376 + 104 * ch whose word +256 is the channel's sensor mode (fd: 0 = apply, fs: 1 = apply).
 * The fs works first pass the private block to the fs debug trigger.  Both works tell the sensor (sensor ops ioctl of the core subdev, cmd 0x2000016 frame
 * done / 0x2000015 frame start, arg { ch, 0 }); fs_work0 then runs a pending core reset (byte
 * +4288: tisp_core_reset, core ops +32 with 0x1000008).  Then every pending update is sent with
 * ispcore_sensor_ops_ioctl(core, cmd, { ch, value }) and cleared.
 * fd: the command comes from a jump table (channel 0: 10 indexes, 1..3: 7), index 5 is a
 * horizontal/vertical flip (tx_isp_core_shvflip(ch), entry stays pending); an index without a
 * case reuses the previous command of the walk (0 at the start).  fs: command table
 * t32_data_c3308 (channel 0) / t32_data_c32d8 (1..3), -1 = keep pending; index 5 is skipped.
 *
 * Vendor behaviour kept: the private block is read before the core NULL check (crash without a
 * core); the reuse of the previous command; the flip entry never leaves the pending state.
 *
 * Checked against the vendor functions (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_ispwork_oracle).
 */
#ifndef TX_ISP_T32_ISPWORK_H
#define TX_ISP_T32_ISPWORK_H

#include "tx_isp_t32_ispcore.h"

#define T32_WORK_N		12
#define T32_PRIV_MODE		376	/* + 104 * ch: pointer, word +256 = sensor mode */
#define T32_MODE_WORD		256
#define T32_PRIV_RESET		4288	/* u8: core reset pending (frame start of channel 0) */
#define T32_SENSOR_CMD_FD	0x02000016U
#define T32_SENSOR_CMD_FS	0x02000015U
#define T32_CORE_OPS_EVENT	32	/* core ops: event(sd, cmd, arg) */
#define T32_CORE_EV_RESET	0x01000008U
#define T32_WK_SHVFLIP		1	/* fd table: flip, no sensor command */
#define T32_WK_REUSE		0	/* fd table: reuse the previous command */
#define T32_WK_NONE		0xffffffffU

struct t32_work_ops {
	int (*call)(u32 fn, u32 a0, u32 a1, u32 a2);	/* ops table entry */
	void (*sensor_ioctl)(u32 core, u32 cmd, u32 *arg);	/* ispcore_sensor_ops_ioctl */
	void (*shvflip)(u32 ch);			/* tx_isp_core_shvflip */
	void (*fs_trige)(u32 priv, u32 ch);		/* ispcoer_debug_info_fs_trige */
	void (*core_reset)(u32 priv, u32 arg);		/* tisp_core_reset */
};

static const u32 t32_fd_cmd[10] = {	/* jump tables t32_data_c30a0 (ch 0) / c30c8, c30e4, c3100 (first 7) */
	0x02000007, 0x02000008, 0x02000009, 0x02000005, 0x02000006, T32_WK_SHVFLIP,
	0x02000017, 0x02000018, T32_WK_REUSE, 0x0200000a,
};
static const u32 t32_fs_cmd[2][T32_WORK_N] = {
	{ 0x02000007, 0x02000008, 0x02000009, 0x02000005, 0x02000006, T32_WK_NONE,	/* c3308, ch 0 */
	  0x02000017, 0x02000018, T32_WK_NONE, 0x0200000a, T32_WK_NONE, 0x02000020 },
	{ 0x02000007, 0x02000008, 0x02000009, 0x02000005, 0x02000006, T32_WK_NONE,	/* c32d8, ch 1..3 */
	  0x02000017, T32_WK_NONE, T32_WK_NONE, T32_WK_NONE, T32_WK_NONE, 0x02000020 },
};

/* the sensor ops ioctl of the core subdev itself: tell the sensor about the frame event */
static inline void t32_work_notify(const struct t32_work_ops *ops, u32 core, u32 cmd, u32 *arg)
{
	u32 sops, fn;

	if (!core)
		return;
	sops = t32_ic_r32(t32_ic_r32(core, T32_SD_OPS), T32_OPS_SENSOR);
	fn = sops ? t32_ic_r32(sops, T32_SENSOR_IOCTL) : 0;
	if (fn)
		ops->call(fn, core, cmd, T32_IC_A(arg));
}

static inline u32 t32_work_mode(u32 priv, u32 ch)
{
	return t32_ic_r32(t32_ic_r32(priv, T32_PRIV_MODE + 104 * ch), T32_MODE_WORD);
}

/* ispcore_irq_fd_work<ch>(work); core = t32_data_f3310 */
static inline void t32_ispcore_irq_fd_work(const struct t32_work_ops *ops, u32 core, u32 ch)
{
	u32 priv = t32_ic_r32(core, T32_SD_PRIV), arg[2] = { ch, 0 }, cmd = 0, idx, e, k;
	u32 n = ch ? 7 : 10;

	t32_work_notify(ops, core, T32_SENSOR_CMD_FD, arg);
	if (!priv)
		return;
	for (idx = 0; idx < T32_WORK_N; idx++) {
		e = priv + T32_PRIV_FS_WORK + 96 * ch + 8 * idx;
		if (!t32_ic_r32(e, 0))
			continue;
		k = idx < n ? t32_fd_cmd[idx] : T32_WK_REUSE;
		if (k == T32_WK_SHVFLIP) {
			ops->shvflip(ch);
			continue;
		}
		if (k != T32_WK_REUSE)
			cmd = k;
		else if (idx == 5)
			continue;
		if (t32_work_mode(priv, ch))
			continue;
		arg[0] = ch;
		arg[1] = t32_ic_r32(e, 4);
		ops->sensor_ioctl(core, cmd, arg);
		t32_ic_w32(e, 0, 0);
	}
}

/* ispcore_irq_fs_work<ch>(work); core = t32_data_f3310 */
static inline void t32_ispcore_irq_fs_work(const struct t32_work_ops *ops, u32 core, u32 ch)
{
	u32 priv = t32_ic_r32(core, T32_SD_PRIV), arg[2] = { ch, 0 }, idx, e, cmd;

	ops->fs_trige(priv, ch);
	t32_work_notify(ops, core, T32_SENSOR_CMD_FS, arg);
	if (!ch && *(volatile u8 *)T32_IC_P(t32_ic_r32(core, T32_SD_PRIV) + T32_PRIV_RESET)) {
		u32 p = t32_ic_r32(core, T32_SD_PRIV), cops, fn;

		*(volatile u8 *)T32_IC_P(p + T32_PRIV_RESET) = 0;
		ops->core_reset(p, 0);
		if (core) {
			cops = t32_ic_r32(t32_ic_r32(core, T32_SD_OPS), T32_OPS_CORE);
			fn = cops ? t32_ic_r32(cops, T32_CORE_OPS_EVENT) : 0;
			if (fn)
				ops->call(fn, core, T32_CORE_EV_RESET, 0);
		}
	}
	if (!priv)
		return;
	for (idx = 0; idx < T32_WORK_N; idx++) {
		e = priv + T32_PRIV_FS_WORK + 96 * ch + 8 * idx;
		if (!t32_ic_r32(e, 0) || idx == 5 || t32_work_mode(priv, ch) != 1)
			continue;
		cmd = t32_fs_cmd[ch ? 1 : 0][idx];
		if (cmd == T32_WK_NONE)
			continue;
		arg[0] = ch;
		arg[1] = t32_ic_r32(e, 4);
		ops->sensor_ioctl(core, cmd, arg);
		t32_ic_w32(e, 0, 0);
	}
}

#endif /* TX_ISP_T32_ISPWORK_H */
