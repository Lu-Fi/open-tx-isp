/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP library: full ISP reset.
 *
 *   t32_lib_all_reset   lib_tisp_all_reset 0x24e9c: interrupt words 0x22028 / 0x2202c = 0xffffffff, reset
 *                       request 0x200 = 1, poll 0x204 for 1 (10000 reads; timeout: error log, end); then core
 *                       control 0x24 = 0x40000001 and poll its low nibble for 0 (10000 reads; timeout: error
 *                       log); info log "reset done".  Returns the result of the last log (tail call).
 *
 * OS / register calls are hooks.  Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15)
 * by tests/t32_oracle (t32_libreset_oracle).
 */
#ifndef TX_ISP_T32_LIBRESET_H
#define TX_ISP_T32_LIBRESET_H

#ifndef __KERNEL__
#include <stdint.h>
#ifndef T32_HOST_U32
#define T32_HOST_U32
typedef uint32_t u32;
#endif
#endif

#define T32_RST_INTP0		0x22028U
#define T32_RST_INTP1		0x2202cU
#define T32_RST_REQ		0x200U
#define T32_RST_ACK		0x204U
#define T32_RST_CORE		0x24U
#define T32_RST_CORE_VAL	0x40000001U
#define T32_RST_POLLS		10000U

struct t32_reset_hooks {
	void *ctx;
	u32 (*sys_read)(void *ctx, u32 reg);			/* system_reg_read */
	u32 (*sys_write)(void *ctx, u32 reg, u32 val);		/* system_reg_write */
	u32 (*log)(void *ctx, u32 level, u32 line);		/* isp_printf (level, source line) */
};

static inline u32 t32_lib_all_reset(const struct t32_reset_hooks *h)
{
	unsigned int n;

	h->sys_write(h->ctx, T32_RST_INTP0, 0xffffffffU);
	h->sys_write(h->ctx, T32_RST_INTP1, 0xffffffffU);
	h->sys_write(h->ctx, T32_RST_REQ, 1);
	for (n = T32_RST_POLLS; h->sys_read(h->ctx, T32_RST_ACK) != 1;)
		if (!--n)
			return h->log(h->ctx, 2, 291);
	h->sys_write(h->ctx, T32_RST_CORE, T32_RST_CORE_VAL);
	for (n = T32_RST_POLLS; h->sys_read(h->ctx, T32_RST_CORE) & 0xf;)
		if (!--n) {
			h->log(h->ctx, 2, 303);
			break;
		}
	return h->log(h->ctx, 1, 306);
}

#endif
