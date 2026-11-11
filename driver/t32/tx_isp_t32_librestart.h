/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP library restart (single pipeline).
 *
 *   t32_lib_restart  lib_tisp_restart 0x289bc: with 2 or more pipelines (channel state dc284 word 900) only a
 *                    printk (line 1664, its result returned).  Otherwise, channel 0: block sleep (ae, awb, defog,
 *                    mdns, msca, ipc, top, wdr), lib_tisp_all_reset, interrupt clear / enable,
 *                    tisp_top_init(0, state), tisp_top_ipc_init(state), block wake-up in vendor order (ipc .. mdns),
 *                    tisp_mdns_start(0), the rest of the wake-ups (msca .. ydns); returns tisp_ysp_wake_up(0).
 *
 * The block functions use the dispatcher of tx_isp_t32_libaov.h (same functions as AOV sleep / wake-up).
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15) by tests/t32_oracle (t32_librestart_oracle).
 */
#ifndef TX_ISP_T32_LIBRESTART_H
#define TX_ISP_T32_LIBRESTART_H

#include "tx_isp_t32_libaov.h"

#define T32_RESTART_PIPES_OFF	900U	/* u32 in the channel state: pipeline count */
#define T32_RESTART_MAX_PIPES	2U

struct t32_restart_hooks {
	struct t32_aov_hooks aov;
	u32 (*printk)(void *ctx, u32 line);		/* printk (result returned by the vendor) */
	u32 (*mdns_start)(void *ctx, u32 ch);		/* tisp_mdns_start */
};

static inline u32 t32_lib_restart(const struct t32_restart_hooks *h, u8 *state)
{
	static const enum t32_aov_fn sleep[] = { T32_AOV_AE_SLEEP, T32_AOV_AWB_SLEEP, T32_AOV_DEFOG_SLEEP,
		T32_AOV_MDNS_SLEEP, T32_AOV_MSCA_SLEEP, T32_AOV_IPC_SLEEP, T32_AOV_TOP_SLEEP, T32_AOV_WDR_SLEEP,
		T32_AOV_ALL_RESET, T32_AOV_INTP_CLEAR, T32_AOV_INTP_ENABLE };
	const struct t32_aov_hooks *a = &h->aov;
	unsigned int i, f;

	if (*(const u32 *)(state + T32_RESTART_PIPES_OFF) >= T32_RESTART_MAX_PIPES)
		return h->printk(a->ctx, 1664);
	for (i = 0; i < sizeof(sleep) / sizeof(sleep[0]); i++)
		a->fn(a->ctx, sleep[i], 0, NULL);
	a->fn(a->ctx, T32_AOV_TOP_INIT, 0, state);
	a->fn(a->ctx, T32_AOV_TOP_IPC_INIT, 0, state);
	for (f = T32_AOV_IPC_WAKE; f <= T32_AOV_MDNS_WAKE; f++)
		a->fn(a->ctx, (enum t32_aov_fn)f, 0, NULL);
	h->mdns_start(a->ctx, 0);
	for (f = T32_AOV_MSCA_WAKE; f < T32_AOV_YSP_WAKE; f++)
		a->fn(a->ctx, (enum t32_aov_fn)f, 0, NULL);
	return a->fn(a->ctx, T32_AOV_YSP_WAKE, 0, NULL);
}

#endif
