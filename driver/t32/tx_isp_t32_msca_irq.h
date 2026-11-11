/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 MSCA frame-done interrupt: readable C of tisp_msca_irq_frame_done(ch), the handler
 * tisp_msca_init hooks on system irq 33 (ISP status 0x300a8 bit 1).  It commits whatever the
 * setters left pending for channel ch (OSD windows, masks, lines, curves) with
 * tisp_msca_cfg_shd(ch, 0) (tx_isp_t32_msca_cfg.h) and returns 1.
 *
 * Checked against the vendor function (Matt Davis' recovered C, PR #15) with its vendor callees by
 * tests/t32_oracle (t32_mscafd_oracle).
 */
#ifndef TX_ISP_T32_MSCA_IRQ_H
#define TX_ISP_T32_MSCA_IRQ_H

#include "tx_isp_t32_msca_cfg.h"

static inline int t32_msca_irq_frame_done(const struct t32_msca_cfg_ops *ops, struct t32_ms_io *io,
					  struct t32_msca_cfg_st *st, u32 ch)
{
	t32_msca_cfg_shd(ops, io, st, ch, 0);
	return 1;
}

#endif /* TX_ISP_T32_MSCA_IRQ_H */
