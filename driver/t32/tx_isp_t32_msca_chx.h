/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 MSCA channel enable / stream control: readable C of tisp_msca_chx_en,
 * lib_tisp_msca_chx_stream_on / _stream_off and tisp_msca_deinit.
 *
 * A stream index n (0..11) names channel n / 3 and its output set n % 3; the
 * per-set "streaming" byte is sb[set + 14] of the channel's parameter block
 * (sb = t32_data_e2768[channel], layout in tx_isp_t32_msca_api.h).
 * chx_en turns the set's bit in the enable word (0xf0040, word 17 in shadow
 * mode) on only if en == 1 and the streaming byte is 1, off otherwise, after
 * zeroing the set's two DMA address registers (0xf0144 / 0xf0164 + 0x100 * set;
 * shadow 57 / 65 + 32 * set), and then kicks 0xf0010 (shadow 13) = 1.  stream_on
 * sets the byte and enables, but only in direct mode (d03a0 == 0); stream_off
 * clears the byte and disables in both modes.  The vendor's tail-call return
 * values are the register write's result and not modelled.
 *
 * Checked against the vendor functions (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_mscaj_oracle).
 */
#ifndef TX_ISP_T32_MSCA_CHX_H
#define TX_ISP_T32_MSCA_CHX_H

#include "tx_isp_t32_msca_io.h"

/* tisp_msca_chx_en(ch, set, en) */
static inline void t32_msca_chx_en(struct t32_ms_io *io, const u8 *sb, u32 ch, u32 set, u32 en)
{
	u32 cur;

	ch &= 0xff;
	set &= 0xff;
	en &= 0xff;
	cur = t32_ms_tr(io, ch, 17, 0xf0040);
	t32_ms_tw(io, ch, set * 32 + 57, 0xf0144 + set * 0x100, 0);
	t32_ms_tw(io, ch, set * 32 + 65, 0xf0164 + set * 0x100, 0);
	if (en == 1 && sb[set + 14] == en)
		t32_ms_tw(io, ch, 17, 0xf0040, cur | 1u << (set & 31));
	else
		t32_ms_tw(io, ch, 17, 0xf0040, cur & ~(1u << (set & 31)));
	t32_ms_tw(io, ch, 13, 0xf0010, 1);
}

/* lib_tisp_msca_chx_stream_on(n): sb is the block of channel (n & 0xff) / 3 */
static inline void t32_msca_chx_stream_on(struct t32_ms_io *io, u8 *sb, u32 n)
{
	n &= 0xff;
	sb[n % 3 + 14] = 1;
	if (!io->shadow)
		t32_msca_chx_en(io, sb, n / 3, n % 3, 1);
}

/* lib_tisp_msca_chx_stream_off(n) */
static inline void t32_msca_chx_stream_off(struct t32_ms_io *io, u8 *sb, u32 n)
{
	n &= 0xff;
	sb[n % 3 + 14] = 0;
	t32_msca_chx_en(io, sb, n / 3, n % 3, 0);
}

struct t32_msca_free_ops {
	void (*irq_func_set)(u32 ch, u32 irq, u32 fn);
	void (*kfree)(u32 p);
};

/*
 * tisp_msca_deinit(ch): unhook irq 33, free the parameter block and the context.
 * sets / ctx are t32_data_e2768 / t32_data_e28ac.  The vendor indexes both arrays with
 * the unmasked argument (the port requires ch < 4, as every caller passes).
 */
static inline int t32_msca_deinit(const struct t32_msca_free_ops *ops, u32 *sets, u32 *ctx, u32 ch)
{
	ops->irq_func_set(ch & 0xff, 33, 0);
	if (sets[ch])
		ops->kfree(sets[ch]);
	sets[ch] = 0;
	if (ctx[ch])
		ops->kfree(ctx[ch]);
	ctx[ch] = 0;
	return 0;
}

#endif /* TX_ISP_T32_MSCA_CHX_H */
