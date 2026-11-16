/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 VIC / stream-on helpers of the vendor module (block r12G): lib_tisp_stream_on, lib_tisp_vic_ctrl,
 * lib_tisp_vic_intp_enable / _disable, lib_tisp_vic_stop, lib_tisp_vic_stream_on, lib_tisp_vic_reset,
 * tx_isp_vic_start, tx_vic_enable_irq, tx_vic_disable_irq.
 *
 * VIC n registers are at (128 + n) << 12 of the system register space.  The callees (system_reg_read /
 * system_reg_write, printk, isp_printf, tisp_rcfg_t33_write_all_reg, the spin lock pair) are the driver's own.
 */
#ifndef TX_ISP_T33_R12G_H
#define TX_ISP_T33_R12G_H

#include "tx_isp_t33_blk.h"

#define T33_R12_VIC(ch)	((((u32)(ch) & 0xff) + 128) << 12)

/* lib_tisp_stream_on: init_static = byte of ispInitStatic */
static inline void t33_lib_stream_on(u8 init_static)
{
	u32 a, b;

	if (init_static != 1) {
		printk("lib_tisp_stream_on: isp not initialised\n");
		return;
	}
	tisp_rcfg_t33_write_all_reg(0);
	for (;;) {
		a = system_reg_read(0x22010);
		b = system_reg_read(0x22014);
		if (!(a | b))
			break;
		system_reg_write(0x22028, 0);
		system_reg_write(0x2202c, 0);
	}
	system_reg_write(0x1040, 1);
	isp_printf(0, "lib_tisp_stream_on: done\n", "lib_tisp_stream_on", 496);
}

/* lib_tisp_vic_ctrl */
static inline u32 t33_lib_vic_ctrl(u32 ch, u32 val)
{
	return system_reg_write(T33_R12_VIC(ch) + 424, val);
}

static inline u32 t33_vic_intp(u32 ch, u32 mask)
{
	system_reg_write(T33_R12_VIC(ch) + 488, mask);
	return system_reg_write(T33_R12_VIC(ch) + 492, mask);
}

/* lib_tisp_vic_intp_enable */
static inline u32 t33_lib_vic_intp_enable(u32 ch)
{
	return t33_vic_intp(ch, 0);
}

/* lib_tisp_vic_intp_disable */
static inline u32 t33_lib_vic_intp_disable(u32 ch)
{
	return t33_vic_intp(ch, 0xffffffffU);
}

static inline u32 t33_vic_run(u32 on)
{
	system_reg_write(T33_R12_VIC(0) + 0x10, on);
	return system_reg_write(T33_R12_VIC(1) + 0x10, on);
}

/* lib_tisp_vic_stop */
static inline u32 t33_lib_vic_stop(void)
{
	printk("lib_tisp_vic_stop\n");
	return t33_vic_run(0);
}

/* lib_tisp_vic_stream_on */
static inline u32 t33_lib_vic_stream_on(void)
{
	return t33_vic_run(1);
}

/* lib_tisp_vic_reset */
static inline u32 t33_lib_vic_reset(void)
{
	u32 a, b;

	isp_printf(0, "lib_tisp_vic_reset\n", "lib_tisp_vic_reset", 444);
	system_reg_read(T33_R12_VIC(0) + 0x10);
	system_reg_read(T33_R12_VIC(1) + 0x10);
	a = system_reg_read(T33_R12_VIC(0) + 0x200);
	system_reg_write(T33_R12_VIC(0) + 0x200, 0);
	system_reg_write(T33_R12_VIC(0) + 0x200, a);
	b = system_reg_read(T33_R12_VIC(1) + 0x200);
	system_reg_write(T33_R12_VIC(1) + 0x200, 0);
	system_reg_write(T33_R12_VIC(1) + 0x200, b);
	system_reg_write(T33_R12_VIC(0) + 0x18, 0);
	system_reg_write(T33_R12_VIC(1) + 0x18, 0);
	system_reg_write(T33_R12_VIC(0) + 0x10, 4);
	return system_reg_write(T33_R12_VIC(1) + 0x10, 4);
}

/* tx_isp_vic_start(vic, force): vic + 816 started, vic + 1092 sensor count */
static inline int t33_tx_vic_start(u8 *vic, u32 force)
{
	u32 *started = (u32 *)(vic + 816), *nsns = (u32 *)(vic + 1092);

	if (*started)
		return 0;
	if (*nsns == 1 || force == 1) {
		t33_lib_vic_reset();
		t33_lib_vic_stream_on();
		*started = 1;
	}
	return 0;
}

static inline int t33_vsd_ok(const u8 *vsd)
{
	return (unsigned long)vsd && (unsigned long)vsd < 0xfffff001UL;
}

/* tx_vic_enable_irq: vsd = dump_vsd (lock + 152, callbacks + 180 / + 184, state words + 164 + 4 n) */
static inline void t33_tx_vic_enable_irq(u8 *vsd, u32 n)
{
	u32 flags = 0, cb;
	u32 *st;

	if (!t33_vsd_ok(vsd))
		return;
	__private_spin_lock_irqsave(vsd + 152, &flags);
	cb = *(u32 *)(vsd + 180);
	if (cb) {
		st = (u32 *)(vsd + 164 + 4 * (int)n);
		if (!*st) {
			if ((int)n <= 0)
				((void (*)(void *, u32))(unsigned long)cb)(vsd + 152, n);
			t33_lib_vic_intp_enable(n);
			*st = 1;
		}
	}
	private_spin_unlock_irqrestore(vsd + 152, flags);
}

/* tx_vic_disable_irq */
static inline void t33_tx_vic_disable_irq(u8 *vsd, u32 n)
{
	u32 flags = 0, cb;
	u32 *st;

	if (!t33_vsd_ok(vsd))
		return;
	__private_spin_lock_irqsave(vsd + 152, &flags);
	cb = *(u32 *)(vsd + 184);
	if (cb) {
		st = (u32 *)(vsd + 164 + 4 * (int)n);
		if (*st) {
			if ((int)n <= 0)
				((void (*)(void *, u32))(unsigned long)cb)(vsd + 152, n);
			t33_lib_vic_intp_disable(n);
			*st = 0;
		}
	}
	private_spin_unlock_irqrestore(vsd + 152, flags);
}

#endif /* TX_ISP_T33_R12G_H */
