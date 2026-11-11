/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP pipeline reset of the vendor tisp core: tisp_reset (only in the
 * d03a0 = 1 shadow-register mode).
 *
 * Masks the interrupts (0x22028 / 0x2202c = ~0, 0x25040 = 0), requests the
 * stop (0x200 = 1) and polls 0x204 for 1 (10000 reads); restarts the MDNS
 * buffers of every pipeline (dc284 +900); requests the reset (0x24 =
 * 0x40000004) and polls 0x24 bits 0..3 for 0, first 10000 reads, then 10000
 * reads 1 us apart.  Register and driver calls go through struct
 * t32_treset_ops.
 *
 * Vendor behaviour kept (not fixed, a change needs the user's OK): a stop
 * timeout only prints and returns (no reset); the first read of 0x204 is
 * discarded; the pipeline counter is 16 bits (65536 or more pipelines never
 * end).
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15)
 * by tests/t32_oracle (t32_treset_oracle).
 */
#ifndef TX_ISP_T32_TRESET_H
#define TX_ISP_T32_TRESET_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#ifndef T32_HOST_U32
#define T32_HOST_U32
typedef uint32_t u32;
#endif
#endif

struct t32_treset_ops {
	u32 (*read)(u32 reg);			/* system_reg_read */
	void (*write)(u32 reg, u32 val);	/* system_reg_write */
	void (*mdns_buf_clc)(u32 pipe);
	void (*mdns_start)(u32 pipe);
	void (*mdns_reg_trig)(u32 pipe);
	void (*udelay)(u32 us);
	int (*isp_printf)(int level, const char *fmt, ...);
};

#define T32_RESET_POLLS	10000

/* shadow: d03a0 == 1; pipes: dc284 +900 */
static inline void t32_tisp_reset(const struct t32_treset_ops *k, int shadow, u32 pipes)
{
	u32 n, p;

	if (!shadow)
		return;
	k->write(0x22028, 0xffffffffu);
	k->write(0x2202c, 0xffffffffu);
	k->write(0x25040, 0);
	k->write(0x200, 1);
	k->read(0x204);
	for (n = T32_RESET_POLLS; ; ) {
		if (k->read(0x204) == 1)
			break;
		n = (n - 1) & 0xffff;
		if (!n) {
			k->isp_printf(1, "[ISP][%s, %d]: isp stop timeout !!!\n", "tisp_reset", 532);
			return;
		}
	}
	for (p = 0; p < pipes; p = (p + 1) & 0xffff) {
		k->mdns_buf_clc(p);
		k->mdns_start(p);
		k->mdns_reg_trig(p);
	}
	k->write(0x24, 0x40000004);
	for (n = T32_RESET_POLLS; n; ) {
		if (!(k->read(0x24) & 0xf))
			return;
		n = (n - 1) & 0xffff;
	}
	k->isp_printf(1, "[ISP][%s, %d]: isp reset timeout !!!\n", "tisp_reset", 550);
	for (n = T32_RESET_POLLS; n; ) {
		k->udelay(1);
		if (!(k->read(0x24) & 0xf))
			return;
		n = (n - 1) & 0xffff;
	}
	k->isp_printf(2, "[ISP][%s, %d]: isp reset timeout after udelay !!!\n", "tisp_reset", 559);
}

#endif /* TX_ISP_T32_TRESET_H */
