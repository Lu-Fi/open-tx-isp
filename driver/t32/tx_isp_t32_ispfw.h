/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP core glue, part 4: firmware threads and the AOV sleep event.
 * Readable C of the vendor functions isp_fw_process (0xb5be8), isp_fw_process1 (0xb5b94),
 * isp_fw_process2 (0xb5b40), isp_fw_process3 (0xb5aec) (one kthread per channel: run
 * lib_tisp_fw_process(ch) until kthread_should_stop) and isp_event_sleep_process (0xbba90):
 * clear the core private word +4012, hand the event on to sensor_event_streamoff_process with the
 * sensor attribute word (+1196) as fourth argument, then handle_aov_ispsleep(priv).
 *
 * Checked against the vendor functions (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_ispfw_oracle).
 */
#ifndef TX_ISP_T32_ISPFW_H
#define TX_ISP_T32_ISPFW_H

#include "tx_isp_t32_ispcore.h"

#define T32_PRIV_SLEEP_FLAG	4012

struct t32_fw_ops {
	int (*should_stop)(void);			/* private_kthread_should_stop */
	void (*fw_process)(u32 ch);			/* lib_tisp_fw_process */
	void (*streamoff)(u32 a0, u32 sd, u32 a2, u32 attr, u32 a4);	/* sensor_event_streamoff_process */
	void (*aov_sleep)(u32 priv);			/* handle_aov_ispsleep */
};

/* isp_fw_process<ch>(data) */
static inline int t32_isp_fw_process(const struct t32_fw_ops *ops, u32 ch)
{
	while (!ops->should_stop())
		ops->fw_process(ch);
	return 0;
}

/* isp_event_sleep_process(a0, sd, a2, a3, a4) */
static inline int t32_isp_event_sleep_process(const struct t32_fw_ops *ops, u32 a0, u32 sd, u32 a2, u32 a4)
{
	u32 priv = t32_ic_r32(sd, T32_SD_PRIV);

	t32_ic_w32(priv, T32_PRIV_SLEEP_FLAG, 0);
	ops->streamoff(a0, sd, a2, t32_ic_r32(priv, T32_PRIV_SENSOR_ATTR), a4);
	ops->aov_sleep(priv);
	return 0;
}

#endif /* TX_ISP_T32_ISPFW_H */
