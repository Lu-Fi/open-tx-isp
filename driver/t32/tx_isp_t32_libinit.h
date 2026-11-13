/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP library channel init.
 *
 *   t32_lib_init  lib_tisp_init 0x251cc (ch = a0 & 0xff, attr = 904 B sensor/ISP attributes):
 *                 copy attr into the channel state (dc284 + 904 * ch), raise the global maximum width / height
 *                 (dc074 words 0 / 1) to attr words 0 / 1, error print (line 390) when the tuning block
 *                 (dd0a4[ch]) is missing, channel 0: lib_tisp_all_reset; tisp_rcfg_init, sensor_init (dd2d4 +
 *                 232 * ch), the block inits in vendor order (csc .. ldc), rcfg word (f0918[ch]) = 0, channel 0
 *                 with WDR on (attr byte 36): tisp_wdr_init(0) and rcfg word = 1; tisp_msca_init,
 *                 tisp_event_init (error print, line 442, on failure; init goes on), channel 0:
 *                 tisp_top_ipc_init, the 7 AE/AWB event callbacks, then if the pending flag (f0900[ch]) is 1
 *                 the startup updates from the AE state (e4100 + 12800 * ch): long total gain
 *                 log2(mult2_32(10, w12528, w12532) << 6), long EV (w12552 << 10), with WDR (state byte 36 == 1)
 *                 the short EV max(EV >> 4, 1024), colour temperature (w188); pending flag = 0, channel 0:
 *                 tisp_core_tuning_init; tisp_code_tuning_poll_init, running flag (dc060[ch]) = 1,
 *                 lib_tisp_set_csccr(ch, e4018[ch]).  Returns 0.
 *     The vendor hands the update callbacks the constant u64 stack words 0, 1, 0, 1 after the value; the three
 *     level-1 banner prints are left out.  The memset before the memcpy of attr is dead (both 904 B) and left out.
 *
 * Vendor behaviour kept: the channel is not range-checked; an event_init failure only prints.  Block functions,
 * prints and OS calls are hooks; mult2_32 / log2 are the ported helpers.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15) by tests/t32_oracle (t32_libinit_oracle).
 */
#ifndef TX_ISP_T32_LIBINIT_H
#define TX_ISP_T32_LIBINIT_H

#ifndef __KERNEL__
#include <stdint.h>
#include <string.h>
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
#include "tx_isp_t32_af.h"
#include "tx_isp_t32_log2.h"

#define T32_INIT_PAR_SIZE	904U
#define T32_INIT_SENSOR_SIZE	232U
#define T32_INIT_AE_SIZE	12800U
#define T32_INIT_WDR_BYTE	36U
#define T32_INIT_ERR_TUNING	390U
#define T32_INIT_ERR_EVENT	442U
#define T32_INIT_AE_CT		188U
#define T32_INIT_AE_GAIN_A	12528U
#define T32_INIT_AE_GAIN_B	12532U
#define T32_INIT_AE_EV		12552U
#define T32_INIT_SHORT_EV_MIN	1024U

enum t32_init_blk {
	T32_IB_CSC, T32_IB_CSCCR, T32_IB_TOP, T32_IB_AE, T32_IB_AWB, T32_IB_AF, T32_IB_TMO, T32_IB_DMSC,
	T32_IB_GAMMA, T32_IB_CDNS, T32_IB_GIB, T32_IB_DPC, T32_IB_LSC, T32_IB_DEFOG, T32_IB_YDNS, T32_IB_CCM,
	T32_IB_BCSH, T32_IB_LCE, T32_IB_SDNS, T32_IB_YSP, T32_IB_MDNS, T32_IB_CLM, T32_IB_GSM, T32_IB_RDNS,
	T32_IB_DMA, T32_IB_DECMP, T32_IB_LDC,
	T32_IB_NUM,
	T32_IB_WDR = T32_IB_NUM, T32_IB_MSCA
};

/* event callbacks and the startup updates */
enum t32_init_cb {
	T32_ICB_LONG_TGAIN, T32_ICB_SHORT_TGAIN, T32_ICB_LONG_AGAIN, T32_ICB_LONG_EV, T32_ICB_SHORT_EV,
	T32_ICB_CT, T32_ICB_AE_IR, T32_ICB_NUM,
	T32_ICB_WDR_SHORT_EV = T32_ICB_NUM	/* tisp_wdr_short_ev_update: startup update only */
};

struct t32_init_hooks {
	void *ctx;
	void (*err)(void *ctx, u32 line);				/* isp_printf level 2 */
	void (*all_reset)(void *ctx);					/* lib_tisp_all_reset */
	void (*rcfg_init)(void *ctx, u32 ch, u8 *par);			/* tisp_rcfg_init */
	void (*sensor_init)(void *ctx, u8 *sensor, u32 ch);		/* sensor_init */
	void (*blk_init)(void *ctx, enum t32_init_blk blk, u32 ch, u8 *par);	/* tisp_<blk>_init */
	u32 (*event_init)(void *ctx, u32 ch);				/* tisp_event_init */
	void (*top_ipc_init)(void *ctx, u8 *par);			/* tisp_top_ipc_init */
	void (*set_cb)(void *ctx, u32 ch, u32 event, enum t32_init_cb cb);	/* tisp_event_set_cb */
	void (*update)(void *ctx, enum t32_init_cb cb, u32 ch, uint64_t v);	/* tisp_<cb>_update */
	void (*core_tuning_init)(void *ctx);				/* tisp_core_tuning_init */
	void (*tuning_poll_init)(void *ctx);				/* tisp_code_tuning_poll_init */
	void (*set_csccr)(void *ctx, u32 ch, u32 mode);			/* lib_tisp_set_csccr */
};

struct t32_init_env {
	u8 *par;		/* dc284: 904 B per channel */
	u32 *max_size;		/* dc074 words 0, 1 */
	void **tuning;		/* dd0a4[] */
	u8 *sensor;		/* dd2d4: 232 B per channel */
	u32 *rcfg;		/* f0918[] */
	u8 *pending;		/* f0900[] */
	const u8 *ae;		/* e4100: 12800 B per channel */
	u8 *running;		/* dc060[] */
	const u8 *csccr_mode;	/* e4018[] */
};

static inline u32 t32_init_rd32(const u8 *p, u32 o)
{
	u32 v;

	memcpy(&v, p + o, 4);
	return v;
}

static inline u32 t32_lib_init(const struct t32_init_hooks *h, const struct t32_init_env *e, u32 ch, const u8 *attr)
{
	static const u8 events[T32_ICB_NUM] = { 4, 8, 5, 6, 10, 11, 7 };
	const u8 *ae;
	u8 *par;
	u32 i, v, ev;

	ch &= 0xff;
	par = e->par + ch * T32_INIT_PAR_SIZE;
	memcpy(par, attr, T32_INIT_PAR_SIZE);
	for (i = 0; i < 2; i++) {
		v = t32_init_rd32(attr, 4 * i);
		if (e->max_size[i] < v)
			e->max_size[i] = v;
	}
	if (!e->tuning[ch])
		h->err(h->ctx, T32_INIT_ERR_TUNING);
	if (ch == 0)
		h->all_reset(h->ctx);
	h->rcfg_init(h->ctx, ch, par);
	h->sensor_init(h->ctx, e->sensor + ch * T32_INIT_SENSOR_SIZE, ch);
	for (i = 0; i < T32_IB_NUM; i++)
		h->blk_init(h->ctx, (enum t32_init_blk)i, ch, par);
	e->rcfg[ch] = 0;
	if (ch == 0 && attr[T32_INIT_WDR_BYTE]) {
		h->blk_init(h->ctx, T32_IB_WDR, 0, par);
		e->rcfg[ch] = 1;
	}
	h->blk_init(h->ctx, T32_IB_MSCA, ch, par);
	if (h->event_init(h->ctx, ch))
		h->err(h->ctx, T32_INIT_ERR_EVENT);
	if (ch == 0)
		h->top_ipc_init(h->ctx, par);
	for (i = 0; i < T32_ICB_NUM; i++)
		h->set_cb(h->ctx, ch, events[i], (enum t32_init_cb)i);
	if (e->pending[ch] == 1) {
		ae = e->ae + ch * T32_INIT_AE_SIZE;
		ev = t32_init_rd32(ae, T32_INIT_AE_EV) << 10;
		v = t32_af_mult2(10, t32_init_rd32(ae, T32_INIT_AE_GAIN_A), t32_init_rd32(ae, T32_INIT_AE_GAIN_B));
		h->update(h->ctx, T32_ICB_LONG_TGAIN, ch, t32_log2_fixed_to_fixed(v << 6, 16, 16));
		h->update(h->ctx, T32_ICB_LONG_EV, ch, ev);
		if (par[T32_INIT_WDR_BYTE] == 1)
			h->update(h->ctx, T32_ICB_WDR_SHORT_EV, ch,
				  ev >> 4 < T32_INIT_SHORT_EV_MIN ? T32_INIT_SHORT_EV_MIN : ev >> 4);
		h->update(h->ctx, T32_ICB_CT, ch, t32_init_rd32(ae, T32_INIT_AE_CT));
	}
	e->pending[ch] = 0;
	if (ch == 0)
		h->core_tuning_init(h->ctx);
	h->tuning_poll_init(h->ctx);
	e->running[ch] = 1;
	h->set_csccr(h->ctx, ch, e->csccr_mode[ch]);
	return 0;
}

#endif
