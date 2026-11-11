/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP function reset: readable C of the vendor's tisp_func_stop_reset
 * and tisp_func_reset.
 *
 * Both mask all interrupts (0x22028 / 0x2202c = ~0), stop_reset first
 * requests the core stop (0x200 = 1) and polls 0x204 for the value 1, then
 * both request the function reset (0x24 = 0x40000004) and poll 0x24 until
 * the low nibble is 0.  Each poll gives up after 10000 reads with an error
 * print; the function then ends with the "isp function reset" print
 * (level 1), whose result is the return value.  A stop timeout returns the
 * error print's result at once (no function reset).
 *
 * Register access and printf are hooks (struct t32_freset_ops); the vendor
 * calls system_reg_write directly, so t32_data_d03a0 does not matter.
 * Checked against Matt Davis' recovered C (PR #15 @ 2e2e06bc) by
 * tests/t32_oracle (t32_freset_oracle).
 */
#ifndef TX_ISP_T32_FRESET_H
#define TX_ISP_T32_FRESET_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#ifndef T32_HOST_U32
#define T32_HOST_U32
typedef uint32_t u32;
#endif
#endif

#define T32_FRESET_IRQ_MASK0	0x22028
#define T32_FRESET_IRQ_MASK1	0x2202c
#define T32_FRESET_STOP		0x200
#define T32_FRESET_STOP_DONE	0x204
#define T32_FRESET_CTRL		0x24
#define T32_FRESET_REQ		0x40000004u
#define T32_FRESET_BUSY		0xf
#define T32_FRESET_POLLS	10000

#define T32_FRESET_MSG_STOP_TO	"[ISP][%s, %d]: isp stop timeout !!!\n"
#define T32_FRESET_MSG_RST_TO	"[ISP][%s, %d]: isp function reset timeout !!!\n"
#define T32_FRESET_MSG_RST	"[ISP][%s, %d]: isp function reset !!!\n"

struct t32_freset_ops {
	u32 (*reg_write)(u32 reg, u32 val);
	u32 (*reg_read)(u32 reg);
	u32 (*printf)(u32 level, const char *fmt, const char *fn, u32 line);
};

/* the common tail: function reset request, poll, report */
static inline u32 t32_freset_func(const struct t32_freset_ops *o, const char *fn,
				  u32 line_to, u32 line_done)
{
	u32 n = T32_FRESET_POLLS;

	o->reg_write(T32_FRESET_CTRL, T32_FRESET_REQ);
	while (o->reg_read(T32_FRESET_CTRL) & T32_FRESET_BUSY) {
		if (--n == 0) {
			o->printf(2, T32_FRESET_MSG_RST_TO, fn, line_to);
			break;
		}
	}
	return o->printf(1, T32_FRESET_MSG_RST, fn, line_done);
}

/* tisp_func_stop_reset */
static inline u32 t32_func_stop_reset(const struct t32_freset_ops *o)
{
	u32 n = T32_FRESET_POLLS;

	o->reg_write(T32_FRESET_IRQ_MASK0, 0xffffffffu);
	o->reg_write(T32_FRESET_IRQ_MASK1, 0xffffffffu);
	o->reg_write(T32_FRESET_STOP, 1);
	while (o->reg_read(T32_FRESET_STOP_DONE) != 1)
		if (--n == 0)
			return o->printf(2, T32_FRESET_MSG_STOP_TO, "tisp_func_stop_reset", 323);
	return t32_freset_func(o, "tisp_func_stop_reset", 336, 339);
}

/* tisp_func_reset */
static inline u32 t32_func_reset(const struct t32_freset_ops *o)
{
	o->reg_write(T32_FRESET_IRQ_MASK0, 0xffffffffu);
	o->reg_write(T32_FRESET_IRQ_MASK1, 0xffffffffu);
	return t32_freset_func(o, "tisp_func_reset", 358, 361);
}

#endif
