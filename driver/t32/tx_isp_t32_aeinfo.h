/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 AE information helpers: readable C of the vendor's
 * tisp_ae_max_exp_calc_fps and tisp_ae_algo_getinfo.
 *
 * Blocks as in tx_isp_t32_aetrig.h: AE context t32_data_e15a8[ch] (word 0
 * state, word 1 tuning), parameter block t32_data_e1588[ch].
 * - max_exp_calc_fps(ch, ev): the maximum exposure for a frame rate,
 *   fix_point_div_32(s, fix_point_mult2_32(s, vts << s, par +1260),
 *   (vts - par +624 + ev) << s) with vts = tuning u16 +8554 and the
 *   fraction bits s = state u16 +1736.
 * - algo_getinfo(ch): under the AE lock copy the zone statistics
 *   (t32_data_d0950, 1052 B per channel: 1024 B zones, five u16 at +1024
 *   stored as bytes at context +12) to the context (+22) together with 900
 *   tuning bytes (+7648 -> context +1046); then the exposure info
 *   (tisp_g_ae_exprinfo_attr) words 6 .. 9 and 31 .. 34 go to context
 *   +1952 .. +1980.  The result is the info block (context + 12).
 *
 * Callees and the lock are hooks (struct t32_aeinfo_ops).  Checked against
 * Matt Davis' recovered C (PR #15 @ 2e2e06bc) by tests/t32_oracle
 * (t32_aeinfo_oracle).
 */
#ifndef TX_ISP_T32_AEINFO_H
#define TX_ISP_T32_AEINFO_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/string.h>
#else
#include <stdint.h>
#include <string.h>
#ifndef T32_HOST_U32
#define T32_HOST_U32
typedef uint32_t u32;
#endif
#ifndef T32_HOST_U8
#define T32_HOST_U8
typedef uint8_t u8;
#endif
#ifndef T32_HOST_U16
#define T32_HOST_U16
typedef uint16_t u16;
#endif
#endif

#define T32_AEI_PAR_VTS_OFF	624
#define T32_AEI_PAR_FPS		1260
#define T32_AEI_TUN_VTS		8554
#define T32_AEI_ST_FRAC		1736
#define T32_AEI_ZONE_STRIDE	1052
#define T32_AEI_ZONE_BYTES	1024
#define T32_AEI_ZONE_HIST	5
#define T32_AEI_CTX_INFO	12
#define T32_AEI_CTX_ZONES	22
#define T32_AEI_CTX_TUN		1046
#define T32_AEI_TUN_SRC		7648
#define T32_AEI_TUN_BYTES	900
#define T32_AEI_CTX_EXPR	1952
#define T32_AEI_EXPR_WORDS	40

struct t32_aeinfo_ops {
	u32 (*fix_mult2)(u32 frac, u32 a, u32 b);	/* fix_point_mult2_32 */
	u32 (*fix_div)(u32 frac, u32 a, u32 b);		/* fix_point_div_32 */
	u32 (*lock)(void);				/* AE spin lock (t32_small_bss), flags */
	void (*unlock)(u32 flags);
	void (*g_exprinfo)(u32 ch, u32 *info);		/* tisp_g_ae_exprinfo_attr */
};

struct t32_aeinfo_dev {
	const struct t32_aeinfo_ops *ops;
	const u32 *ctx;			/* t32_data_e15a8 */
	const u32 *par;			/* t32_data_e1588 */
	const u8 *zones;		/* t32_data_d0950 */
};

static inline u8 *t32_aei_ptr(u32 addr) { return (u8 *)(uintptr_t)addr; }
static inline u32 t32_aei_rd32(const u8 *p) { u32 v; memcpy(&v, p, 4); return v; }
static inline u16 t32_aei_rd16(const u8 *p) { u16 v; memcpy(&v, p, 2); return v; }

/* tisp_ae_max_exp_calc_fps */
static inline u32 t32_ae_max_exp_calc_fps(struct t32_aeinfo_dev *d, u32 ch, u32 ev)
{
	const u8 *ctx = t32_aei_ptr(d->ctx[ch]);
	const u8 *par = t32_aei_ptr(d->par[ch]);
	u32 vts = t32_aei_rd16(t32_aei_ptr(t32_aei_rd32(ctx + 4)) + T32_AEI_TUN_VTS);
	u32 frac = t32_aei_rd16(t32_aei_ptr(t32_aei_rd32(ctx)) + T32_AEI_ST_FRAC);
	u32 m = d->ops->fix_mult2(frac, vts << (frac & 31), t32_aei_rd32(par + T32_AEI_PAR_FPS));

	return d->ops->fix_div(frac, m, (vts - t32_aei_rd32(par + T32_AEI_PAR_VTS_OFF) + ev) << (frac & 31));
}

/* tisp_ae_algo_getinfo */
static inline u8 *t32_ae_algo_getinfo(struct t32_aeinfo_dev *d, u32 ch)
{
	u8 *ctx = t32_aei_ptr(d->ctx[ch]);
	const u8 *z = d->zones + T32_AEI_ZONE_STRIDE * ch;
	u32 info[T32_AEI_EXPR_WORDS] = { 0 };
	u32 flags, i;

	flags = d->ops->lock();
	memcpy(ctx + T32_AEI_CTX_ZONES, z, T32_AEI_ZONE_BYTES);
	for (i = 0; i < T32_AEI_ZONE_HIST; i++) {
		u16 h = t32_aei_rd16(z + T32_AEI_ZONE_BYTES + 4 * i);

		ctx[T32_AEI_CTX_INFO + 2 * i] = h & 0xff;
		ctx[T32_AEI_CTX_INFO + 2 * i + 1] = h >> 8;
	}
	memcpy(ctx + T32_AEI_CTX_TUN, t32_aei_ptr(t32_aei_rd32(ctx + 4)) + T32_AEI_TUN_SRC, T32_AEI_TUN_BYTES);
	d->ops->unlock(flags);
	d->ops->g_exprinfo(ch, info);
	memcpy(ctx + T32_AEI_CTX_EXPR, &info[6], 16);
	memcpy(ctx + T32_AEI_CTX_EXPR + 16, &info[31], 16);
	return ctx + T32_AEI_CTX_INFO;
}

#endif
