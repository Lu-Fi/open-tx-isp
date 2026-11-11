/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP library: IPC (inter-processor / AOV) memory size and placement.
 *
 *   t32_lib_get_isp_ipc_memory_space  lib_tisp_get_isp_ipc_memory_space 0x28034 (ch & 0xff, cfg): the LDC flag
 *       (cfg byte 2) is kept in the channel's 76-byte block (dc074 + 76 * ch, byte 10).  Only the last sensor
 *       channel (dc284 word 900 - 1) needs memory: tisp_top_ipc_get_mem_size(u16 dc074[0], u16 cfg[0]) plus,
 *       with the LDC flag, lib_tisp_ldc_get_mem_size(dc074 word 0, word 1); other channels 0
 *   t32_lib_set_isp_ipc_memory_space  lib_tisp_set_isp_ipc_memory_space 0x281b0 (ch & 0xff, base): marks the
 *       memory as configured (dd674 = 1); last channel: tisp_top_ipc_set_mem_cfg(base), the LDC area follows
 *       the top IPC area (block word 52); with the LDC flag (byte 10 == 1): block words 56 / 60 = LDC start /
 *       end (start + word 64), returns tisp_ldc_load_reg_cfg(ch, channel state)
 *     Without the LDC load the vendor returns a leftover address (void here, returns 0).
 *   The vendor's printk traces (source lines 1483 / 1510) are a trace hook (counted, line not compared).
 *
 * OS / block calls are hooks.  Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_libipc_oracle).
 */
#ifndef TX_ISP_T32_LIBIPC_H
#define TX_ISP_T32_LIBIPC_H

#ifndef __KERNEL__
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

#define T32_IPC_MDNS_BLK	76U	/* t32_data_dc074 block per channel */
#define T32_IPC_LDC_FLAG	10U	/* byte */
#define T32_IPC_TOP_SIZE	52U	/* u32 */
#define T32_IPC_LDC_START	56U	/* u32 */
#define T32_IPC_LDC_END		60U	/* u32 */
#define T32_IPC_LDC_SIZE	64U	/* u32 */
#define T32_IPC_STATE_BLK	904U	/* t32_data_dc284 block per channel */
#define T32_IPC_SENSOR_NUM	900U	/* u32 in channel 0's state */

struct t32_ipc_hooks {
	void *ctx;
	void (*trace)(void *ctx, u32 line);					/* printk */
	u32 (*top_get_mem_size)(void *ctx, u32 w, u32 cfg0);			/* tisp_top_ipc_get_mem_size */
	u32 (*ldc_get_mem_size)(void *ctx, u32 w0, u32 w1);			/* lib_tisp_ldc_get_mem_size */
	u32 (*top_set_mem_cfg)(void *ctx, u32 base);				/* tisp_top_ipc_set_mem_cfg */
	u32 (*ldc_load_reg_cfg)(void *ctx, u32 ch, u8 *state);			/* tisp_ldc_load_reg_cfg */
};

static inline u32 t32_ipc_w(const u8 *p, u32 off)
{
	return *(const u32 *)(p + off);
}

/* mdns = t32_data_dc074, state = t32_data_dc284 */
static inline u32 t32_lib_get_isp_ipc_memory_space(const struct t32_ipc_hooks *h, u8 *mdns, const u8 *state,
						   u32 ch, const u8 *cfg)
{
	u32 last, size = 0;

	ch &= 0xff;
	mdns[T32_IPC_MDNS_BLK * ch + T32_IPC_LDC_FLAG] = cfg[2];
	last = t32_ipc_w(state, T32_IPC_SENSOR_NUM) - 1;
	if (ch == last) {
		h->trace(h->ctx, 1483);
		size = h->top_get_mem_size(h->ctx, *(const u16 *)mdns, *(const u16 *)cfg);
	}
	if (cfg[2] == 1 && ch == last)
		size += h->ldc_get_mem_size(h->ctx, t32_ipc_w(mdns, 0), t32_ipc_w(mdns, 4));
	return size;
}

static inline u32 t32_lib_set_isp_ipc_memory_space(const struct t32_ipc_hooks *h, u8 *mdns, u8 *state,
						   u8 *configured, u32 ch, u32 base)
{
	u8 *blk;
	u32 last;

	*configured = 1;
	ch &= 0xff;
	last = t32_ipc_w(state, T32_IPC_SENSOR_NUM) - 1;
	blk = mdns + T32_IPC_MDNS_BLK * ch;
	if (ch == last) {
		h->trace(h->ctx, 1510);
		h->top_set_mem_cfg(h->ctx, base);
		base += t32_ipc_w(blk, T32_IPC_TOP_SIZE);
	}
	if (blk[T32_IPC_LDC_FLAG] != 1 || ch != last)
		return 0;
	*(u32 *)(blk + T32_IPC_LDC_START) = base;
	*(u32 *)(blk + T32_IPC_LDC_END) = t32_ipc_w(blk, T32_IPC_LDC_SIZE) + base;
	return h->ldc_load_reg_cfg(h->ctx, ch, state + T32_IPC_STATE_BLK * ch);
}

#endif
