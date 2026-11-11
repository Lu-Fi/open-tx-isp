/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP library channel deinit.
 *
 *   t32_lib_deinit  lib_tisp_deinit 0x25d1c: channel (a0 & 0xff): every block's deinit in vendor order (top, ae,
 *                   awb, gib, lsc, tmo, defog, dmsc, gamma, mdns, af, ydns, cdns, bcsh, ysp, sdns, tstp, clm, ccm,
 *                   lce, csc, csccr, dpc, msca, rdns, decmp, ldc, wdr), rcfg word (f0918[ch]) = 0,
 *                   tisp_rcfg_deinit(ch), e4018 bytes 0..3 = 0, vfree (and clear) the 6 parameter buffers
 *                   (dd0b4 + 24 * ch), the tuning block (dd0a4[ch]), the LUT info buffers (words 4, 5, 6, 8, 7,
 *                   10, 9 of dc274[ch]) and the LUT info itself, clear the 28 payload size bytes (dc1a4 + 28 * ch),
 *                   channel 0: tisp_core_tuning_deinit, running flag (dc060[ch]) = 0.
 *     The vendor return value is an address left in v0: void here.
 *
 * Vendor behaviour kept: the channel is not range-checked.  Block functions and OS calls are hooks.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15) by tests/t32_oracle (t32_libdeinit_oracle).
 */
#ifndef TX_ISP_T32_LIBDEINIT_H
#define TX_ISP_T32_LIBDEINIT_H

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

#define T32_DEINIT_PAR_BUFS	6U
#define T32_DEINIT_SIZES	28U

enum t32_deinit_blk {
	T32_DI_TOP, T32_DI_AE, T32_DI_AWB, T32_DI_GIB, T32_DI_LSC, T32_DI_TMO, T32_DI_DEFOG, T32_DI_DMSC,
	T32_DI_GAMMA, T32_DI_MDNS, T32_DI_AF, T32_DI_YDNS, T32_DI_CDNS, T32_DI_BCSH, T32_DI_YSP, T32_DI_SDNS,
	T32_DI_TSTP, T32_DI_CLM, T32_DI_CCM, T32_DI_LCE, T32_DI_CSC, T32_DI_CSCCR, T32_DI_DPC, T32_DI_MSCA,
	T32_DI_RDNS, T32_DI_DECMP, T32_DI_LDC, T32_DI_WDR, T32_DI_NUM
};

/* LUT info (dc274[ch]): buffer pointers from word 4 */
struct t32_lut_info {
	u32 head[4];
	void *buf[7];		/* words 4 .. 10 */
};

struct t32_deinit_hooks {
	void *ctx;
	void (*blk_deinit)(void *ctx, enum t32_deinit_blk blk, u32 ch);	/* tisp_<blk>_deinit */
	void (*rcfg_deinit)(void *ctx, u32 ch);				/* tisp_rcfg_deinit */
	void (*core_tuning_deinit)(void *ctx);				/* tisp_core_tuning_deinit */
	void (*vfree)(void *ctx, void *p);				/* private_vfree */
};

struct t32_deinit_env {
	u32 *rcfg;			/* f0918[] */
	u8 *e4018;			/* 4 bytes */
	void **par_bufs;		/* dd0b4: 6 per channel */
	void **tuning;			/* dd0a4[] */
	struct t32_lut_info **lut;	/* dc274[] */
	u8 *sizes;			/* dc1a4: 28 per channel */
	u8 *running;			/* dc060[] */
};

static inline void t32_deinit_free(const struct t32_deinit_hooks *h, void **p)
{
	if (*p) {
		h->vfree(h->ctx, *p);
		*p = NULL;
	}
}

static inline void t32_lib_deinit(const struct t32_deinit_hooks *h, const struct t32_deinit_env *e, u32 ch)
{
	static const u8 lut_order[7] = { 0, 1, 2, 4, 3, 6, 5 };	/* words 4, 5, 6, 8, 7, 10, 9 */
	struct t32_lut_info *lut;
	unsigned int i;

	ch &= 0xff;
	for (i = 0; i < T32_DI_NUM; i++)
		h->blk_deinit(h->ctx, (enum t32_deinit_blk)i, ch);
	e->rcfg[ch] = 0;
	h->rcfg_deinit(h->ctx, ch);
	memset(e->e4018, 0, 4);
	for (i = 0; i < T32_DEINIT_PAR_BUFS; i++)
		t32_deinit_free(h, &e->par_bufs[ch * T32_DEINIT_PAR_BUFS + i]);
	t32_deinit_free(h, &e->tuning[ch]);
	lut = e->lut[ch];
	if (lut) {
		for (i = 0; i < 7; i++)
			t32_deinit_free(h, &lut->buf[lut_order[i]]);
		t32_deinit_free(h, (void **)&e->lut[ch]);
	}
	memset(e->sizes + ch * T32_DEINIT_SIZES, 0, T32_DEINIT_SIZES);
	if (ch == 0)
		h->core_tuning_deinit(h->ctx);
	e->running[ch] = 0;
}

#endif
