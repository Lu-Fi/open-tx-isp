/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 raw denoise (RDNS, register block 0x26000).
 *
 * Vendor functions (Matt Davis' recovered C, PR #15): tisp_rdns_noref_reg_cfg,
 * tisp_rdns_ref_reg_cfg, tisp_rdns_reg_trig, tisp_rdns_intp,
 * tisp_rdns_all_reg_refresh, tisp_rdns_intp_reg_refresh,
 * tisp_rdns_par_refresh, tisp_rdns_refresh, tisp_rdns_init,
 * tisp_rdns_deinit, tisp_rdns_dn_params_refresh, tisp_rdns_param_array_get,
 * tisp_rdns_param_array_set, tisp_rdns_wake_up.
 *
 * IQ section: 1540 bytes, id word 0x0134dacd, at tuning base + 0x62c0
 * (t32_data_dd0a4[ch] + 25280).  Layout:
 *   +4, +26, +48 ...   66 tables of 11 u16 (stride 22), gain interpolated
 *   +1456 .. +1459     four static flag bytes (registers 0x26040/0x26044)
 *   +1460 ...          7 tables of 11 u8 (stride 11)
 * tisp_rdns_intp interpolates them at the gain (index = gain >> 16, fraction
 * = gain & 0xffff) into a 140 byte parameter block.  Byte 5 is never written.
 * Param bytes 0..2 select three 64-byte rows (32 u16) of the vendor table
 * t32_data_c0ac0; their low bytes become registers 0x2604c.. (24 words),
 * the rest of the block feeds 0x260ac.. (34 words).
 *
 * Registers: 0x26000 id, 0x2603c trigger, 0x26040/0x26044 static flags,
 * 0x2604c .. 0x26130 interpolated parameters.
 *
 * NOT ported: the debug print on a section id mismatch in tisp_rdns_init,
 * the vendor allocation failure print, the 0x2.... trace-id variant of the
 * register address used when t32_data_d03a0 != 0.  The vendor context array
 * t32_data_e3600 is replaced by a context pointer (slot for init/deinit);
 * the row table t32_data_c0ac0 is passed as ctx->rows.  Functions return the
 * number of register writes (the vendor returns 0 or garbage).
 *
 * Checked bit-exact against the vendor flow by tests/t32_oracle
 * (t32_rdns_oracle).
 */
#ifndef TX_ISP_T32_RDNS_H
#define TX_ISP_T32_RDNS_H

#include "tx_isp_t32_tp.h"

#define T32_RDNS_ID 0x0134dacdU
#define T32_RDNS_SEC_OFF 0x62c0U
#define T32_RDNS_SEC_BYTES 1540U
#define T32_RDNS_PARAM_BYTES 140U
#define T32_RDNS_MAX_WRITES 64
#define T32_RDNS_GAIN_NONE 0xffffffffU
#define T32_RDNS_GAIN_UNITY 0x10000U
#define T32_RDNS_REFRESH_THRESHOLD 256U

#define T32_RDNS_REG_ID 0x26000U
#define T32_RDNS_REG_TRIG 0x2603cU
#define T32_RDNS_REG_NOREF 0x26040U
#define T32_RDNS_REG_REF 0x2604cU

#define T32_RDNS_SEC_NOREF 1456U
#define T32_RDNS_SEC_U16_FIRST 4U
#define T32_RDNS_SEC_U16_STRIDE 22U
#define T32_RDNS_SEC_U16(j) (T32_RDNS_SEC_U16_FIRST + (j) * T32_RDNS_SEC_U16_STRIDE)
#define T32_RDNS_SEC_U8_FIRST 1460U
#define T32_RDNS_SEC_U8_STRIDE 11U
#define T32_RDNS_SEC_U8(i) (T32_RDNS_SEC_U8_FIRST + (i) * T32_RDNS_SEC_U8_STRIDE)

struct t32_rdns_ctx {
	u8 *sec;		/* IQ section (1540 bytes) */
	u8 *param;		/* 140 interpolated parameter bytes */
	u32 gain;		/* last accepted gain, 0xffffffff = none */
	u32 rsvd;
	const u8 *rows;		/* vendor table t32_data_c0ac0, 64-byte rows */
};

static inline u32 t32_rdns_le16(const u8 *p)
{
	return (u32)p[0] | (u32)p[1] << 8;
}

static inline void t32_rdns_put16(u8 *p, u32 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
}

/* tisp_simple_intp_uint8: 11 entry table, index >= 10 returns the last */
static inline u32 t32_rdns_intp8(u32 idx, u32 frac, const u8 *t)
{
	u32 a, b, d;

	if (idx >= 10)
		return t[10];
	a = t[idx];
	b = t[idx + 1];
	if (a == b)
		return b;
	d = a > b ? a - b : b - a;
	d *= frac;
	d = (d >> 16) + ((d >> 15) & 1);
	return (a > b ? a - d : a + d) & 0xff;
}

/* tisp_simple_intp_uint16 */
static inline u32 t32_rdns_intp16(u32 idx, u32 frac, const u8 *t)
{
	u32 a, b, d;

	if (idx >= 10)
		return t32_rdns_le16(t + 20);
	a = t32_rdns_le16(t + 2 * idx);
	b = t32_rdns_le16(t + 2 * idx + 2);
	if (a == b)
		return b;
	d = a > b ? a - b : b - a;
	d *= frac;
	d = (d >> 16) + ((d >> 15) & 1);
	return (a > b ? a - d : a + d) & 0xffffU;
}

/* four u16 entries -> one register, low bytes only */
static inline u32 t32_rdns_pack4(u32 e0, u32 e1, u32 e2, u32 e3)
{
	return (e0 & 0xffU) | (e1 & 0xffU) << 8 | (e2 & 0xffU) << 16 | (e3 & 0xffU) << 24;
}

/* tisp_rdns_intp */
static inline void t32_rdns_intp(struct t32_rdns_ctx *ctx, u32 gain)
{
	const u8 *s = ctx->sec;
	u8 *p = ctx->param;
	u32 idx = gain >> 16;
	u32 frac = gain & 0xffffU;
	unsigned int k;

	for (k = 0; k < 5; k++)
		p[k] = (u8)t32_rdns_intp8(idx, frac, s + T32_RDNS_SEC_U8(k));
	t32_rdns_put16(p + 6, t32_rdns_intp16(idx, frac, s + T32_RDNS_SEC_U16(0)));
	t32_rdns_put16(p + 8, t32_rdns_intp16(idx, frac, s + T32_RDNS_SEC_U16(1)));
	p[10] = (u8)t32_rdns_intp8(idx, frac, s + T32_RDNS_SEC_U8(5));
	p[11] = (u8)t32_rdns_intp8(idx, frac, s + T32_RDNS_SEC_U8(6));
	for (k = 0; k < 64; k++)
		t32_rdns_put16(p + 12 + 2 * k,
			       t32_rdns_intp16(idx, frac, s + T32_RDNS_SEC_U16(k + 2)));
}

/* tisp_rdns_noref_reg_cfg */
static inline void t32_rdns_noref_reg_cfg(const struct t32_rdns_ctx *ctx,
					  struct t32_front_write *w, unsigned int *n)
{
	const u8 *sec = ctx->sec;

	T32_FWR(w, *n, T32_RDNS_REG_NOREF, sec[T32_RDNS_SEC_NOREF] & 7U);
	T32_FWR(w, *n, T32_RDNS_REG_NOREF + 4, (sec[T32_RDNS_SEC_NOREF + 1] & 1U) | 1U << 16 |
		(u32)(sec[T32_RDNS_SEC_NOREF + 2] & 1) << 8 |
		(u32)(sec[T32_RDNS_SEC_NOREF + 3] & 1) << 24);
}

/* tisp_rdns_ref_reg_cfg */
static inline void t32_rdns_ref_reg_cfg(const struct t32_rdns_ctx *ctx,
					struct t32_front_write *w, unsigned int *n)
{
	const u8 *p = ctx->param;
	const u8 *row, *e;
	u32 reg = T32_RDNS_REG_REF;
	unsigned int r, g, i;

	for (r = 0; r < 3; r++) {
		row = ctx->rows + ((u32)p[r] << 6);
		for (g = 0; g < 8; g++) {
			e = row + g * 8;
			T32_FWR(w, *n, reg, t32_rdns_pack4(t32_rdns_le16(e), t32_rdns_le16(e + 2),
							   t32_rdns_le16(e + 4), t32_rdns_le16(e + 6)));
			reg += 4;
		}
	}
	T32_FWR(w, *n, reg, (u32)(p[4] & 7) << 8 | (u32)(p[10] & 3) << 16 |
		(p[3] & 7U) | (u32)(p[11] & 3) << 24);
	reg += 4;
	T32_FWR(w, *n, reg, (t32_rdns_le16(p + 8) & 0xfffU) << 16 | (t32_rdns_le16(p + 6) & 0xfffU));
	reg += 4;
	for (i = 0; i < 32; i++) {
		T32_FWR(w, *n, reg, (t32_rdns_le16(p + 14 + 4 * i) & 0x1ffU) << 16 |
			(t32_rdns_le16(p + 12 + 4 * i) & 0x1ffU));
		reg += 4;
	}
}

/* tisp_rdns_reg_trig */
static inline void t32_rdns_reg_trig(struct t32_front_write *w, unsigned int *n)
{
	T32_FWR(w, *n, T32_RDNS_REG_TRIG, 1);
}

/* tisp_rdns_all_reg_refresh */
static inline void t32_rdns_all_reg_refresh_n(struct t32_rdns_ctx *ctx, u32 gain,
					      struct t32_front_write *w, unsigned int *n)
{
	t32_rdns_intp(ctx, gain);
	t32_rdns_noref_reg_cfg(ctx, w, n);
	t32_rdns_ref_reg_cfg(ctx, w, n);
	t32_rdns_reg_trig(w, n);
}

static inline int t32_rdns_all_reg_refresh(struct t32_rdns_ctx *ctx, u32 gain,
					   struct t32_front_write *w)
{
	unsigned int n = 0;

	t32_rdns_all_reg_refresh_n(ctx, gain, w, &n);
	return (int)n;
}

/* tisp_rdns_intp_reg_refresh: like all_reg_refresh without the static flags */
static inline void t32_rdns_intp_reg_refresh_n(struct t32_rdns_ctx *ctx, u32 gain,
					       struct t32_front_write *w, unsigned int *n)
{
	t32_rdns_intp(ctx, gain);
	t32_rdns_ref_reg_cfg(ctx, w, n);
	t32_rdns_reg_trig(w, n);
}

static inline int t32_rdns_intp_reg_refresh(struct t32_rdns_ctx *ctx, u32 gain,
					    struct t32_front_write *w)
{
	unsigned int n = 0;

	t32_rdns_intp_reg_refresh_n(ctx, gain, w, &n);
	return (int)n;
}

/* tisp_rdns_par_refresh: first call (no gain yet) writes everything */
static inline void t32_rdns_par_refresh_n(struct t32_rdns_ctx *ctx, u32 gain, u32 threshold,
					  struct t32_front_write *w, unsigned int *n)
{
	u32 old = ctx->gain;
	u32 diff;

	if (old == T32_RDNS_GAIN_NONE) {
		t32_rdns_all_reg_refresh_n(ctx, gain, w, n);
		ctx->gain = gain;
		return;
	}
	diff = gain < old ? old - gain : gain - old;
	if (diff < threshold)
		return;
	t32_rdns_intp_reg_refresh_n(ctx, gain, w, n);
	ctx->gain = gain;
}

static inline int t32_rdns_par_refresh(struct t32_rdns_ctx *ctx, u32 gain, u32 threshold,
				       struct t32_front_write *w)
{
	unsigned int n = 0;

	t32_rdns_par_refresh_n(ctx, gain, threshold, w, &n);
	return (int)n;
}

/* tisp_rdns_refresh */
static inline int t32_rdns_refresh(struct t32_rdns_ctx *ctx, u32 gain,
				   struct t32_front_write *w)
{
	return t32_rdns_par_refresh(ctx, gain, T32_RDNS_REFRESH_THRESHOLD, w);
}

/*
 * tisp_rdns_init: allocate context and parameter block, point at the section
 * in the tuning base, refresh at unity gain, write the block id.
 */
static inline int t32_rdns_init(struct t32_rdns_ctx **slot,
				void *(*alloc_ctx)(unsigned long size),
				void *(*alloc_param)(unsigned long size),
				u8 *tuning_base, const u8 *rows, struct t32_front_write *w)
{
	struct t32_rdns_ctx *ctx;
	unsigned int n = 0;

	ctx = alloc_ctx(sizeof(*ctx));
	*slot = ctx;
	if (!ctx)
		return -1;
	memset(ctx, 0, sizeof(*ctx));
	ctx->param = alloc_param(T32_RDNS_PARAM_BYTES);
	if (!ctx->param)
		return -1;
	memset(ctx->param, 0, T32_RDNS_PARAM_BYTES);
	ctx->sec = tuning_base + T32_RDNS_SEC_OFF;
	ctx->rows = rows;
	ctx->gain = T32_RDNS_GAIN_NONE;
	t32_rdns_par_refresh_n(ctx, T32_RDNS_GAIN_UNITY, T32_RDNS_REFRESH_THRESHOLD, w, &n);
	T32_FWR(w, n, T32_RDNS_REG_ID, T32_RDNS_ID);
	return (int)n;
}

/* tisp_rdns_deinit */
static inline int t32_rdns_deinit(void (*free_param)(void *p), void (*free_ctx)(void *p),
				  struct t32_rdns_ctx **slot)
{
	struct t32_rdns_ctx *ctx = *slot;

	if (ctx) {
		if (ctx->param) {
			free_param(ctx->param);
			ctx->param = NULL;
		}
		free_ctx(*slot);
		*slot = NULL;
	}
	return 0;
}

/* tisp_rdns_dn_params_refresh: section from the tuning base, write all at the stored gain */
static inline int t32_rdns_dn_params_refresh(struct t32_rdns_ctx *ctx, u8 *tuning_base,
					     struct t32_front_write *w)
{
	ctx->sec = tuning_base + T32_RDNS_SEC_OFF;
	return t32_rdns_all_reg_refresh(ctx, ctx->gain, w);
}

/* tisp_rdns_param_array_get */
static inline int t32_rdns_param_array_get(const struct t32_rdns_ctx *ctx, u8 *buf, u32 *len)
{
	memcpy(buf, ctx->sec, T32_RDNS_SEC_BYTES);
	*len = T32_RDNS_SEC_BYTES;
	return 0;
}

/* tisp_rdns_param_array_set */
static inline int t32_rdns_param_array_set(struct t32_rdns_ctx *ctx, const u8 *buf,
					   struct t32_front_write *w)
{
	memcpy(ctx->sec, buf, T32_RDNS_SEC_BYTES);
	return t32_rdns_all_reg_refresh(ctx, ctx->gain, w);
}

/* tisp_rdns_wake_up: rewrite everything from the stored parameters */
static inline int t32_rdns_wake_up(const struct t32_rdns_ctx *ctx, struct t32_front_write *w)
{
	unsigned int n = 0;

	t32_rdns_noref_reg_cfg(ctx, w, &n);
	t32_rdns_ref_reg_cfg(ctx, w, &n);
	t32_rdns_reg_trig(w, &n);
	return (int)n;
}

#endif /* TX_ISP_T32_RDNS_H */
