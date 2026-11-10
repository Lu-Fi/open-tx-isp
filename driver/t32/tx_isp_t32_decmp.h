/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 decompression (DECMP, block 0x27000) per-frame and parameter paths.
 * Vendor functions: tisp_decmp_ref_reg_cfg, tisp_decmp_all_reg_refresh,
 * tisp_decmp_init, tisp_decmp_deinit, tisp_decmp_dn_params_refresh,
 * tisp_decmp_param_array_get, tisp_decmp_param_array_set and
 * tisp_decmp_wake_up.
 *
 * IQ bin section: 100 bytes at tuning base + 0xf00c, read as 25 u32 words
 * p[0..24] (p[0] is never used).  p[1..12] are 12 knot positions (x), p[13..24]
 * the 12 values (y).  tisp_decmp_ref_reg_cfg writes 23 registers from 0x27010:
 * 6 x pairs (12 bit fields), 6 y pairs (16 bit halves), then the 11 segment
 * slopes ((dy << 7) / dx, 24 bit; dx == 0 gives all ones like the MIPS divu
 * sequence of the vendor code).
 *
 * The vendor context is 8 bytes per channel (t32_data_e3f60[ch]): the pointer
 * to the section and a word set to -1 that nothing reads.
 *
 * NOT ported: the kmalloc failure path (the vendor prints and then calls
 * memset on NULL, the port returns -1), the vendor channel table (the port
 * takes the context or its slot), the vendor return values of the register
 * writers (the port returns the write count).
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15)
 * by tests/t32_oracle (t32_decmp_oracle).
 */
#ifndef TX_ISP_T32_DECMP_H
#define TX_ISP_T32_DECMP_H

#include "tx_isp_t32_tp.h"

#define T32_DECMP_PARAM_OFF 0xf00cU
#define T32_DECMP_BYTES 100U
#define T32_DECMP_WRITES 23
#define T32_DECMP_GFP 0x2400c0U
#define T32_DECMP_REG_KNOT 0x27010U
#define T32_DECMP_REG_VAL 0x27028U
#define T32_DECMP_REG_SLOPE 0x27040U
#define T32_DECMP_SLOPE_SHIFT 7U
#define T32_DECMP_HALF 16U
#define T32_DECMP_M12 0xfffU
#define T32_DECMP_M16 0xffffU
#define T32_DECMP_M24 0xffffffU

struct t32_decmp_ctx {
	u32 *param;		/* section, tuning base + 0xf00c */
	u32 rsvd;		/* -1, not read */
};

/* slope of one segment, the vendor divu gives all ones for a zero divisor */
static inline u32 t32_decmp_slope(u32 num, u32 den)
{
	if (!den)
		return ~0U;
	return (num << T32_DECMP_SLOPE_SHIFT) / den;
}

/* tisp_decmp_ref_reg_cfg */
static inline int t32_decmp_ref_reg_cfg(const struct t32_decmp_ctx *ctx,
					struct t32_front_write *w)
{
	const u32 *p = ctx->param;
	u32 d[11];
	unsigned int n = 0, k;

	for (k = 0; k < 11; k++)
		d[k] = t32_decmp_slope(p[14 + k] - p[13 + k], p[2 + k] - p[1 + k]);
	for (k = 0; k < 6; k++)
		T32_FWR(w, n, T32_DECMP_REG_KNOT + 4 * k,
			((p[2 * k + 2] & T32_DECMP_M12) << T32_DECMP_HALF) |
			(p[2 * k + 1] & T32_DECMP_M12));
	for (k = 0; k < 6; k++)
		T32_FWR(w, n, T32_DECMP_REG_VAL + 4 * k,
			(p[13 + 2 * k] & T32_DECMP_M16) |
			(p[14 + 2 * k] << T32_DECMP_HALF));
	for (k = 0; k < 11; k++)
		T32_FWR(w, n, T32_DECMP_REG_SLOPE + 4 * k, d[k] & T32_DECMP_M24);
	return (int)n;
}

/* tisp_decmp_all_reg_refresh */
static inline int t32_decmp_all_reg_refresh(const struct t32_decmp_ctx *ctx,
					    struct t32_front_write *w)
{
	return t32_decmp_ref_reg_cfg(ctx, w);
}

/* tisp_decmp_wake_up */
static inline int t32_decmp_wake_up(const struct t32_decmp_ctx *ctx,
				    struct t32_front_write *w)
{
	return t32_decmp_ref_reg_cfg(ctx, w);
}

/* tisp_decmp_init: allocate and clear the context, point it at the section */
static inline int t32_decmp_init(struct t32_decmp_ctx **slot,
				 void *(*alloc)(u32 size, u32 flags), u8 *tuning_base)
{
	struct t32_decmp_ctx *ctx = alloc(sizeof(*ctx), T32_DECMP_GFP);

	*slot = ctx;
	if (!ctx)
		return -1;
	memset(ctx, 0, sizeof(*ctx));
	ctx->param = (u32 *)(tuning_base + T32_DECMP_PARAM_OFF);
	ctx->rsvd = (u32)-1;
	return 0;
}

/* tisp_decmp_deinit: free the context only (the section belongs to the bin) */
static inline int t32_decmp_deinit(void (*kfree)(void *p), struct t32_decmp_ctx **slot)
{
	if (*slot) {
		kfree(*slot);
		*slot = NULL;
	}
	return 0;
}

/* tisp_decmp_dn_params_refresh: re-point at the section, rewrite registers */
static inline int t32_decmp_dn_params_refresh(struct t32_decmp_ctx *ctx, u8 *tuning_base,
					      struct t32_front_write *w)
{
	ctx->param = (u32 *)(tuning_base + T32_DECMP_PARAM_OFF);
	return t32_decmp_ref_reg_cfg(ctx, w);
}

/* tisp_decmp_param_array_get */
static inline int t32_decmp_param_array_get(const struct t32_decmp_ctx *ctx, u8 *buf, u32 *len)
{
	memcpy(buf, ctx->param, T32_DECMP_BYTES);
	*len = T32_DECMP_BYTES;
	return 0;
}

/* tisp_decmp_param_array_set: copy in, then tisp_decmp_ref_reg_cfg; returns the write count */
static inline int t32_decmp_param_array_set(struct t32_decmp_ctx *ctx, const u8 *buf,
					    struct t32_front_write *w)
{
	memcpy(ctx->param, buf, T32_DECMP_BYTES);
	return t32_decmp_ref_reg_cfg(ctx, w);
}

#endif /* TX_ISP_T32_DECMP_H */
