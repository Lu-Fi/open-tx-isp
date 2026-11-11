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
};

struct t32_lcem {
	const struct t32_lcem_ops *ops;
	u8 *st[4];				/* t32_data_e1da8 */
	u32 *tuning;				/* t32_data_dd0a4 */
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

#endif
