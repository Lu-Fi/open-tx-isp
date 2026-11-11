/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 LCE (local contrast) small state helpers: readable C of the vendor's
 * tisp_lce_wdr_en, tisp_lce_top_change_state, tisp_lce_dn_params_refresh,
 * tisp_lce_get_hist_data and tisp_lce_get_sum_data.
 *
 * The LCE state of a channel (t32_data_e1da8[ch], 0x22850 B) starts with the
 * pointer to the parameter section in use (tuning base t32_data_dd0a4[ch]
 * + 0x8494).  Used here: the zone histograms (u16, from +164), the zone sums
 * (u32, from +0x90a4), the refresh mode byte (+0x22830: 1 = WDR switch,
 * 3 = full refresh, 4 = top state without curve reload) and the WDR enable
 * byte (+0x2284c).  Parameter byte +214 is the "top" state.
 *
 * tisp_lce_get_show_data, tisp_lce_param_array_get and
 * tisp_lce_param_array_set (appended): the API view of the state.  The
 * show data (state +4 .. +35, two compressed curves at +36 / +100) come
 * from the day block (+0x22736 == 0: curves at +0x224fc / +0x2257c) or the
 * selected zone (+0x2273a row, +0x2273b column, 64-byte curves from
 * +0x99a4).  get returns 1451 bytes: 712 parameter bytes, 160 state bytes
 * (+4), 576 bytes (+0x21f64) and 3 bytes (+0x223e4).  set takes 712
 * parameter bytes, and on a change of the WDR byte (+0x22737, kept as a
 * flag in t32_data_e20d8) reloads the curve and the Gauss weights.
 * lce_compress_data(_part_1), tisp_lce_gauss_wgt and
 * tisp_lce_write_ctrl_reg are hooks as well.
 *
 * tisp_lce_par_to_used and tisp_lce_curve_init_default (ported in
 * tx_isp_t32_lce.h, checked by t32_lce_oracle) are hooks here.  The vendor
 * functions without a result (top_change_state, get_hist_data,
 * get_sum_data) leave registers in v0; ours return nothing.
 * Checked against Matt Davis' recovered C (PR #15 @ 2e2e06bc) by
 * tests/t32_oracle (t32_lcem_oracle).
 */
#ifndef TX_ISP_T32_LCEM_H
#define TX_ISP_T32_LCEM_H

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

#define T32_LCEM_PAR_OFF	0x8494		/* parameter section in the tuning block */
#define T32_LCEM_HIST		164		/* u16 zone histograms */
#define T32_LCEM_SUM		0x90a4		/* u32 zone sums, 18 rows of 32 */
#define T32_LCEM_MODE		0x22830
#define T32_LCEM_WDR_EN		0x2284c
#define T32_LCEM_PAR_TOP	214

#define T32_LCEM_MODE_WDR	1
#define T32_LCEM_MODE_REFRESH	3
#define T32_LCEM_MODE_TOP	4

#define T32_LCEM_ROWS		18
#define T32_LCEM_SUM_COLS	32
#define T32_LCEM_SUM_SRC	2048		/* sums in the statistics buffer */
#define T32_LCEM_SUM_STRIDE	2176		/* bytes per statistics row */

struct t32_lcem_ops {
	void (*par_to_used)(u32 ch);		/* tisp_lce_par_to_used */
	u32 (*curve_init_default)(u32 ch);	/* tisp_lce_curve_init_default */
	void (*compress_part1)(const u8 *src, u8 *dst);			/* lce_compress_data_part_1 */
	void (*compress)(u32 n, const u8 *src, u8 *dst);		/* lce_compress_data */
	void (*gauss_wgt)(u32 ch, const u8 *src, u8 *dst, u32 n);	/* tisp_lce_gauss_wgt */
	void (*write_ctrl_reg)(u32 ch);					/* tisp_lce_write_ctrl_reg */
	void (*interp_strength)(u32 ch);				/* tisp_lce_tgain_interp_strength */
};

struct t32_lcem {
	const struct t32_lcem_ops *ops;
	u8 *st[4];				/* t32_data_e1da8 */
	u32 *tuning;				/* t32_data_dd0a4 */
	u8 *wdr_flag;				/* t32_data_e20d8 */
};

static inline u32 t32_lcem_rd32(const u8 *p) { u32 v; memcpy(&v, p, 4); return v; }
static inline void t32_lcem_wr32(u8 *p, u32 v) { memcpy(p, &v, 4); }
static inline u16 t32_lcem_rd16(const u8 *p) { u16 v; memcpy(&v, p, 2); return v; }
static inline void t32_lcem_wr16(u8 *p, u16 v) { memcpy(p, &v, 2); }

/* the parameter section in use (state word 0, a 32-bit address) */
static inline const u8 *t32_lcem_par(const u8 *st)
{
	return (const u8 *)(uintptr_t)t32_lcem_rd32(st);
}

/* tisp_lce_wdr_en */
static inline u32 t32_lce_wdr_en(struct t32_lcem *l, u32 ch, u32 en)
{
	u8 *st = l->st[ch];

	st[T32_LCEM_WDR_EN] = en;
	st[T32_LCEM_MODE] = T32_LCEM_MODE_WDR;
	l->ops->par_to_used(ch);
	return 0;
}

/* tisp_lce_top_change_state: reload the default curve when the top state is 1 */
static inline void t32_lce_top_change_state(struct t32_lcem *l, u32 ch)
{
	u8 *st = l->st[ch];

	if (t32_lcem_par(st)[T32_LCEM_PAR_TOP] == 1) {
		st[T32_LCEM_MODE] = T32_LCEM_MODE_REFRESH;
		l->ops->curve_init_default(ch);
	} else {
		st[T32_LCEM_MODE] = T32_LCEM_MODE_TOP;
	}
}

/* tisp_lce_dn_params_refresh: day/night switch, take the parameter section again */
static inline u32 t32_lce_dn_params_refresh(struct t32_lcem *l, u32 ch)
{
	u8 *st = l->st[ch];

	t32_lcem_wr32(st, l->tuning[ch] + T32_LCEM_PAR_OFF);
	st[T32_LCEM_MODE] = T32_LCEM_MODE_REFRESH;
	l->ops->par_to_used(ch);
	l->ops->curve_init_default(ch);
	return 0;
}

/*
 * tisp_lce_get_hist_data: 13-bit histogram halves of the statistics words.
 * 8 column groups x 18 rows x 4 pairs x 16 bins; statistics word
 * ((8 row + 144 grp + 2 pair) * 32 + 4 bin) gives the low half to
 * +164 and the high half to +228 of the state slot
 * 2 pair + 2 (1024 row + 4 grp) + 128 bin.
 */
static inline void t32_lce_get_hist_data(struct t32_lcem *l, u32 ch, const u8 *stats)
{
	u8 *st = l->st[ch];
	u32 grp, row, pair, bin;

	for (grp = 0; grp < 8; grp++)
		for (row = 0; row < T32_LCEM_ROWS; row++)
			for (pair = 0; pair < 4; pair++)
				for (bin = 0; bin < 16; bin++) {
					const u8 *s = stats + ((8 * row + 144 * grp + 2 * pair) << 5) + 4 * bin;
					u8 *d = st + 2 * pair + 2 * (1024 * row + 4 * grp) + 128 * bin;

					t32_lcem_wr16(d + T32_LCEM_HIST, t32_lcem_rd32(s) & 0x1fff);
					t32_lcem_wr16(d + T32_LCEM_HIST + 64, t32_lcem_rd16(s + 2) & 0x1fff);
				}
}

/* tisp_lce_get_sum_data: 24-bit zone sums, 18 rows of 32 */
static inline void t32_lce_get_sum_data(struct t32_lcem *l, u32 ch, const u8 *stats)
{
	u8 *st = l->st[ch];
	u32 row, col;

	for (row = 0; row < T32_LCEM_ROWS; row++)
		for (col = 0; col < T32_LCEM_SUM_COLS; col++)
			t32_lcem_wr32(st + T32_LCEM_SUM + 128 * row + 4 * col,
				      t32_lcem_rd32(stats + T32_LCEM_SUM_SRC + T32_LCEM_SUM_STRIDE * row + 4 * col)
				      & 0xffffff);
}

#define T32_LCEM_PAR_BYTES	712
#define T32_LCEM_API_BYTES	1451
#define T32_LCEM_SHOW_MODE	0x22736
#define T32_LCEM_SHOW_ROW	0x2273a
#define T32_LCEM_SHOW_COL	0x2273b
#define T32_LCEM_WDR		0x22737
#define T32_LCEM_DAY_SRC	0x22604		/* 12 + 12 show bytes, day */
#define T32_LCEM_DAY_PAIR	0x225fc		/* 4 + 4 show bytes, day (2-byte stride) */
#define T32_LCEM_NIGHT_SRC	0x226ac
#define T32_LCEM_NIGHT_PAIR	0x226a4
#define T32_LCEM_DAY_CURVE	0x224fc		/* two 128-byte curves */
#define T32_LCEM_ZONE_CURVES	0x99a4		/* 64 bytes per zone, 32 zones per row */
#define T32_LCEM_ZONE_CUR	0x22624		/* the selected zone's two curves */
#define T32_LCEM_GAUSS		0x226cc		/* Gauss parameter block (state + 0x20000 + 9932) */
#define T32_LCEM_LIGHT		0x22822		/* 12 light bytes from parameter u16 +48 .. +70 */
#define T32_LCEM_DAYNIGHT	0x22734

/* the 4 pair bytes and 12 + 12 bytes of the show data (state +4 .. +35) */
static inline void t32_lcem_show_bytes(u8 *st, u32 pair, u32 src)
{
	u32 i;

	for (i = 0; i < 4; i++) {
		st[4 + i] = st[pair + 2 * i];
		st[20 + i] = st[pair + 2 * i + 20];
	}
	for (i = 0; i < 12; i++) {
		st[8 + i] = st[src + i];
		st[24 + i] = st[src + i + 20];
	}
}

/* tisp_lce_get_show_data */
static inline void t32_lce_get_show_data(struct t32_lcem *l, u32 ch)
{
	u8 *st = l->st[ch];

	if (!st[T32_LCEM_SHOW_MODE]) {
		t32_lcem_show_bytes(st, T32_LCEM_DAY_PAIR, T32_LCEM_DAY_SRC);
		l->ops->compress_part1(st + T32_LCEM_DAY_CURVE, st + 36);
		l->ops->compress_part1(st + T32_LCEM_DAY_CURVE + 128, st + 100);
	} else {
		u32 zone = (st[T32_LCEM_SHOW_ROW] << 5) + st[T32_LCEM_SHOW_COL];

		t32_lcem_show_bytes(st, T32_LCEM_NIGHT_PAIR, T32_LCEM_NIGHT_SRC);
		memcpy(st + T32_LCEM_ZONE_CUR, st + T32_LCEM_ZONE_CURVES + (zone << 6), 64);
		l->ops->compress(2, st + T32_LCEM_ZONE_CUR, st + 36);
		l->ops->compress(2, st + T32_LCEM_ZONE_CUR + 64, st + 100);
	}
}

/* tisp_lce_param_array_get */
static inline u32 t32_lce_param_array_get(struct t32_lcem *l, u32 ch, u8 *out, u32 *len)
{
	u8 *st = l->st[ch];

	memcpy(out, t32_lcem_par(st), T32_LCEM_PAR_BYTES);
	memcpy(out + 712, st + 4, 160);
	t32_lce_get_show_data(l, ch);
	memcpy(out + 872, st + 0x21f64, 576);
	memcpy(out + 1448, st + 0x223e4, 3);
	*len = T32_LCEM_API_BYTES;
	return 0;
}

/* tisp_lce_param_array_set */
static inline u32 t32_lce_param_array_set(struct t32_lcem *l, u32 ch, const u8 *in)
{
	u8 *st = l->st[ch];
	u8 *par = (u8 *)t32_lcem_par(st);
	u8 *g = st + T32_LCEM_GAUSS;
	u8 wdr;

	memcpy(par, in, T32_LCEM_PAR_BYTES);
	l->ops->par_to_used(ch);
	wdr = st[T32_LCEM_WDR];
	if (*l->wdr_flag == wdr) {
		l->ops->write_ctrl_reg(ch);
	} else {
		*l->wdr_flag = wdr != 0;
		l->ops->curve_init_default(ch);
		if (*l->wdr_flag) {
			static const struct { u16 n, dst, mode; } gw[4] = {
				{ 3, 299, 3 }, { 3, 319, 4 }, { 3, 244, 1 }, { 3, 264, 2 },
			};
			u32 i, k;

			for (i = 0; i < 4; i++) {
				for (k = 0; k < gw[i].n; k++)
					st[T32_LCEM_LIGHT + 3 * i + k] = t32_lcem_rd16(par + 48 + 2 * (3 * i + k));
				l->ops->gauss_wgt(ch, g + 342 + 3 * i, g + gw[i].dst, gw[i].mode);
			}
		} else {
			l->ops->gauss_wgt(ch, par + 435, g + 161, 1);
			memcpy(g + 155, par + 435, 3);
			l->ops->gauss_wgt(ch, par + 438, g + 181, 2);
			memcpy(g + 158, par + 438, 3);
		}
	}
	st[T32_LCEM_MODE] = st[T32_LCEM_DAYNIGHT] ? 2 : 4;
	return 0;
}

#define T32_LCEM_LAST_GAIN	0x22838
#define T32_LCEM_LAST_EV	0x2283c
#define T32_LCEM_PAR_MODE	507		/* 0 = follow the gain, 1 = follow the EV */

/* |v - last| as the vendor computes it from a 64-bit value (lo, hi): a set high word counts as "above" */
static inline u32 t32_lcem_delta(u32 lo, u32 hi, u32 last)
{
	return (hi || lo >= last) ? lo - last : last - lo;
}

/* tisp_lce_tgain_update: redo the strength when the gain (>> 7) or the EV moved */
static inline u32 t32_lce_tgain_update(struct t32_lcem *l, u32 ch, u32 gain_lo, u32 gain_hi, u32 ev_lo, u32 ev_hi)
{
	u8 *st = l->st[ch];
	u32 mode = t32_lcem_par(st)[T32_LCEM_PAR_MODE];
	int dg = (int)t32_lcem_delta(gain_lo, gain_hi, t32_lcem_rd32(st + T32_LCEM_LAST_GAIN)) >> 7;
	int de = (int)t32_lcem_delta(ev_lo, ev_hi, t32_lcem_rd32(st + T32_LCEM_LAST_EV));

	if (dg > 0 && mode == 0) {
		t32_lcem_wr32(st + T32_LCEM_LAST_GAIN, gain_lo);
	} else {
		if (de <= 0 || mode != 1)
			return 0;
		t32_lcem_wr32(st + T32_LCEM_LAST_EV, ev_lo);
	}
	l->ops->interp_strength(ch);
	return 0;
}

#endif
