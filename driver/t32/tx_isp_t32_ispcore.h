/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP core glue, part 1: irq dispatch, the sensor ops walk and small state helpers.
 * Readable C of the vendor functions
 *   isp_irq_handle (0xd020), isp_irq_thread_handle (0xd080),
 *   ispcore_sensor_ops_release_all_sensor (0xb4d58), ispcore_sensor_ops_ioctl (0xb4e00),
 *   ispcore_link_setup (0xb4df8), ispcore_irq_thread_handle (0xb4d50), isp_info_lsc (0xb3490),
 *   isp_core_tuning_clear (0xc80c), isp_core_tuning_deinit (0xc7f0), ispcore_update_fs_work (0xbb468).
 *
 * The vendor structures are reached through 32-bit addresses (the module runs on MIPS32): a
 * subdev keeps its ops table at +264 (ops[0] = core ops, ops[3] = sensor ops), the core ops
 * hold interrupt_service_routine at +36 and interrupt_thread at +40, the sensor ops hold
 * release_all at +0 and ioctl at +8.  The irq cookie (dev_id) is the subdev + 156.  The core
 * subdev keeps 16 sensor subdev slots at +84 and its private block at +280.
 * Indirect calls through these tables go through ops->call (the kernel glue casts the address,
 * the host oracle records the call).
 *
 * Vendor behaviour kept: the sensor walk dereferences the slot's ops table without a NULL
 * check (crash on a sensor without ops), a NULL slot keeps the previous result, and the walk
 * always ends with 0 unless a callee returns an error other than -ENOIOCTLCMD (-515).
 *
 * Checked against the vendor functions (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_ispcore_oracle).
 */
#ifndef TX_ISP_T32_ISPCORE_H
#define TX_ISP_T32_ISPCORE_H

#include "tx_isp_t32_front.h"

#define T32_IC_P(a)		((u8 *)(uintptr_t)(a))
#define T32_IC_A(p)		((u32)(uintptr_t)(p))

#define T32_SD_IRQ_COOKIE	156	/* dev_id = subdev + 156 */
#define T32_SD_OPS		264	/* subdev -> ops table */
#define T32_SD_SENSORS		84	/* core subdev: 16 sensor subdev slots */
#define T32_SD_SENSORS_N	16
#define T32_SD_PRIV		280	/* core subdev -> private block */
#define T32_OPS_CORE		0
#define T32_OPS_SENSOR		12
#define T32_CORE_ISR		36	/* core ops: interrupt_service_routine(sd, irq, 0) */
#define T32_CORE_ITHREAD	40	/* core ops: interrupt_thread(sd, 0) */
#define T32_SENSOR_RELEASE	0	/* sensor ops: release_all(sd) */
#define T32_SENSOR_IOCTL	8	/* sensor ops: ioctl(sd, cmd, arg) */
#define T32_ENOIOCTLCMD		(-515)
#define T32_IRQ_HANDLED		1
#define T32_IRQ_WAKE_THREAD	2
#define T32_SENSOR_CMD_ATTR	0x0200001cU	/* sensor attribute update: priv + 1196 keeps a 108 B copy */
#define T32_PRIV_SENSOR_ATTR	1196
#define T32_PRIV_FS_WORK	1304	/* 4 channels x 12 entries of { pending, value } */

struct t32_ic_ops {
	int (*call)(u32 fn, u32 a0, u32 a1, u32 a2);	/* indirect call through an ops table */
	void (*kfree)(void *p);
};

static inline u32 t32_ic_r32(u32 a, u32 off)
{
	return *(volatile u32 *)T32_IC_P(a + off);
}

static inline void t32_ic_w32(u32 a, u32 off, u32 v)
{
	*(volatile u32 *)T32_IC_P(a + off) = v;
}

/* the core ops entry at off of subdev sd, 0 when the table or the entry is missing */
static inline u32 t32_ic_core_op(u32 sd, u32 off)
{
	u32 core = t32_ic_r32(t32_ic_r32(sd, T32_SD_OPS), T32_OPS_CORE);

	return core ? t32_ic_r32(core, off) : 0;
}

/* isp_irq_handle(irq, dev_id): the core's interrupt_service_routine decides HANDLED / WAKE_THREAD */
static inline int t32_isp_irq_handle(const struct t32_ic_ops *ops, u32 irq, u32 dev_id)
{
	u32 sd = dev_id - T32_SD_IRQ_COOKIE, isr;

	if (!sd)
		return T32_IRQ_HANDLED;
	isr = t32_ic_core_op(sd, T32_CORE_ISR);
	if (!isr)
		return T32_IRQ_HANDLED;
	return ops->call(isr, sd, irq, 0) == T32_IRQ_WAKE_THREAD ? T32_IRQ_WAKE_THREAD : T32_IRQ_HANDLED;
}

/* isp_irq_thread_handle(irq, dev_id): runs the core's interrupt_thread, always HANDLED */
static inline int t32_isp_irq_thread_handle(const struct t32_ic_ops *ops, u32 irq, u32 dev_id)
{
	u32 sd = dev_id - T32_SD_IRQ_COOKIE, fn;

	(void)irq;
	if (!sd)
		return T32_IRQ_HANDLED;
	fn = t32_ic_core_op(sd, T32_CORE_ITHREAD);
	if (fn)
		ops->call(fn, sd, 0, 0);
	return T32_IRQ_HANDLED;
}

/* the walk of ispcore_sensor_ops_release_all_sensor / ispcore_sensor_ops_ioctl over the 16 slots */
static inline int t32_ispcore_sensor_walk(const struct t32_ic_ops *ops, u32 sd, u32 entry, u32 cmd, u32 arg)
{
	int ret = 0;
	unsigned int i;

	for (i = 0; i < T32_SD_SENSORS_N; i++) {
		u32 s = t32_ic_r32(sd, T32_SD_SENSORS + 4 * i), sops, fn;

		if (!s)
			continue;
		sops = t32_ic_r32(t32_ic_r32(s, T32_SD_OPS), T32_OPS_SENSOR);
		fn = sops ? t32_ic_r32(sops, entry) : 0;
		if (!fn) {
			ret = T32_ENOIOCTLCMD;
			continue;
		}
		ret = ops->call(fn, s, cmd, arg);
		if (ret && ret != T32_ENOIOCTLCMD)
			return ret;
	}
	return ret == T32_ENOIOCTLCMD ? 0 : ret;
}

static inline int t32_ispcore_sensor_ops_release_all_sensor(const struct t32_ic_ops *ops, u32 sd)
{
	return t32_ispcore_sensor_walk(ops, sd, T32_SENSOR_RELEASE, 0, 0);
}

static inline int t32_ispcore_sensor_ops_ioctl(const struct t32_ic_ops *ops, u32 sd, u32 cmd, u32 arg)
{
	u32 priv = t32_ic_r32(sd, T32_SD_PRIV);

	if (cmd == T32_SENSOR_CMD_ATTR && priv)
		memcpy(T32_IC_P(priv + T32_PRIV_SENSOR_ATTR), T32_IC_P(arg), 108);
	return t32_ispcore_sensor_walk(ops, sd, T32_SENSOR_IOCTL, cmd, arg);
}

/* ispcore_link_setup, ispcore_irq_thread_handle and isp_info_lsc are empty in the vendor module */
static inline int t32_ispcore_link_setup(void)
{
	return 0;
}

static inline int t32_ispcore_irq_thread_handle(void)
{
	return 0;
}

static inline int t32_isp_info_lsc(void)
{
	return 0;
}

/* isp_core_tuning_clear(idx, tuning): the 32 B entries start at +16, field +16 of entry idx is cleared */
static inline void t32_isp_core_tuning_clear(u32 idx, u32 tuning)
{
	t32_ic_w32(tuning, 32 + (idx << 5), 0);
}

/* isp_core_tuning_deinit(tuning) */
static inline void t32_isp_core_tuning_deinit(const struct t32_ic_ops *ops, u32 tuning)
{
	if (tuning)
		ops->kfree(T32_IC_P(tuning));
}

/* ispcore_update_fs_work(ch, idx, val): mark frame-start work idx of channel ch pending with val */
static inline int t32_ispcore_update_fs_work(u32 core_sd, u32 ch, u32 idx, u32 val)
{
	u32 e = t32_ic_r32(core_sd, T32_SD_PRIV) + T32_PRIV_FS_WORK + ((ch * 12 + idx) << 3);

	t32_ic_w32(e, 0, 1);
	t32_ic_w32(e, 4, val);
	return 0;
}

#endif /* TX_ISP_T32_ISPCORE_H */
