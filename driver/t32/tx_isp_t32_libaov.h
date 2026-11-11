/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP library: AOV (always-on video) sleep and wake-up of a channel.
 *
 *   t32_lib_aov_sleep    lib_tisp_aov_sleep 0x28304 (ch & 0xff): trace; single-sensor channel (state word 900
 *                        of the channel == 1): AE, AWB, defog, MDNS, MSCA, IPC, top, WDR sleep (each with ch);
 *                        returns the WDR sleep result.  Otherwise channel 0: IPC sleep(0), returns
 *                        tisp_aov_rcfg_sleep(0); other channels: nothing (the vendor returns a leftover address;
 *                        0 here).
 *   t32_lib_aov_wake_up  lib_tisp_aov_wake_up 0x28418 (ch & 0xff): trace; single-sensor channel: function reset,
 *                        interrupt clear + enable, tisp_top_init(ch, state), tisp_top_ipc_init(state), IPC and top
 *                        wake-up, then the 30 block wake-ups (AE .. YSP, vendor order), returns
 *                        system_reg_write(0x1040, 1) (stream start).  Otherwise channel 0: full reset, rcfg wake-up,
 *                        tisp_top_ipc_init(channel 0 state), IPC wake-up(0), interrupt enable; all such channels end
 *                        with (and return) tisp_top_init(ch, state).
 *   The vendor passes a leftover register as argument to tisp_func_reset (no argument here).
 *
 * The block calls go through one dispatcher hook (enum t32_aov_fn), the trace (printk, lines 1543 / 1565) is a
 * hook.  Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15) by tests/t32_oracle
 * (t32_libaov_oracle).
 */
#ifndef TX_ISP_T32_LIBAOV_H
#define TX_ISP_T32_LIBAOV_H

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

#define T32_AOV_STATE_BLK	904U	/* t32_data_dc284 block per channel */
#define T32_AOV_SENSOR_NUM	900U	/* u32 */
#define T32_AOV_STREAM_START	0x1040U

enum t32_aov_fn {
	/* sleep, vendor order */
	T32_AOV_AE_SLEEP = 1, T32_AOV_AWB_SLEEP, T32_AOV_DEFOG_SLEEP, T32_AOV_MDNS_SLEEP, T32_AOV_MSCA_SLEEP,
	T32_AOV_IPC_SLEEP, T32_AOV_TOP_SLEEP, T32_AOV_WDR_SLEEP, T32_AOV_RCFG_SLEEP,
	/* wake-up helpers */
	T32_AOV_FUNC_RESET, T32_AOV_INTP_CLEAR, T32_AOV_INTP_ENABLE, T32_AOV_TOP_INIT, T32_AOV_TOP_IPC_INIT,
	T32_AOV_ALL_RESET, T32_AOV_RCFG_WAKE,
	/* wake-up, vendor order */
	T32_AOV_IPC_WAKE, T32_AOV_TOP_WAKE, T32_AOV_AE_WAKE, T32_AOV_AF_WAKE, T32_AOV_AWB_WAKE, T32_AOV_BCSH_WAKE,
	T32_AOV_CCM_WAKE, T32_AOV_CDNS_WAKE, T32_AOV_CLM_WAKE, T32_AOV_CSC_WAKE, T32_AOV_CSCCR_WAKE,
	T32_AOV_DECMP_WAKE, T32_AOV_DEFOG_WAKE, T32_AOV_DMSC_WAKE, T32_AOV_DPC_WAKE, T32_AOV_GAMMA_WAKE,
	T32_AOV_GIB_WAKE, T32_AOV_GSM_WAKE, T32_AOV_LCE_WAKE, T32_AOV_LDC_WAKE, T32_AOV_LSC_WAKE,
	T32_AOV_MDNS_WAKE, T32_AOV_MSCA_WAKE, T32_AOV_RDNS_WAKE, T32_AOV_SDNS_WAKE, T32_AOV_TMO_WAKE,
	T32_AOV_TSTP_WAKE, T32_AOV_WDR_WAKE, T32_AOV_YDNS_WAKE, T32_AOV_YSP_WAKE,
};

struct t32_aov_hooks {
	void *ctx;
	u32 (*fn)(void *ctx, enum t32_aov_fn fn, u32 ch, u8 *state);	/* the block function (state: init calls) */
	void (*trace)(void *ctx, u32 line);				/* printk */
	u32 (*sys_write)(void *ctx, u32 reg, u32 val);			/* system_reg_write */
};

static inline int t32_aov_single(const u8 *state, u32 ch)
{
	return *(const u32 *)(state + T32_AOV_STATE_BLK * ch + T32_AOV_SENSOR_NUM) == 1;
}

/* state = t32_data_dc284 */
static inline u32 t32_lib_aov_sleep(const struct t32_aov_hooks *h, u8 *state, u32 ch)
{
	static const enum t32_aov_fn seq[] = { T32_AOV_AE_SLEEP, T32_AOV_AWB_SLEEP, T32_AOV_DEFOG_SLEEP,
		T32_AOV_MDNS_SLEEP, T32_AOV_MSCA_SLEEP, T32_AOV_IPC_SLEEP, T32_AOV_TOP_SLEEP };
	unsigned int i;

	ch &= 0xff;
	h->trace(h->ctx, 1543);
	if (t32_aov_single(state, ch)) {
		for (i = 0; i < sizeof(seq) / sizeof(seq[0]); i++)
			h->fn(h->ctx, seq[i], ch, NULL);
		return h->fn(h->ctx, T32_AOV_WDR_SLEEP, ch, NULL);
	}
	if (ch != 0)
		return 0;
	h->fn(h->ctx, T32_AOV_IPC_SLEEP, 0, NULL);
	return h->fn(h->ctx, T32_AOV_RCFG_SLEEP, 0, NULL);
}

static inline u32 t32_lib_aov_wake_up(const struct t32_aov_hooks *h, u8 *state, u32 ch)
{
	u8 *st;
	unsigned int f;

	ch &= 0xff;
	h->trace(h->ctx, 1565);
	st = state + T32_AOV_STATE_BLK * ch;
	if (t32_aov_single(state, ch)) {
		h->fn(h->ctx, T32_AOV_FUNC_RESET, 0, NULL);
		h->fn(h->ctx, T32_AOV_INTP_CLEAR, 0, NULL);
		h->fn(h->ctx, T32_AOV_INTP_ENABLE, 0, NULL);
		h->fn(h->ctx, T32_AOV_TOP_INIT, ch, st);
		h->fn(h->ctx, T32_AOV_TOP_IPC_INIT, 0, st);
		for (f = T32_AOV_IPC_WAKE; f <= T32_AOV_YSP_WAKE; f++)
			h->fn(h->ctx, (enum t32_aov_fn)f, ch, NULL);
		return h->sys_write(h->ctx, T32_AOV_STREAM_START, 1);
	}
	if (ch == 0) {
		h->fn(h->ctx, T32_AOV_ALL_RESET, 0, NULL);
		h->fn(h->ctx, T32_AOV_RCFG_WAKE, 0, NULL);
		h->fn(h->ctx, T32_AOV_TOP_IPC_INIT, 0, state);
		h->fn(h->ctx, T32_AOV_IPC_WAKE, 0, NULL);
		h->fn(h->ctx, T32_AOV_INTP_ENABLE, 0, NULL);
	}
	return h->fn(h->ctx, T32_AOV_TOP_INIT, ch, st);
}

#endif
