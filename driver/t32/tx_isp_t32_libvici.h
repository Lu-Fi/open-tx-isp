/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 VIC interrupt handler of the ISP library.
 *
 *   t32_lib_vic_intp_handle  lib_tisp_vic_intp_handle 0xadf88: per VIC 0 / 1 (base 0x80000 / 0x81000):
 *     status = reg 0x1e0, DMA status = reg 0x1e4 (both kept in the 20-byte result, words 1 + vic / 3 + vic).
 *     Status bits 0..5 count into config words 2..7; then, while a raw re-arm is pending (e3f40) and the
 *     frame counter (config word 2) + 1 == par byte 10: tisp_vic_mdma_enable(par, stride, 1, raw DMA), pending = 0.
 *     Status bit 4: result byte vic = 2 * vic + (reg 0x20c & 7 ? 1 : 0)  [vic 1: 2 / 3].
 *     Status bits 9..23 count into config words 14..28; bits 9..27 also log and count the VIC error counter.
 *     Acknowledge: reg 0x1f0 = status, reg 0x1f4 = DMA status.  DMA status bits 0..3 count into config words
 *     10..13.  DMA bit 0 (raw frame done): frames left (e3f48) - 1, frames captured (e3f49) + 1; at 0 frames left
 *     complete the raw completion (e3f4c) and frames captured = 0.  DMA bit 1: complete it when the DMA flag (par
 *     byte 4) is 1.  The 20-byte result (status bytes 0 / 1, bytes 2 / 3 zero) is copied to out.
 *
 * Vendor behaviour kept: no NULL check of the VIC configs; result bytes 0 / 1 of a VIC without status bit 4 are
 * the stack's (zero here).  Register and OS calls are hooks; printk traces carry the vendor source line.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15) by tests/t32_oracle (t32_libvici_oracle).
 */
#ifndef TX_ISP_T32_LIBVICI_H
#define TX_ISP_T32_LIBVICI_H

#include "tx_isp_t32_libvicp.h"

#define T32_VICI_STATUS		0x1e0U
#define T32_VICI_DMA_STATUS	0x1e4U
#define T32_VICI_STATUS_ACK	0x1f0U
#define T32_VICI_DMA_ACK	0x1f4U
#define T32_VICI_FRAME_STAT	0x20cU
#define T32_VICI_ERR_MASK	0x0ffffe00U
#define T32_VICI_BIT_FRAME	0x10U
#define T32_VICI_CNT_STATUS	2U	/* config word index of status bit 0 */
#define T32_VICI_CNT_DMA	10U	/* ... of DMA status bit 0 */
#define T32_VICI_CNT_ERR	14U	/* ... of status bit 9 */
#define T32_VIC_PAR_REARM	12U	/* e3f40: raw re-arm pending */
#define T32_VIC_PAR_REARM_AT	10U	/* frame count of the re-arm */
#define T32_VIC_ERRCNT_OFF	8U	/* e3f58: per-VIC error counters, after the completion's wait queue */

struct t32_vici_hooks {
	struct t32_vicp_hooks p;
	void (*complete)(void *ctx, void *completion);	/* private_complete */
};

struct t32_vici_result {
	u8 frame[4];		/* status byte per VIC */
	u32 status[2];
	u32 dma_status[2];
};

static inline void t32_vici_count(u32 *cnt, u32 bits, u32 first, u32 n)
{
	u32 b;

	for (b = 0; b < n; b++)
		if (bits & (1U << (first + b)))
			cnt[b]++;
}

static inline void t32_lib_vic_intp_handle(const struct t32_vici_hooks *h, struct t32_vic_glob *g,
					   struct t32_vici_result *out)
{
	const struct t32_vic_hooks *v = &h->p.vic;
	struct t32_vici_result res;
	u32 vic, base, st, dma;
	u32 *cnt;

	memset(&res, 0, sizeof(res));
	for (vic = 0; vic < 2; vic++) {
		base = T32_VICG_BASE(vic);
		st = v->sys_read(v->ctx, base + T32_VICI_STATUS);
		res.status[vic] = st;
		dma = v->sys_read(v->ctx, base + T32_VICI_DMA_STATUS);
		res.dma_status[vic] = dma;
		cnt = (u32 *)g->cfg[vic];
		if (st & 0x1ff) {
			t32_vici_count(cnt + T32_VICI_CNT_STATUS, st, 0, 6);
			if (g->par[T32_VIC_PAR_REARM] &&
			    g->par[T32_VIC_PAR_REARM_AT] == cnt[T32_VICI_CNT_STATUS] + 1) {
				v->mdma_enable(v->ctx, g->par, g->mdma_a1, 1, g->raw_dma);
				g->par[T32_VIC_PAR_REARM] = 0;
			}
			if (st & T32_VICI_BIT_FRAME)
				res.frame[vic] = (u8)(2 * vic +
					((v->sys_read(v->ctx, base + T32_VICI_FRAME_STAT) & 7) ? 1 : 0));
		}
		if (st & ~0x1ffU)
			t32_vici_count(cnt + T32_VICI_CNT_ERR, st, 9, 15);
		if (st & T32_VICI_ERR_MASK) {
			v->log(v->ctx, 1, 610);
			((u32 *)(g->wq + T32_VIC_ERRCNT_OFF))[vic]++;
		}
		v->sys_write(v->ctx, base + T32_VICI_STATUS_ACK, st);
		v->sys_write(v->ctx, base + T32_VICI_DMA_ACK, dma);
		t32_vici_count(cnt + T32_VICI_CNT_DMA, dma, 0, 4);
		if (dma & 1) {
			h->p.trace(v->ctx, 625, 0, 0, 0);
			g->b48--;
			g->b49++;
			if (g->b48 == 0) {
				h->complete(v->ctx, &g->frame_cnt);
				g->b49 = 0;
			}
		}
		if (dma & 2) {
			if (g->par[T32_VIC_PAR_DMA] == 1)
				h->complete(v->ctx, &g->frame_cnt);
			h->p.trace(v->ctx, 641, 0, 0, 0);
		}
	}
	memcpy(out, &res, sizeof(res));
}

#endif
