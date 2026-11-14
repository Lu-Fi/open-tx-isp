/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 colour denoise (CDNS, block 0x12000) from the IQ bin at AddSensor (S4
 * step 5): tisp_cdns_init -> tisp_cdns_intp_reg_refresh (39 point tables
 * interpolated at the gain index 1.0 = 0x10000, tisp_cdns_reg_cfg and the
 * 0x12010 trigger).  There is no block id write.
 *
 * IQ bin section 18 (436 bytes): tables of 11 bytes from +6, a flag at +0x1a6.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15)
 * by tests/t32_oracle (t32_cdns_oracle, t32_cdnsf_oracle).
 */
#ifndef TX_ISP_T32_CDNS_H
#define TX_ISP_T32_CDNS_H

#include "tx_isp_t32_tp.h"

#define T32_SEC_CDNS		18
#define T32_CDNS_SEC_BYTES	436U
#define T32_CDNS_WRITES		16

/* { table offset in the section, output byte } of tisp_cdns_intp */
static const u16 t32_cdns_map[39][2] = {
	{0x4, 0}, {0xf, 1}, {0x1a, 2}, {0x25, 3}, {0x30, 4}, {0x3b, 5}, {0x46, 6}, {0x51, 7}, 
	{0x5c, 8}, {0x67, 9}, {0x72, 10}, {0x7d, 11}, {0x88, 12}, {0x93, 13}, {0x9e, 14}, {0xa9, 
	15}, {0xb4, 16}, {0xbf, 17}, {0xca, 18}, {0xd5, 19}, {0xe0, 20}, {0xeb, 21}, {0xf6, 22}, 
	{0x101, 23}, {0x10c, 24}, {0x117, 25}, {0x122, 26}, {0x12d, 27}, {0x138, 28}, {0x143, 
	29}, {0x14e, 30}, {0x159, 31}, {0x164, 32}, {0x16f, 33}, {0x17a, 34}, {0x185, 35}, 
	{0x190, 36}, {0x19b, 37}, {0x1a7, 38}
};

static inline void t32_cdns_reg_trig(struct t32_front_write *w, unsigned int *n)
{
	T32_FWR(w, *n, 0x12010, 1);
}

/* tisp_cdns_reg_cfg: 15 parameter registers, one shared by init/refresh/wake */
static inline void t32_cdns_reg_cfg(const u8 *sec, const u8 *o,
				    struct t32_front_write *w,
				    unsigned int *count)
{
	unsigned int n = *count, k;

	T32_FWR(w, n, 0x12040, o[0] & 7U);
	for (k = 0; k < 8; k++)
		T32_FWR(w, n, 0x12050 + 4 * k, (u32)o[0xf + 3 * k] << 8 | (u32)o[0x10 + 3 * k] << 16 | o[0xe + 3 * k]);
	T32_FWR(w, n, 0x12070, (u32)o[4] << 24 | o[1] | (u32)(o[2] & 0x3f) << 8 | (u32)(o[3] & 0x3f) << 16);
	T32_FWR(w, n, 0x12074, (u32)(o[6] & 0x3f) << 8 | (u32)(o[7] & 0x3f) << 16 | (o[5] & 7U));
	T32_FWR(w, n, 0x12078, o[8] & 3U);
	T32_FWR(w, n, 0x1207c, (u32)(o[10] & 0xf) << 16 | o[9]);
	T32_FWR(w, n, 0x12080, (u32)(o[0xc] & 0x3f) << 8 | (u32)(o[0xd] & 0x3f) << 16 | (o[0xb] & 7U));
	T32_FWR(w, n, 0x12084, (sec[0x1a6] & 1U) | (u32)o[0x26] << 4);
	*count = n;
}

static inline int t32_cdns_init(const u8 *sec, struct t32_front_write *w)
{
	u8 o[39];
	unsigned int n = 0, k;

	for (k = 0; k < 39; k++)
		o[t32_cdns_map[k][1]] = t32_tp_intp_u8(1, 0, sec + t32_cdns_map[k][0]);
	t32_cdns_reg_cfg(sec, o, w, &n);
	t32_cdns_reg_trig(w, &n);
	return (int)n;
}

/*
 * Per-frame path: tisp_cdns_par_refresh / tisp_cdns_refresh /
 * tisp_cdns_dn_params_refresh / tisp_cdns_wake_up.
 */
struct t32_cdns_ctx {
	u8 *sec;		/* IQ bin section 18 */
	u8 *param;		/* 39 interpolated parameter bytes */
	u32 gain;		/* last accepted gain */
	u32 rsvd;
};

/* tisp_cdns_intp */
static inline void t32_cdns_intp(struct t32_cdns_ctx *ctx, u32 gain)
{
	unsigned int k;

	for (k = 0; k < 39; k++)
		ctx->param[t32_cdns_map[k][1]] =
			t32_tp_intp_u8(gain >> 16, gain & 0xffff,
				       ctx->sec + t32_cdns_map[k][0]);
}

static inline void t32_cdns_intp_reg_refresh(struct t32_cdns_ctx *ctx, u8 ch,
					     u32 gain,
					     struct t32_front_write *w,
					     unsigned int *n)
{
	(void)ch;
	t32_cdns_intp(ctx, gain);
	t32_cdns_reg_cfg(ctx->sec, ctx->param, w, n);
	t32_cdns_reg_trig(w, n);
}

static inline void t32_cdns_par_refresh_impl(struct t32_cdns_ctx *ctx, u8 ch,
					     u32 gain, u32 threshold,
					     struct t32_front_write *w,
					     unsigned int *n)
{
	u32 old = ctx->gain;

	if (old == (u32)-1) {
		ctx->gain = gain;
		t32_cdns_intp_reg_refresh(ctx, ch, gain, w, n);
	} else {
		u32 diff = gain > old ? gain - old : old - gain;

		if (diff >= threshold) {
			ctx->gain = gain;
			t32_cdns_intp_reg_refresh(ctx, ch, gain, w, n);
		}
	}
}

static inline int t32_cdns_par_refresh(struct t32_cdns_ctx *ctx, u8 ch, u32 gain,
				       u32 threshold, struct t32_front_write *w)
{
	unsigned int n = 0;

	t32_cdns_par_refresh_impl(ctx, ch, gain, threshold, w, &n);
	return (int)n;
}

static inline int t32_cdns_refresh(struct t32_cdns_ctx *ctx, u8 ch, u32 gain,
				   struct t32_front_write *w)
{
	unsigned int n = 0;

	t32_cdns_par_refresh_impl(ctx, ch, gain, 256U, w, &n);
	return (int)n;
}

static inline int t32_cdns_dn_params_refresh(struct t32_cdns_ctx *ctx, u8 ch,
					     u8 *tuning_base,
					     struct t32_front_write *w)
{
	unsigned int n = 0;

	ctx->sec = tuning_base + 0xcfa0;
	t32_cdns_intp_reg_refresh(ctx, ch, ctx->gain, w, &n);
	return (int)n;
}

static inline int t32_cdns_wake_up(struct t32_cdns_ctx *ctx, u8 ch,
				   struct t32_front_write *w)
{
	unsigned int n = 0;

	(void)ch;
	t32_cdns_reg_cfg(ctx->sec, ctx->param, w, &n);
	t32_cdns_reg_trig(w, &n);
	return (int)n;
}

/*
 * Parameter accessors (tisp_cdns_param_array_get / _set / _deinit /
 * _all_reg_refresh).  The vendor indexes the context array t32_data_e23d0 by
 * channel; the port takes the context (or its slot) directly.
 */
#define T32_CDNS_PARAM_BYTES	436U

/* tisp_cdns_param_array_get: the section is copied out whole */
static inline int t32_cdns_param_array_get(const struct t32_cdns_ctx *ctx, u8 *buf, u32 *len)
{
	memcpy(buf, ctx->sec, T32_CDNS_PARAM_BYTES);
	*len = T32_CDNS_PARAM_BYTES;
	return 0;
}

/* tisp_cdns_all_reg_refresh: the interpolate + write path of the refresh */
static inline int t32_cdns_all_reg_refresh(struct t32_cdns_ctx *ctx, u8 ch, u32 gain,
					   struct t32_front_write *w)
{
	unsigned int n = 0;

	t32_cdns_intp_reg_refresh(ctx, ch, gain, w, &n);
	return (int)n;
}

/* tisp_cdns_param_array_set: replace the section, then refresh at the stored gain */
static inline int t32_cdns_param_array_set(struct t32_cdns_ctx *ctx, u8 ch, const u8 *buf,
					   struct t32_front_write *w)
{
	memcpy(ctx->sec, buf, T32_CDNS_PARAM_BYTES);
	return t32_cdns_all_reg_refresh(ctx, ch, ctx->gain, w);
}

/* tisp_cdns_deinit: free the context of the channel and clear its slot */
static inline int t32_cdns_deinit(void (*kfree)(void *p), struct t32_cdns_ctx **slot)
{
	if (*slot) {
		kfree(*slot);
		*slot = NULL;
	}
	return 0;
}

/*
 * T33 (PRJ008) CDNS: section 18 is 588 bytes; the T33 module interpolates
 * 54 values at the gain 1.0 (0x10000) at init: 38 tables of 11 bytes from
 * +4, the flag at +0x1a6, 15 more tables from +0x1a7, and writes the T32's
 * registers plus 0x12088..0x12094 (tisp_cdns_par_refresh +
 * func_cdns_para_update of the T33 module).  Module list 21.
 */
#define T33_CDNS_SEC_BYTES	588U
#define T33_CDNS_WRITES		21

/* at the gain (log2 Q16) of tisp_cdns_par_refresh; init: 0x10000 */
static inline void t33_cdns_par(const u8 *sec, u32 gain, u8 *o)
{
	u32 gi = gain >> 16, gf = gain & 0xffff;
	unsigned int k;

	for (k = 0; k < 0x26; k++)
		o[k] = t32_tp_intp_u8(gi, gf, sec + 4 + 11 * k);
	o[0x26] = sec[0x1a6];
	for (k = 0x27; k < 0x36; k++)
		o[k] = t32_tp_intp_u8(gi, gf, sec + 0x1a7 + 11 * (k - 0x27));
}

/* cdnsInfo (440 bytes): func_cdns_para_update's copies of the 0x36 values in words 54..107 */
#define T33_CDNS_INFO_WORDS	110
static inline void t33_cdns_info(const u8 *sec, u32 gain, u32 *inf)
{
	u8 o[0x36];
	unsigned int k;

	t33_cdns_par(sec, gain, o);
	for (k = 0; k < 0x36; k++)
		inf[54 + k] = o[k];
}

static inline int t33_cdns_gain(const u8 *sec, u32 gain, struct t32_front_write *w)
{
	u8 o[0x36];
	unsigned int n = 0, k;

	t33_cdns_par(sec, gain, o);
	T32_FWR(w, n, 0x12040, o[0] & 7U);
	for (k = 0; k < 8; k++)
		T32_FWR(w, n, 0x12050 + 4 * k, (u32)o[2 + 3 * k] << 8 | (u32)o[3 + 3 * k] << 16 | o[1 + 3 * k]);
	T32_FWR(w, n, 0x12070, o[0x19] | (u32)o[0x1c] << 24 | (u32)(o[0x1a] & 0x3f) << 8 |
		(u32)(o[0x1b] & 0x3f) << 16);
	T32_FWR(w, n, 0x12074, (u32)(o[0x1e] & 0x3f) << 8 | (u32)(o[0x1f] & 0x3f) << 16 | (o[0x1d] & 7U));
	T32_FWR(w, n, 0x12078, o[0x20] & 3U);
	T32_FWR(w, n, 0x1207c, (u32)(o[0x22] & 0xf) << 16 | o[0x21]);
	T32_FWR(w, n, 0x12080, (u32)(o[0x24] & 0x3f) << 8 | (u32)(o[0x25] & 0x3f) << 16 | (o[0x23] & 7U));
	T32_FWR(w, n, 0x12084, (u32)o[0x27] << 4 | (o[0x26] & 1U));
	T32_FWR(w, n, 0x12088, (u32)(o[0x2f] & 0x1f) << 8 | (u32)(o[0x30] & 0x3f) << 16 |
		(o[0x2e] & 0x3fU) | (u32)(o[0x31] & 0x3f) << 24);
	T32_FWR(w, n, 0x1208c, (u32)o[0x33] << 8 | (u32)o[0x35] << 16 | (o[0x32] & 0x1fU) |
		(u32)(o[0x34] & 1) << 24);
	T32_FWR(w, n, 0x12090, (u32)(o[0x2b] & 0x3f) << 8 | (u32)(o[0x2c] & 0x3f) << 16 |
		(o[0x2a] & 0x3fU) | (u32)(o[0x2d] & 0x3f) << 24);
	T32_FWR(w, n, 0x12094, (u32)(o[0x29] & 0x3f) << 8 | (o[0x28] & 0x3fU));
	T32_FWR(w, n, 0x12010, 1);
	return (int)n;
}

static inline int t33_cdns_init(const u8 *sec, struct t32_front_write *w)
{
	return t33_cdns_gain(sec, 0x10000, w);
}

#endif /* TX_ISP_T32_CDNS_H */

