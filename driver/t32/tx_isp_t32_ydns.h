/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 luma denoise (YDNS, block 0xe000) from the IQ bin at AddSensor (S4
 * step 5): tisp_ydns_init -> tisp_ydns_refresh (parameter refresh and
 * tisp_ydns_intp_reg_refresh, each interpolating the bin's 11 point tables
 * at the gain index 1.0 = 0x10000 and writing the block through
 * tisp_ydns_reg_cfg plus the 0xe010 trigger) and the block id.
 *
 * IQ bin section 17 (248 bytes): +4 / +5 enable bits, then 22 tables of 11
 * bytes from +6, 11 bytes apart (table k at +6 + 11 k).
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15)
 * by tests/t32_oracle (t32_ydns_oracle, t32_ydnsf_oracle).
 */
#ifndef TX_ISP_T32_YDNS_H
#define TX_ISP_T32_YDNS_H

#include "tx_isp_t32_tp.h"

#define T32_SEC_YDNS		17
#define T32_YDNS_SEC_BYTES	248U
#define T32_YDNS_SEC_OFF	0xcea8
#define T32_YDNS_ID		0x0134da11U
#define T32_YDNS_WRITES		17

static inline int t32_ydns_init(const u8 *sec, struct t32_front_write *w)
{
	/* output byte of table k: the vendor's tisp_ydns_intp assignment */
	static const u8 dst[22] = { 12, 13, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 14, 15, 16, 17, 18, 19, 20, 21 };
	u8 o[22];
	unsigned int n = 0, k, pass;

	for (k = 0; k < 22; k++)
		o[dst[k]] = t32_tp_intp_u8(1, 0, sec + 6 + 11 * k);
	for (pass = 0; pass < 2; pass++) {
		T32_FWR(w, n, 0xe050, (u32)(sec[5] & 1) << 4 | (sec[4] & 1U));
		T32_FWR(w, n, 0xe060, (u32)(o[1] & 3) << 8 | (u32)(o[2] & 3) << 12 | o[0]);
		T32_FWR(w, n, 0xe064, t32_front_le32(o + 3));
		T32_FWR(w, n, 0xe068, t32_front_le32(o + 7));
		T32_FWR(w, n, 0xe070, (u32)o[0xc] << 8 | (u32)o[0xd] << 16 | o[0xb]);
		T32_FWR(w, n, 0xe074, t32_front_le32(o + 0xe));
		T32_FWR(w, n, 0xe078, t32_front_le32(o + 0x12));
		T32_FWR(w, n, 0xe010, 1);
	}
	T32_FWR(w, n, 0xe000, T32_YDNS_ID);
	return (int)n;
}

/*
 * Per-frame path: tisp_ydns_par_refresh / tisp_ydns_refresh /
 * tisp_ydns_dn_params_refresh / tisp_ydns_wake_up.
 */
struct t32_ydns_ctx {
	u8 *sec;		/* IQ bin section 17 */
	u8 *param;		/* 22 interpolated parameter bytes */
	u32 gain;		/* last accepted gain */
	u32 threshold;		/* refresh hysteresis */
};

static inline void t32_ydns_intp(struct t32_ydns_ctx *ctx, u8 ch, u32 gain)
{
	static const u8 dst[22] = { 12, 13, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 14, 15, 16, 17, 18, 19, 20, 21 };
	u8 *o = ctx->param;
	u8 *sec = ctx->sec;
	unsigned int k;

	(void)ch;
	for (k = 0; k < 22; k++)
		o[dst[k]] = t32_tp_intp_u8(gain >> 16, gain & 0xffff,
					   sec + 6 + 11 * k);
}

static inline void t32_ydns_reg_cfg(const struct t32_ydns_ctx *ctx, u8 ch,
				    struct t32_front_write *w, unsigned int *n)
{
	const u8 *sec = ctx->sec;
	const u8 *o = ctx->param;

	(void)ch;
	T32_FWR(w, *n, 0xe050, (u32)(sec[5] & 1) << 4 | (sec[4] & 1U));
	T32_FWR(w, *n, 0xe060, (u32)(o[1] & 3) << 8 | (u32)(o[2] & 3) << 12 | o[0]);
	T32_FWR(w, *n, 0xe064, t32_front_le32(o + 3));
	T32_FWR(w, *n, 0xe068, t32_front_le32(o + 7));
	T32_FWR(w, *n, 0xe070, (u32)o[0xc] << 8 | (u32)o[0xd] << 16 | o[0xb]);
	T32_FWR(w, *n, 0xe074, t32_front_le32(o + 0xe));
	T32_FWR(w, *n, 0xe078, t32_front_le32(o + 0x12));
}

static inline void t32_ydns_reg_trig(u8 ch, struct t32_front_write *w,
				     unsigned int *n)
{
	(void)ch;
	T32_FWR(w, *n, 0xe010, 1);
}

static inline void t32_ydns_intp_reg_refresh(struct t32_ydns_ctx *ctx, u8 ch,
					     u32 gain, struct t32_front_write *w,
					     unsigned int *n)
{
	t32_ydns_intp(ctx, ch, gain);
	t32_ydns_reg_cfg(ctx, ch, w, n);
	t32_ydns_reg_trig(ch, w, n);
}

static inline void t32_ydns_par_refresh_impl(struct t32_ydns_ctx *ctx, u8 ch,
					     u32 gain, struct t32_front_write *w,
					     unsigned int *n)
{
	u32 old = ctx->gain;

	if (old == (u32)-1) {
		ctx->gain = gain;
		t32_ydns_intp_reg_refresh(ctx, ch, gain, w, n);
	} else {
		u32 diff = gain > old ? gain - old : old - gain;

		if (diff >= ctx->threshold) {
			ctx->gain = gain;
			t32_ydns_intp_reg_refresh(ctx, ch, gain, w, n);
		}
	}
}

static inline int t32_ydns_par_refresh(struct t32_ydns_ctx *ctx, u8 ch,
				       u32 gain, struct t32_front_write *w)
{
	unsigned int n = 0;

	t32_ydns_par_refresh_impl(ctx, ch, gain, w, &n);
	return (int)n;
}

static inline int t32_ydns_refresh(struct t32_ydns_ctx *ctx, u8 ch, u32 gain,
				   struct t32_front_write *w)
{
	unsigned int n = 0;

	t32_ydns_par_refresh_impl(ctx, ch, gain, w, &n);
	t32_ydns_intp_reg_refresh(ctx, ch, ctx->gain, w, &n);
	return (int)n;
}

static inline int t32_ydns_dn_params_refresh(struct t32_ydns_ctx *ctx, u8 ch,
					     u8 *tuning_base, struct t32_front_write *w)
{
	unsigned int n = 0;

	ctx->sec = tuning_base + T32_YDNS_SEC_OFF;
	t32_ydns_intp_reg_refresh(ctx, ch, ctx->gain, w, &n);
	return (int)n;
}

static inline int t32_ydns_wake_up(struct t32_ydns_ctx *ctx, u8 ch,
				   struct t32_front_write *w)
{
	unsigned int n = 0;

	t32_ydns_reg_cfg(ctx, ch, w, &n);
	t32_ydns_reg_trig(ch, w, &n);
	return (int)n;
}

/*
 * Parameter accessors (tisp_ydns_param_array_get / _set / _deinit /
 * _all_reg_refresh).  The vendor indexes the context array t32_data_e23c0 by
 * channel; the port takes the context (or its slot) directly.
 */

/* tisp_ydns_param_array_get: the section is copied out whole */
static inline int t32_ydns_param_array_get(const struct t32_ydns_ctx *ctx, u8 *buf, u32 *len)
{
	memcpy(buf, ctx->sec, T32_YDNS_SEC_BYTES);
	*len = T32_YDNS_SEC_BYTES;
	return 0;
}

/* tisp_ydns_all_reg_refresh: the interpolate + write path of the refresh */
static inline int t32_ydns_all_reg_refresh(struct t32_ydns_ctx *ctx, u8 ch, u32 gain,
					   struct t32_front_write *w)
{
	unsigned int n = 0;

	t32_ydns_intp_reg_refresh(ctx, ch, gain, w, &n);
	return (int)n;
}

/* tisp_ydns_param_array_set: replace the section, then refresh at the stored gain */
static inline int t32_ydns_param_array_set(struct t32_ydns_ctx *ctx, u8 ch, const u8 *buf,
					   struct t32_front_write *w)
{
	memcpy(ctx->sec, buf, T32_YDNS_SEC_BYTES);
	return t32_ydns_all_reg_refresh(ctx, ch, ctx->gain, w);
}

/* tisp_ydns_deinit: free the parameter block and the context, clear the slot */
static inline int t32_ydns_deinit(void (*vfree)(void *p), void (*kfree)(void *p),
				  struct t32_ydns_ctx **slot)
{
	struct t32_ydns_ctx *ctx = *slot;

	if (ctx) {
		if (ctx->param) {
			vfree(ctx->param);
			ctx->param = NULL;
		}
		kfree(*slot);
		*slot = NULL;
	}
	return 0;
}

#endif /* TX_ISP_T32_YDNS_H */

