/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 core one-shot tick (vendor tx_isp_core_oneshot_tick, .isra): under
 * the core spin lock (+288) mark the frame channels started (oneshot mode
 * +4284 == 1), stream on 3 MSCA channels per sensor (+1196 sensors), then
 * per channel 0..2 feed the MSCA address FIFO from the queue (+220) or move
 * the first ready buffer (+212 list) to the queue, and trigger LDC / rcfg
 * with more than one sensor.
 *
 * Core device: +1132 channel array (284 bytes each: +212 ready list,
 * +220 queue list (next, prev), +280 started), +1136 channel count.
 * Buffer node: +0 next, +4 prev, +8 / +12 FIFO address words.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C,
 * PR #15) by tests/t32_oracle (t32_txtick_oracle).
 */
#ifndef TX_ISP_T32_TXTICK_H
#define TX_ISP_T32_TXTICK_H

#include "tx_isp_t32_txos.h"
#include "tx_isp_t32_txirq.h"
#include "tx_isp_t32_txcore.h"

#define T32_SD_PRIV		280U
#define T32_CORE_SNS_NUM	1196U
#define T32_CORE_LOCK		288U
#define T32_CORE_ONESHOT	4284U
#define T32_CORE_CHANS		1132U
#define T32_CORE_NCHAN		1136U
#define T32_CHAN_BYTES		284U
#define T32_CHAN_READY		212U
#define T32_CHAN_QUEUE		220U
#define T32_CHAN_STARTED	280U
#define T32_LIST_POISON1	0x100U
#define T32_LIST_POISON2	0x200U

/* provided by the driver glue (kernel) or by the oracle (host) */
void t32_tx_os_msca_chx_stream_on(u32 ch);
void t32_tx_os_msca_addr_fifo_write(u32 a0, u32 ch, u32 y, u32 uv);
void t32_tx_os_msca_chx_en(u32 a0, u32 ch, u32 on);
void t32_tx_os_ldc_reg_reflash(u32 a0);
void t32_tx_os_rcfg_cfg_trig(u32 a0);

/* tx_isp_core_oneshot_tick(sd).  Vendor: more than 255 sensors never end the stream-on loop */
static inline int t32_tx_core_oneshot_tick(u32 sd)
{
	u32 core = t32_tx_sd_ok(sd) ? t32_tx_ld(sd, T32_SD_PRIV) : 0, flags, i, c, n;

	flags = t32_tx_os_spin_lock_irqsave(core + T32_CORE_LOCK);
	if (t32_tx_ld(core, T32_CORE_ONESHOT) == 1)
		for (i = 0; i < t32_tx_ld(core, T32_CORE_NCHAN); i++) {
			u32 ch = t32_tx_ld(core, T32_CORE_CHANS) + i * T32_CHAN_BYTES;

			if (!t32_tx_ld(ch, T32_CHAN_STARTED))
				t32_tx_st(ch, T32_CHAN_STARTED, 1);
		}
	for (i = 0, c = 0; i < t32_tx_ld(core, T32_CORE_SNS_NUM); i = (i + 1) & 0xff, c = (c + 3) & 0xff) {
		t32_tx_os_msca_chx_stream_on(c);
		t32_tx_os_msca_chx_stream_on((c + 1) & 0xff);
		t32_tx_os_msca_chx_stream_on((c + 2) & 0xff);
	}
	for (i = 0; i < 3; i++) {
		u32 ch = t32_tx_ld(core, T32_CORE_CHANS) + i * T32_CHAN_BYTES;
		u32 rd = t32_tx_ld(ch, T32_CHAN_READY), q = t32_tx_ld(ch, T32_CHAN_QUEUE), tail;

		if (rd == ch + T32_CHAN_READY)
			rd = 0;
		if (q != ch + T32_CHAN_QUEUE && q) {
			t32_tx_os_msca_addr_fifo_write(0, i, t32_tx_ld(q, 8), t32_tx_ld(q, 12));
			t32_tx_os_msca_chx_en(0, i, 1);
			continue;
		}
		if (!rd) {
			t32_tx_os_msca_chx_en(0, i, 0);
			continue;
		}
		t32_tx_os_msca_addr_fifo_write(0, i, t32_tx_ld(rd, 8), t32_tx_ld(rd, 12));
		t32_tx_os_msca_chx_en(0, i, 1);
		/* list_move_tail(rd, queue) */
		n = t32_tx_ld(rd, 0);
		t32_tx_st(n, 4, t32_tx_ld(rd, 4));
		t32_tx_st(t32_tx_ld(rd, 4), 0, n);
		t32_tx_st(rd, 0, T32_LIST_POISON1);
		t32_tx_st(rd, 4, T32_LIST_POISON2);
		tail = t32_tx_ld(ch, T32_CHAN_QUEUE + 4);
		t32_tx_st(ch, T32_CHAN_QUEUE + 4, rd);
		t32_tx_st(rd, 0, ch + T32_CHAN_QUEUE);
		t32_tx_st(rd, 4, tail);
		t32_tx_st(tail, 0, rd);
	}
	if (t32_tx_ld(core, T32_CORE_SNS_NUM) != 1) {
		t32_tx_os_ldc_reg_reflash(0);
		t32_tx_os_rcfg_cfg_trig(0);
	}
	t32_tx_os_spin_unlock_irqrestore(core + T32_CORE_LOCK, flags);
	return 0;
}

#endif
