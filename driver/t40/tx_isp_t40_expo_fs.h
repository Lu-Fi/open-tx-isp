/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T40 frame-start sensor apply (openimp #505): plain-data helpers, checked against the vendor module under Unicorn
 * (tests/t40_expo_fs_host.sh, tools/ko_emu/diff_expo_fs.py).
 *
 *  - t40_vic_irq_account: the VIC part of the vendor isp_vic_interrupt_service_routine that this driver's VIC ack filter
 *    runs: per-device counters vic+0x2c0+idx*0x10 (bits 0..3 of the pending status, /proc/jz/isp/isp-w02 row 2) and the
 *    frame-start decision (bit 4, VIC0/VIC1 only): vic+0x24c+idx*4 == 0 -> main_fs_work, == 1 -> sec_fs_work, else none
 *    (the vendor prints "no work!!!").
 *  - t40_efs_set_it / t40_efs_set_again: vendor sensor_set_integration_time / sensor_set_analog_gain (the sensor_ctrl
 *    0x164 / 0x174 callbacks of the AE): keep the packed EXPO word attr+0x120 (again << 16 | it), and on a change of the
 *    value (attr+0xb0 IT, attr+0xa0 again) arm the pending slot of the value (INT_TIME slot 3 / AGAIN slot 0) and the EXPO
 *    slot 6 with the packed word.  core = the ISP core (ispcore_sd + 0x10c), attr = the sensor attribute of channel ch
 *    (core + 0x170 + ch * 0x60); slots: core + 0x300 + ch * 0x48 + slot * 8 = {pending, value}.
 *  - t40_efs_slots_take: one slot walk of the vendor frame-start (fs = 1, ispcore_irq_main_fs_work) / frame-done (fs = 0,
 *    ispcore_irq_main_fd_work) worker: slot 5 (0x2000010) is taken by both, every other armed slot only when
 *    attr+0x124 (expo_fs) == fs.  The sensor commands come back in out[] (cmd, value; at most 9), the caller sends them.
 *    The vendor clears a pending word after its ioctl; here it is cleared before the command is returned, so a value
 *    armed while the command is on the I2C bus stays pending for the next frame (same commands, same final state).
 */
#ifndef TX_ISP_T40_EXPO_FS_H
#define TX_ISP_T40_EXPO_FS_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <asm/barrier.h>
#define T40_EFS_WMB() smp_wmb()
#else
#include <stdint.h>
#define T40_EFS_WMB() do { } while (0)
#endif

#define T40_EFS_U32(p, off) (*(volatile uint32_t *)((uint8_t *)(p) + (off)))
#define T40_EFS_SLOT(core, ch, i) (0x300U + (ch) * 0x48U + (i) * 8U)
#define T40_EFS_ATTR_AGAIN 0xa0U
#define T40_EFS_ATTR_IT 0xb0U
#define T40_EFS_ATTR_EXPO 0x120U
#define T40_EFS_ATTR_EXPO_FS 0x124U

enum { T40_EFS_WORK_NONE = -1, T40_EFS_WORK_MAIN = 0, T40_EFS_WORK_SEC = 1, T40_EFS_WORK_NOWORK = -2 };

static inline int t40_vic_irq_account(uint8_t *vic, uint32_t idx, uint32_t pending0)
{
	uint32_t b, sel;

	if (idx > 2U)
		return T40_EFS_WORK_NONE;
	for (b = 0; b < 4U; b++)
		if (pending0 & (1U << b))
			T40_EFS_U32(vic, 0x2c0U + idx * 0x10U + b * 4U) += 1U;
	if (idx == 2U || !(pending0 & 0x10U))
		return T40_EFS_WORK_NONE;
	sel = T40_EFS_U32(vic, 0x24cU + idx * 4U);
	if (sel == 0U)
		return T40_EFS_WORK_MAIN;
	if (sel == 1U)
		return T40_EFS_WORK_SEC;
	return T40_EFS_WORK_NOWORK;
}

static inline void t40_efs_arm(uint8_t *core, uint32_t ch, uint32_t slot, uint32_t value)
{
	T40_EFS_U32(core, T40_EFS_SLOT(core, ch, slot) + 4U) = value;
	T40_EFS_WMB();
	T40_EFS_U32(core, T40_EFS_SLOT(core, ch, slot)) = 1U;
}

/* returns 1 when the slots were armed (value changed), 0 otherwise */
static inline int t40_efs_set_it(uint8_t *core, uint8_t *attr, uint32_t ch, uint32_t it)
{
	T40_EFS_U32(attr, T40_EFS_ATTR_EXPO) = (T40_EFS_U32(attr, T40_EFS_ATTR_EXPO) & 0xffff0000U) + it;
	if (it == T40_EFS_U32(attr, T40_EFS_ATTR_IT))
		return 0;
	T40_EFS_U32(attr, T40_EFS_ATTR_IT) = it;
	t40_efs_arm(core, ch, 3U, it);
	t40_efs_arm(core, ch, 6U, T40_EFS_U32(attr, T40_EFS_ATTR_EXPO));
	return 1;
}

static inline int t40_efs_set_again(uint8_t *core, uint8_t *attr, uint32_t ch, uint32_t again)
{
	T40_EFS_U32(attr, T40_EFS_ATTR_EXPO) = (T40_EFS_U32(attr, T40_EFS_ATTR_EXPO) & 0xffffU) | (again << 16);
	if (T40_EFS_U32(attr, T40_EFS_ATTR_AGAIN) == again)
		return 0;
	T40_EFS_U32(attr, T40_EFS_ATTR_AGAIN) = again;
	t40_efs_arm(core, ch, 0U, again);
	t40_efs_arm(core, ch, 6U, T40_EFS_U32(attr, T40_EFS_ATTR_EXPO));
	return 1;
}

struct t40_efs_cmd {
	uint32_t cmd;
	uint32_t value;
};

/* attr may be NULL (no sensor attribute yet): then only slot 5 is taken, as the vendor would fault instead */
static inline unsigned int t40_efs_slots_take(uint8_t *core, uint8_t *attr, uint32_t ch, uint32_t fs,
					      struct t40_efs_cmd *out)
{
	static const int32_t cmd[9] = {
		0x2000007, 0x2000008, 0x2000009, 0x2000005, 0x2000006, 0x2000010, 0x2000016, -1, 0x200000a
	};
	unsigned int n = 0;
	uint32_t i, s;

	for (i = 0; i < 9U; i++) {
		s = T40_EFS_SLOT(core, ch, i);
		if (!T40_EFS_U32(core, s))
			continue;
		if (i != 5U && (cmd[i] == -1 || !attr || T40_EFS_U32(attr, T40_EFS_ATTR_EXPO_FS) != fs))
			continue;
		T40_EFS_U32(core, s) = 0U;
		out[n].cmd = (uint32_t)cmd[i];
		out[n].value = T40_EFS_U32(core, s + 4U);
		n++;
	}
	return n;
}

#endif
