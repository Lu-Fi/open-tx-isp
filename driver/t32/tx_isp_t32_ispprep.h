/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP core glue, part 10: ispcore_prepare_module (0xb6948) and isp_vic_set_dma_enable (0x1b40).
 *
 * prepare (pm prepare of the core): only when all 4 channels stream (core private +300 words
 * >= 2): wait for every channel's firmware queue (lib_tisp_fw_process_empty(ch), channels below
 * the private word +1196; a busy one is logged and gets 10 ms) and for the core event queue of
 * channel 0 (busy: counter t32_data_f336c + 1, log, 10 ms).
 * set_dma_enable(vic, on): maps on to the DMA mode of the VIC input mode (+1096: 3 -> on ? on == 1
 * : 2, 4 -> on ^ 2, else on); when VIC DMA is armed (+2412), the mode matches +2416, the frame
 * counter +2420 is below the limit +2428 and less than 4 ahead of +2424, the next buffer
 * (counter * stride +2440 + base +2444) is programmed with lib_tisp_vic_set_par and the DMA
 * started; the counter counts every attempt that reaches set_par.
 *
 * Checked against the vendor functions (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_ispprep_oracle).
 */
#ifndef TX_ISP_T32_ISPPREP_H
#define TX_ISP_T32_ISPPREP_H

#include "tx_isp_t32_isppm.h"

#define T32_PRIV_NCH		1196
#define T32_VIC_MODE		1096
#define T32_VIC_DMA_ARMED	2412
#define T32_VIC_DMA_MODE	2416
#define T32_VIC_DMA_CNT		2420
#define T32_VIC_DMA_DONE	2424
#define T32_VIC_DMA_LIMIT	2428
#define T32_VIC_DMA_B0		2432	/* u8 */
#define T32_VIC_DMA_B1		2436	/* u8 */
#define T32_VIC_DMA_STRIDE	2440
#define T32_VIC_DMA_BASE	2444

struct t32_prep_ops {
	void (*log)(u32 level, u32 line, u32 arg);	/* isp_printf */
	int (*fw_empty)(u32 ch);			/* lib_tisp_fw_process_empty */
	int (*event_empty)(u32 ch);			/* isp_core_event_work_empty */
	void (*msleep)(u32 ms);
	int (*set_par)(u32 mode, u32 a1, u32 b0, u32 a3, u32 b1, u32 addr);	/* lib_tisp_vic_set_par */
	void (*dma_start)(void);			/* lib_tisp_vic_dma_start */
};

/* ispcore_prepare_module(sd); busy_cnt = t32_data_f336c */
static inline int t32_ispcore_prepare_module(const struct t32_prep_ops *ops, u32 sd, u32 *busy_cnt)
{
	u32 priv = t32_pm_priv(sd), ch;

	if (!priv) {
		ops->log(2, 4192, 0);
		return T32_EINVAL;
	}
	if (!t32_pm_all_streaming(priv))
		return 0;
	for (ch = 0; ch < t32_ic_r32(priv, T32_PRIV_NCH); ch++) {
		if (ops->fw_empty(ch & 0xff))
			continue;
		ops->log(1, 0, ch);
		ops->msleep(10);
	}
	if (!ops->event_empty(0)) {
		++*busy_cnt;
		ops->log(1, 1, 0);
		ops->msleep(10);
	}
	ops->log(0, 4245, 0);
	return 0;
}

/* isp_vic_set_dma_enable(vic, on) */
static inline void t32_isp_vic_set_dma_enable(const struct t32_prep_ops *ops, u32 vic, u32 on)
{
	u32 mode = t32_ic_r32(vic, T32_VIC_MODE), m = on, cnt;

	if (mode == 3)
		m = on ? on == 1 : 2;
	else if (mode == 4)
		m = on ^ 2;
	if (!t32_ic_r32(vic, T32_VIC_DMA_ARMED) || m != t32_ic_r32(vic, T32_VIC_DMA_MODE))
		return;
	cnt = t32_ic_r32(vic, T32_VIC_DMA_CNT);
	if ((int)cnt >= (int)t32_ic_r32(vic, T32_VIC_DMA_LIMIT) ||
	    (int)(cnt - t32_ic_r32(vic, T32_VIC_DMA_DONE)) >= 4)
		return;
	if (!ops->set_par(m & 0xff, 3, *(volatile u8 *)T32_IC_P(vic + T32_VIC_DMA_B0), 1,
			  *(volatile u8 *)T32_IC_P(vic + T32_VIC_DMA_B1),
			  cnt * t32_ic_r32(vic, T32_VIC_DMA_STRIDE) + t32_ic_r32(vic, T32_VIC_DMA_BASE)))
		ops->dma_start();
	else
		ops->log(1, 362, 0);
	t32_ic_w32(vic, T32_VIC_DMA_CNT, t32_ic_r32(vic, T32_VIC_DMA_CNT) + 1);
}

#endif /* TX_ISP_T32_ISPPREP_H */
