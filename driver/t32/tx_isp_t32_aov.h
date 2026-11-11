/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 always-on-video (AOV) sleep / wake register save and restore of the
 * vendor tisp core, the IPC block save / restore, the defog sleep snapshot,
 * the "pipe null done" interrupt and handle_aov_ispsleep.
 *
 * Flow of the vendor functions tisp_aov_rcfg_sleep, tisp_aov_rcfg_wake_up,
 * tisp_ipc_sleep, tisp_ipc_wake_up, tsip_defog_sleep,
 * tisp_isp_pipe_null_down_interrupt, handle_aov_ispsleep.  Register access
 * goes through struct t32_aov_ops: read / write are system_reg_read /
 * system_reg_write, tisp_write is tisp_reg_write(channel, register, value)
 * with the direct register address (in the vendor's d03a0 = 1 shadow mode the
 * vendor hands tisp_reg_write the shadow index instead; T32_AOV_IDX_* lists
 * them for the reads below).
 *
 * Vendor behaviour kept (not fixed, a change needs the user's OK): in shadow
 * mode (shadow != 0) tsip_defog_sleep reads the shadow index as a register
 * address (system_reg_read(2555) instead of 0xa210 ...).
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15)
 * by tests/t32_oracle (t32_aov_oracle).
 */
#ifndef TX_ISP_T32_AOV_H
#define TX_ISP_T32_AOV_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#ifndef T32_HOST_U32
#define T32_HOST_U32
typedef uint32_t u32;
#endif
#ifndef T32_HOST_U8
#define T32_HOST_U8
typedef uint8_t u8;
typedef uint16_t u16;
#endif
#endif

struct t32_aov_ops {
	u32 (*read)(u32 reg);
	void (*write)(u32 reg, u32 val);
	void (*tisp_write)(u32 ch, u32 reg, u32 val);
	/* handle_aov_ispsleep */
	void (*aov_sleep)(int step);		/* lib_tisp_aov_sleep */
	void (*up)(void *sem);
	void (*atomic_inc)(u32 *v);
	void (*wake_up)(void *q, u32 mode, int nr, void *key);
};

#define T32_AOV_IPC_SAVE_REG	0x2f0	/* saved at sleep, restored at wake up */

/* tisp_aov_rcfg_sleep: ch = idx - 1, or for idx 0 the last of `nchan` channels */
static inline u32 t32_aov_rcfg_sleep(const struct t32_aov_ops *k, u32 *saved, u32 idx, u32 nchan)
{
	static const u32 regs[3] = { 0xd088, 0xd08c, 0xd090 };
	u32 ch, i;

	idx &= 0xff;
	ch = (idx ? idx - 1 : nchan - 1) & 0xff;
	k->tisp_write(ch, 0xd018, 0);
	for (i = 0; i < 3; i++)
		k->tisp_write(ch, regs[i], k->read(regs[i]));
	*saved = k->read(T32_AOV_IPC_SAVE_REG);
	return *saved;
}

/* tisp_aov_rcfg_wake_up */
static inline void t32_aov_rcfg_wake_up(const struct t32_aov_ops *k, const u32 *saved)
{
	k->write(T32_AOV_IPC_SAVE_REG, *saved);
}

/* the six IPC registers of tisp_ipc_sleep / tisp_ipc_wake_up */
static const u32 t32_ipc_regs[6] = { 0x1094, 0x1098, 0x10bc, 0x10c0, 0x1100, 0x1104 };

static inline u32 t32_ipc_sleep(const struct t32_aov_ops *k, u32 *save)
{
	int i;

	for (i = 0; i < 6; i++)
		save[i] = k->read(t32_ipc_regs[i]);
	return save[5];
}

static inline void t32_ipc_wake_up(const struct t32_aov_ops *k, const u32 *save)
{
	int i;

	for (i = 0; i < 6; i++)
		k->write(t32_ipc_regs[i], save[i]);
}

/* tsip_defog_sleep: 11 defog registers into save[] ({ address, shadow index }) */
static const u16 t32_defog_sleep_regs[11][2] = {
	{ 0xa210, 2555 }, { 0xa214, 2556 }, { 0xa218, 2557 }, { 0xa21c, 2558 }, { 0xa20c, 2554 },
	{ 0xa040, 2439 }, { 0xa044, 2440 }, { 0xa04c, 2442 }, { 0xa204, 2552 }, { 0xa208, 2553 },
	{ 0xa20c, 2554 }
};

static inline u32 t32_defog_sleep(const struct t32_aov_ops *k, u32 *save, int shadow)
{
	int i;

	for (i = 0; i < 11; i++)
		save[i] = k->read(t32_defog_sleep_regs[i][shadow ? 1 : 0]);
	return save[10];
}

/* tisp_isp_pipe_null_down_interrupt: a pending value goes to 0x2104c once */
static inline int t32_isp_pipe_null_done(const struct t32_aov_ops *k, u8 *pending, u32 val)
{
	if (*pending) {
		k->write(0x2104c, val);
		*pending = 0;
	}
	return 1;
}

/* handle_aov_ispsleep (dev: the ISP core device; +1196 AOV mode, +4024 event count,
 * +4028 wait queue, +4032 semaphore) */
#define T32_CORE_AOV_MODE	1196
#define T32_CORE_AOV_EVENTS	4024
#define T32_CORE_AOV_WQ		4028
#define T32_CORE_AOV_SEM	4032

static inline void t32_handle_aov_ispsleep(const struct t32_aov_ops *k, u8 *dev)
{
	u32 mode = *(u32 *)(dev + T32_CORE_AOV_MODE);

	k->aov_sleep(0);
	if (mode != 1)
		k->aov_sleep(1);
	k->up(dev + T32_CORE_AOV_SEM);
	k->atomic_inc((u32 *)(dev + T32_CORE_AOV_EVENTS));
	k->wake_up((void *)(uintptr_t)*(u32 *)(dev + T32_CORE_AOV_WQ), 3, 1, NULL);	/* 32-bit pointer */
}

#endif /* TX_ISP_T32_AOV_H */
