/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP library parameter refresh (new IQ bin / day-night mode).
 *
 *   t32_lib_refresh_para  lib_tisp_refresh_para 0xb0714: request (u32 bin address, u32 bin size, u32 mode,
 *                         u8 channel at byte 17): tisp_bin_data_parse(ch, size, address + 0x80000000).
 *                         Mode byte 1: byte 22 of the channel's tuning block (dd0a4[ch]) = 0.  When the channel
 *                         is running (dc060[ch] == 1): every block's day-night refresh in vendor order (top, dpc,
 *                         gib, ae, awb, lsc, tmo, dmsc, gamma, ccm, clm, bcsh, ysp, mdns, ydns, sdns, cdns, defog
 *                         with the two words at the start of the channel state dc284[ch], lce, rdns, decmp,
 *                         csccr), then the refresh state (dad90 byte 0 = 1, byte 2 = mode byte) and
 *                         __wake_up(dad94, 1, 1, 0).  Returns 0.
 *
 * Vendor behaviour kept: the channel byte is not range-checked.  Block functions and OS calls are hooks.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15) by tests/t32_oracle (t32_librefresh_oracle).
 */
#ifndef TX_ISP_T32_LIBREFRESH_H
#define TX_ISP_T32_LIBREFRESH_H

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

#define T32_REFRESH_TUNING_B22	22U
#define T32_REFRESH_RUNNING	1U
#define T32_REFRESH_BIN_KSEG0	0x80000000U

struct t32_refresh_req {
	u32 bin;		/* +0: bin physical address */
	u32 size;		/* +4 */
	u32 pad8;
	u32 mode;		/* +12: day-night mode (low byte used) */
	u8 b16;
	u8 ch;			/* +17 */
};

/* block day-night refresh hooks, vendor call order */
enum t32_refresh_blk {
	T32_RF_TOP, T32_RF_DPC, T32_RF_GIB, T32_RF_AE, T32_RF_AWB, T32_RF_LSC, T32_RF_TMO, T32_RF_DMSC,
	T32_RF_GAMMA, T32_RF_CCM, T32_RF_CLM, T32_RF_BCSH, T32_RF_YSP, T32_RF_MDNS, T32_RF_YDNS, T32_RF_SDNS,
	T32_RF_CDNS, T32_RF_DEFOG, T32_RF_LCE, T32_RF_RDNS, T32_RF_DECMP, T32_RF_CSCCR, T32_RF_NUM
};

struct t32_refresh_hooks {
	void *ctx;
	u32 (*bin_parse)(void *ctx, u32 ch, u32 size, u32 addr);		/* tisp_bin_data_parse */
	/* <blk>_dn_params_refresh(ch, mode); top / ae / sdns take only ch; defog gets the two state words */
	void (*dn_refresh)(void *ctx, enum t32_refresh_blk blk, u32 ch, u32 mode, u32 w0, u32 w1);
	void (*wake_up)(void *ctx, void *wq, u32 mode, u32 nr, void *key);	/* __wake_up */
};

struct t32_refresh_env {
	u8 **tuning;		/* dd0a4[] */
	const u8 *running;	/* dc060[] */
	const u32 *const *chn_state;	/* dc284[] (channel state, first two words) */
	u8 *state;		/* dad90 */
	void *wq;		/* dad94 */
};

static inline u32 t32_lib_refresh_para(const struct t32_refresh_hooks *h, const struct t32_refresh_env *e,
				       const struct t32_refresh_req *req)
{
	u32 ch = req->ch, mode = req->mode & 0xff, blk;

	h->bin_parse(h->ctx, ch, req->size, req->bin + T32_REFRESH_BIN_KSEG0);
	if (mode == 1)
		e->tuning[ch][T32_REFRESH_TUNING_B22] = 0;
	if (e->running[ch] != T32_REFRESH_RUNNING)
		return 0;
	for (blk = 0; blk < T32_RF_NUM; blk++) {
		if (blk == T32_RF_DEFOG)
			h->dn_refresh(h->ctx, T32_RF_DEFOG, ch, mode, e->chn_state[ch][0], e->chn_state[ch][1]);
		else
			h->dn_refresh(h->ctx, (enum t32_refresh_blk)blk, ch, mode, 0, 0);
	}
	e->state[0] = T32_REFRESH_RUNNING;
	e->state[2] = (u8)mode;
	h->wake_up(h->ctx, e->wq, 1, 1, NULL);
	return 0;
}

#endif
