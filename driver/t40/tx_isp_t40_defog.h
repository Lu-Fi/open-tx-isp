/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T40 defog (dehaze, TOP bit 11) in plain C.
 *
 * Rebuilt from the flow of the OEM tisp_defog_* / Tiziano_defog_soft
 * functions (tiziano-defog.c in the sc830ai tx-isp-t40.ko):
 *
 *  - init: the frame is split into 10 x 18 statistics cells (cell height
 *    2*((height/2)/10), width 2*((width/2)/18), the first rem rows/columns
 *    are one cell row/column larger); the reciprocals of the four cell
 *    sizes scale the statistics.  The 812-byte defog section of the bank
 *    (+0x10958) is the parameter block ("tool"); the position weights
 *    (0xc020..0xc12c) come from the bank or, for another resolution or zoom,
 *    from count_weight.
 *  - statistics IRQ 10: 180 cells x three 21-bit sums are read from the
 *    statistics DMA buffer (4 slots), event 4 runs the process step.
 *  - process (per frame): the three sums -> per-cell values -> luma; per
 *    cell the three "air light" outputs follow the 17-point curve, the
 *    fourth (transmission) output follows the 5-knot curve whose levels are
 *    interpolated over the exposure (EV); optional motion weighting from the
 *    frame-to-frame luma change, 5x5 smoothing; the four 180-byte outputs go
 *    to the 0x500a0/0x500a4 table.
 *  - EV change / IMP strength (tisp_s_defog_ratio): the transmission levels
 *    are re-interpolated, cached values dropped.
 *
 * This replaces the literal mips2c chain (tx_isp_t40_defog_lit.inc up to
 * open-tx-isp next 1bc9b8e2); tests/t40/c_chains_host compares both.
 * Only the main channel (sensor 0) exists here.  OEM behaviour kept on
 * purpose (bit-exact): the curve cache (flag bit 0) is never invalidated
 * after a day/night refresh, the EV/strength path drops only the first 180
 * of the 256 transmission cache entries, the motion "variance" is the
 * squared deviation of the last cell of the window.
 */
#ifndef TX_ISP_T40_DEFOG_H
#define TX_ISP_T40_DEFOG_H

#define T40_DFG_TOOL_OFFSET	0x10958U	/* defog section inside a tuning bank */
#define T40_DFG_TOOL_BYTES	812U
#define T40_DFG_ROWS		10U
#define T40_DFG_COLS		18U
#define T40_DFG_CELLS		(T40_DFG_ROWS * T40_DFG_COLS)
#define T40_DFG_STAT_SLOT	2048U		/* statistics DMA slot (4 slots) */

/* parameter block (OEM defog_main_tool_para) offsets */
#define DFG_T_CTL	0	/* [10] see struct t40_dfg_soft.ctl */
#define DFG_T_CURVE	10	/* [17] air light curve (by value / 16) */
#define DFG_T_K9	27	/* [9] 3x3 weight smoothing kernel */
#define DFG_T_K25	36	/* [25] 5x5 output smoothing kernel */
#define DFG_T_EV	64	/* u32 EV the levels were interpolated for */
#define DFG_T_KNOTS	68	/* u32[9] EV knots */
#define DFG_T_LEVELS	104	/* [5][9] transmission levels per EV knot */
#define DFG_T_YKNOTS	149	/* [5] luma knots of the transmission curve */
#define DFG_T_CUR	154	/* [5] levels at the current EV */
#define DFG_T_C010A	159	/* 0xc010 bits 16.. */
#define DFG_T_C010B	160	/* 0xc010 bits 8.. */
#define DFG_T_LUT_C130	161	/* [32] */
#define DFG_T_LUT_C150	193	/* [32] */
#define DFG_T_C010C	225	/* 0xc010 bits 0.. */
#define DFG_T_LUT_C194	226	/* [32] */
#define DFG_T_C22C_G	258	/* gain of the 0xc22c table */
#define DFG_T_C22C	259	/* [32] */
#define DFG_T_C170_G0	291	/* gains of the two 0xc170 tables */
#define DFG_T_C170_A	292	/* [17] */
#define DFG_T_C170_G1	309
#define DFG_T_C170_B	310	/* [17] */
#define DFG_T_C1B4_G	327	/* [6] gains of the six 0xc1b4 tables */
#define DFG_T_C1B4	333	/* [6][17] */
#define DFG_T_ZOOMK	435	/* position weight zoom (128 = bank weights) */
#define DFG_T_W24	436	/* u16[24] 0xc020.. (13 bit) */
#define DFG_T_W31	484	/* u16[31] 0xc050.. (14 bit) */
#define DFG_T_W5X32	546	/* [5][32] 0xc090.. (5 bit) */
#define DFG_T_MANUAL	706	/* levels fixed, EV ignored */
#define DFG_T_C2A0	707
#define DFG_T_WH	708	/* u16 width, height (LE) the weights were made for */
#define DFG_T_WDR_KNOTS	728	/* u32[9] */
#define DFG_T_WDR_LEVELS 764	/* [5][9] */
#define DFG_T_WEIGHTS_END 706	/* 436..705: everything count_weight makes */

/* soft state (OEM defog_main_soft_para, 3948 bytes, same layout) */
struct t40_dfg_soft {
	uint32_t stat[3][T40_DFG_CELLS];	/*    0 cell sums: bits 42..62, 21..41, 0..20 */
	uint32_t recip[4];			/* 2160 2^31 / cell area: big/big, big/small rows, small/big, small/small */
	uint8_t prev_y[T40_DFG_CELLS];		/* 2176 last luma per cell */
	uint8_t curve[17];			/* 2356 tool copies */
	uint8_t k9[9];				/* 2373 */
	uint8_t k25[25];			/* 2382 */
	/* ctl: [0] simple luma mode, [1] one curve for all channels, [2]/[3] curve
	 * output min/max, [4] simple mode gain, [5] motion weighting (0 for the
	 * first two frames), [6] motion offset, [7]/[8] motion low/high
	 * threshold, [9] output smoothing */
	uint8_t ctl[10];			/* 2407 */
	uint8_t pad2417[39];			/* 2417 (OEM 2448/2452: pointers to tool+149/+154) */
	uint8_t rem_h, rem_w, prec;		/* 2456 */
	uint8_t out[4][T40_DFG_CELLS];		/* 2459 three air light outputs + transmission */
	uint8_t flags[256];			/* 3179 by value: bit 0 curve cached, bit 4 transmission cached */
	uint8_t cache_curve[256];		/* 3435 */
	uint8_t cache_trans[256];		/* 3691 */
	uint8_t pad3947;
};

/* register image of tisp_defog_write_reg_para2 (OEM defog_main_reg2_para) */
struct t40_dfg_reg2 {
	uint8_t c010[4];			/*   0 */
	uint32_t c170[17];			/*   4 */
	uint8_t c22c[32];			/*  72 */
	uint8_t c1b4[6][17];			/* 104 */
	uint8_t pad206[2];
};

struct t40_dfg_ctx {
	uint8_t tool[T40_DFG_TOOL_BYTES];
	struct t40_dfg_soft soft;
	struct t40_dfg_reg2 reg2;
	int16_t dy[T40_DFG_CELLS];		/* luma change against the last frame */
	uint8_t motion[T40_DFG_CELLS];		/* motion weight (255 = moving) */
	uint8_t wdr_sel;			/* tables chosen at the last bank load */
};

static struct t40_dfg_ctx *t40_dfg T40C2;
/* OEM .data/.bss words that outlive init (defog_ev starts at 99, the strength at 128) */
static uint32_t t40_dfg_ev T40C2 = 0x63;
static uint32_t t40_dfg_frames T40C2;
static uint32_t t40_dfg_wdr T40C2;	/* OEM defog_wdr_en: nothing sets it on this driver */
static uint8_t t40_dfg_zoomk T40C2;
static uint8_t t40_dfg_strength T40C2 = 0x80;

static const uint16_t t40_dfg_w24_base[24] = {
	255, 521, 799, 1090, 1395, 1717, 2057, 2417, 2799, 3207, 3643, 4114,
	4624, 5180, 5792, 6471, 7237, 8111, 9133, 10359, 11896, 13953, 17075, 23792
};
static const uint16_t t40_dfg_dist_base[63] = {
	133, 269, 407, 547, 689, 834, 982, 1132, 1285, 1440, 1599, 1761, 1926, 2094, 2266, 2442,
	2621, 2804, 2991, 3183, 3379, 3580, 3785, 3996, 4213, 4435, 4663, 4898, 5140, 5389, 5645, 5910,
	6183, 6465, 6758, 7061, 7375, 7702, 8042, 8397, 8767, 9155, 9562, 9989, 10440, 10916, 11421, 11959,
	12534, 13152, 13819, 14544, 15338, 16216, 17197, 18130, 19595, 21114, 22974, 25371, 28750, 34526, 40368
};

static inline uint32_t t40_dfg_u32(const uint8_t *t, unsigned int o)
{
	return t[o] | t[o + 1] << 8 | t[o + 2] << 16 | (uint32_t)t[o + 3] << 24;
}
static inline void t40_dfg_put32(uint8_t *t, unsigned int o, uint32_t v)
{
	t[o] = v; t[o + 1] = v >> 8; t[o + 2] = v >> 16; t[o + 3] = v >> 24;
}
static inline uint16_t t40_dfg_u16(const uint8_t *t, unsigned int o)
{
	return t[o] | t[o + 1] << 8;
}
static inline void t40_dfg_put16(uint8_t *t, unsigned int o, uint16_t v)
{
	t[o] = v; t[o + 1] = v >> 8;
}

/* EV knots and level tables, chosen at each bank load (OEM tisp_defog_wdr_choose_para) */
static inline unsigned int t40_dfg_knots(const struct t40_dfg_ctx *c)
{
	return c->wdr_sel ? DFG_T_WDR_KNOTS : DFG_T_KNOTS;
}
static inline unsigned int t40_dfg_levels(const struct t40_dfg_ctx *c)
{
	return c->wdr_sel ? DFG_T_WDR_LEVELS : DFG_T_LEVELS;
}

/* levels at the given EV, linear between the two knots around it (OEM tisp_defog_interp_trans_k) */
static void t40_dfg_interp_levels(struct t40_dfg_ctx *c, uint32_t ev)
{
	uint8_t *tool = c->tool;
	unsigned int kn = t40_dfg_knots(c), lv = t40_dfg_levels(c), i, j;
	uint8_t *out = tool + DFG_T_CUR;

	for (i = 0; i < 9; i++)
		if (ev < t40_dfg_u32(tool, kn + 4 * i))
			break;
	for (j = 0; j < 5; j++) {
		const uint8_t *l = tool + lv + 9 * j;

		if (i == 0 || i == 9) {
			out[j] = l[i ? 8 : 0];
		} else {
			uint32_t d1 = ev - t40_dfg_u32(tool, kn + 4 * (i - 1));
			uint32_t d0 = t40_dfg_u32(tool, kn + 4 * i) - ev, s = d0 + d1;

			out[j] = (l[i - 1] * d0 + l[i] * d1 + (s >> 1)) / s;
		}
	}
}

/* new levels for the EV / strength; drops the cached transmission values when they changed */
static void t40_dfg_relevel(struct t40_dfg_ctx *c, uint32_t ev)
{
	uint8_t old[5];
	unsigned int i;

	memcpy(old, c->tool + DFG_T_CUR, 5);
	t40_dfg_interp_levels(c, ev);
	if (memcmp(old, c->tool + DFG_T_CUR, 5))
		for (i = 0; i < T40_DFG_CELLS; i++)	/* OEM: 180 of the 256 entries */
			c->soft.flags[i] &= 0x0f;
}

/* OEM tisp_defog_update_trans_k */
static void t40_dfg_update_levels(struct t40_dfg_ctx *c)
{
	if (c->tool[DFG_T_MANUAL] || t40_dfg_u32(c->tool, DFG_T_EV) == t40_dfg_ev)
		return;
	t40_dfg_put32(c->tool, DFG_T_EV, t40_dfg_ev);
	t40_dfg_relevel(c, t40_dfg_ev);
}

/* pack four bytes, each masked */
static inline uint32_t t40_dfg_pack4(const uint8_t *p, uint32_t m)
{
	return (p[0] & m) | (p[1] & m) << 8 | (p[2] & m) << 16 | (uint32_t)(p[3] & m) << 24;
}

/* OEM tisp_defog_write_reg_para1: position weights */
static void t40_dfg_write_weights(const uint8_t *t)
{
	unsigned int i;

	for (i = 0; i < 12; i++)
		system_reg_write(0xc020 + 4 * i, (t40_dfg_u16(t, DFG_T_W24 + 4 * i) & 0x1fff) |
				 (t40_dfg_u16(t, DFG_T_W24 + 4 * i + 2) & 0x1fff) << 16);
	for (i = 0; i < 15; i++)
		system_reg_write(0xc050 + 4 * i, (t40_dfg_u16(t, DFG_T_W31 + 4 * i) & 0x3fff) |
				 (t40_dfg_u16(t, DFG_T_W31 + 4 * i + 2) & 0x3fff) << 16);
	system_reg_write(0xc08c, t40_dfg_u16(t, DFG_T_W31 + 60) & 0x3fff);
	for (i = 0; i < 40; i++)
		system_reg_write(0xc090 + 4 * i, t40_dfg_pack4(t + DFG_T_W5X32 + 4 * i, 0x1f));
}

/* one gain-scaled table byte (OEM: below 65 scale up from 0, above towards max) */
static inline uint32_t t40_dfg_gain(uint32_t g, uint32_t v, uint32_t max)
{
	if (g < 65)
		return (int32_t)(g * v) >> 6;
	return max - ((int32_t)((max - v) * (128 - g)) >> 6);
}

/* OEM tisp_defog_update_reg_para2: register image + soft copies */
static void t40_dfg_update_reg2(struct t40_dfg_ctx *c)
{
	const uint8_t *t = c->tool;
	struct t40_dfg_reg2 *r = &c->reg2;
	struct t40_dfg_soft *s = &c->soft;
	unsigned int i, g;

	for (i = 0; i < 17; i++)
		r->c170[i] = (t40_dfg_gain(t[DFG_T_C170_G1], t[DFG_T_C170_B + i], 127) << 7 & 0x3f80) |
			     (t40_dfg_gain(t[DFG_T_C170_G0], t[DFG_T_C170_A + i], 127) & 0x7f);
	for (i = 0; i < 17; i++)
		for (g = 0; g < 6; g++)
			r->c1b4[g][i] = t40_dfg_gain(t[DFG_T_C1B4_G + g], t[DFG_T_C1B4 + 17 * g + i], 63) & 0x3f;
	for (i = 0; i < 32; i++)
		r->c22c[i] = (t[DFG_T_C22C + i] * t[DFG_T_C22C_G]) >> 7 & 0x7f;
	r->c010[0] = t[DFG_T_CTL];
	r->c010[1] = t[DFG_T_C010A];
	r->c010[2] = t[DFG_T_C010B];
	r->c010[3] = t[DFG_T_C010C];
	memcpy(s->ctl, t + DFG_T_CTL, 10);
	memcpy(s->k9, t + DFG_T_K9, 9);
	memcpy(s->k25, t + DFG_T_K25, 25);
	memcpy(s->curve, t + DFG_T_CURVE, 17);
}

/* OEM tisp_defog_write_reg_para2 */
static void t40_dfg_write_reg2(const struct t40_dfg_ctx *c)
{
	const struct t40_dfg_reg2 *r = &c->reg2;
	const uint8_t *t = c->tool;
	unsigned int i, g;

	system_reg_write(0xc010, (r->c010[3] & 0x3f) | (r->c010[2] & 0x3f) << 8 |
			 (r->c010[1] & 0x3f) << 16 | (r->c010[0] & 1) << 24);
	for (i = 0; i < 8; i++)
		system_reg_write(0xc130 + 4 * i, t40_dfg_pack4(t + DFG_T_LUT_C130 + 4 * i, 0x7f));
	for (i = 0; i < 8; i++)
		system_reg_write(0xc150 + 4 * i, t40_dfg_pack4(t + DFG_T_LUT_C150 + 4 * i, 0x7f));
	for (i = 0; i < 8; i++)
		system_reg_write(0xc170 + 4 * i, (r->c170[2 * i] & 0x3fff) | (r->c170[2 * i + 1] & 0x3fff) << 16);
	system_reg_write(0xc190, r->c170[16] & 0x3fff);
	for (i = 0; i < 8; i++)
		system_reg_write(0xc194 + 4 * i, t40_dfg_pack4(t + DFG_T_LUT_C194 + 4 * i, 0x7f));
	for (g = 0; g < 6; g++) {
		for (i = 0; i < 4; i++)
			system_reg_write(0xc1b4 + 20 * g + 4 * i, t40_dfg_pack4(&r->c1b4[g][4 * i], 0x3f));
		system_reg_write(0xc1b4 + 20 * g + 16, r->c1b4[g][16] & 0x3f);
	}
	for (i = 0; i < 8; i++)
		system_reg_write(0xc22c + 4 * i, t40_dfg_pack4(&r->c22c[4 * i], 0x7f));
	system_reg_write(0xc2a0, t[DFG_T_C2A0] & 0x1f);
	system_reg_write(0xc004, 0xf);
}

/* OEM tisp_defog_write_reg_para3: the four outputs per cell into the 0x500a4 table */
static void t40_dfg_write_table(const struct t40_dfg_soft *s)
{
	unsigned int i;

	system_reg_write(0xc2ac, 0);
	system_reg_write(0x500a0, 0x101);
	for (i = 0; i < T40_DFG_CELLS; i++)
		system_reg_write(0x500a4, s->out[0][i] | s->out[1][i] << 8 | s->out[2][i] << 16 |
				 (uint32_t)s->out[3][i] << 24);
	system_reg_write(0x500a0, 0xb30102);
	system_reg_write(0xc2ac, 1);
}

/* weighted mean over the window of in[] around each cell (OEM img_filter9 / img_filter25):
 * radius 1 or 2, the window clipped at the edges; result (sum + w/2) / w */
static void t40_dfg_smooth(uint8_t *io, const uint8_t *k, int rad)
{
	uint8_t tmp[T40_DFG_CELLS];
	int r, cc, dy, dx, n = 2 * rad + 1;

	for (r = 0; r < (int)T40_DFG_ROWS; r++)
		for (cc = 0; cc < (int)T40_DFG_COLS; cc++) {
			uint32_t w = 0, sum = 0;

			for (dy = -rad; dy <= rad; dy++) {
				if (r + dy < 0 || r + dy >= (int)T40_DFG_ROWS)
					continue;
				for (dx = -rad; dx <= rad; dx++) {
					uint32_t kv;

					if (cc + dx < 0 || cc + dx >= (int)T40_DFG_COLS)
						continue;
					kv = k[(dy + rad) * n + dx + rad];
					w += kv;
					sum += io[(r + dy) * T40_DFG_COLS + cc + dx] * kv;
				}
			}
			tmp[r * T40_DFG_COLS + cc] = w ? ((w >> 1) + sum) / w : 0;	/* OEM: w = 0 undefined */
		}
	memcpy(io, tmp, sizeof(tmp));
}

/* 3x3 maximum (OEM tisp_defog_max_filter) */
static void t40_dfg_max3(uint8_t *io)
{
	uint8_t tmp[T40_DFG_CELLS];
	int r, cc, dy, dx;

	for (r = 0; r < (int)T40_DFG_ROWS; r++)
		for (cc = 0; cc < (int)T40_DFG_COLS; cc++) {
			uint8_t m = 0;

			for (dy = r ? -1 : 0; dy <= (r < 9 ? 1 : 0); dy++)
				for (dx = cc ? -1 : 0; dx <= (cc < 17 ? 1 : 0); dx++)
					if (io[(r + dy) * T40_DFG_COLS + cc + dx] > m)
						m = io[(r + dy) * T40_DFG_COLS + cc + dx];
			tmp[r * T40_DFG_COLS + cc] = m;
		}
	memcpy(io, tmp, sizeof(tmp));
}

/* air light curve by value, cached per value */
static uint8_t t40_dfg_curve(struct t40_dfg_soft *s, uint32_t v)
{
	uint32_t a, x;

	if (s->flags[v] & 1)
		return s->cache_curve[v];
	a = s->curve[v >> 4];
	x = a + ((int32_t)((s->curve[(v >> 4) + 1] - a) * (v & 15) + 8) >> 4);
	x = x * (uint32_t)(s->ctl[3] - s->ctl[2]) + 127;
	x = (x * 257 + ((uint32_t)s->ctl[2] << 16)) >> 16 & 0xff;
	s->cache_curve[v] = x;
	s->flags[v]++;
	return x;
}

/* transmission by luma: 5-knot curve (knots tool+149, levels tool+154), cached per luma */
static uint8_t t40_dfg_trans(struct t40_dfg_soft *s, const uint8_t *tool, int32_t y)
{
	const uint8_t *kn = tool + DFG_T_YKNOTS, *lv = tool + DFG_T_CUR;
	uint8_t v;
	int j;

	if (s->flags[y] & 0x10)
		return s->cache_trans[y];
	if (y < kn[0]) {
		v = lv[0];
	} else {
		for (j = 0; j < 4 && y >= kn[j + 1]; j++)
			;
		if (j == 4) {
			v = lv[4];
		} else {
			int32_t dk = kn[j + 1] - kn[j], dl = lv[j + 1] - lv[j];

			v = lv[j] + ((y - kn[j]) * dl + (dk >> 1)) / dk;
		}
	}
	s->cache_trans[y] = v;
	s->flags[y] += 16;
	return v;
}

/* motion weight per cell from the luma changes of the 5x5 window (OEM: inside Tiziano_defog_soft) */
static void t40_dfg_motion(struct t40_dfg_ctx *c)
{
	const struct t40_dfg_soft *s = &c->soft;
	int32_t off = s->ctl[6] << 4, lo = s->ctl[7] << 2, hi = s->ctl[8] << 6;
	int r, cc, y, x;

	for (r = 0; r < (int)T40_DFG_ROWS; r++)
		for (cc = 0; cc < (int)T40_DFG_COLS; cc++) {
			int y0 = r < 2 ? 0 : r - 2, y1 = r > 7 ? 9 : r + 2;
			int x0 = cc < 2 ? 0 : cc - 2, x1 = cc > 15 ? 17 : cc + 2;
			int32_t n = (y1 - y0 + 1) * (x1 - x0 + 1), sum = 0, asum = 0, mean, amean, dev, v;
			uint8_t w;

			for (y = y0; y <= y1; y++)
				for (x = x0; x <= x1; x++) {
					int32_t d = c->dy[y * T40_DFG_COLS + x];

					sum += d;
					asum += d > 0 ? d : -d;
				}
			mean = sum / n;
			amean = (asum << 6) / n;
			/* OEM: the "variance" keeps only the last cell of the window */
			dev = c->dy[y1 * T40_DFG_COLS + x1] - mean;
			dev *= dev;
			/* 32-bit product as in the OEM; off = 0 is undefined there */
			v = off ? (int32_t)((uint32_t)(dev / n + off) * (uint32_t)amean) / off : 0;
			if (v > hi)
				w = 255;
			else if (v < lo)
				w = 0;
			else
				w = hi > lo ? ((v - lo) << 8) / (hi - lo) : 0;	/* OEM: hi = lo undefined */
			c->motion[r * T40_DFG_COLS + cc] = w;
		}
}

/* OEM Tiziano_defog_soft: one frame of statistics -> the four output tables */
static void t40_dfg_soft(struct t40_dfg_ctx *c)
{
	struct t40_dfg_soft *s = &c->soft;
	int sh = 31 - s->prec;
	unsigned int r, cc, i, k;

	for (r = 0; r < T40_DFG_ROWS; r++)
		for (cc = 0; cc < T40_DFG_COLS; cc++) {
			int32_t rc = (int32_t)s->recip[(r < s->rem_h ? 0 : 2) + (cc < s->rem_w ? 0 : 1)];
			int32_t rnd = (rc >> (30 - s->prec)) & 1, v[3], y;

			k = r * T40_DFG_COLS + cc;
			for (i = 0; i < 3; i++) {
				v[i] = (int32_t)(((int64_t)s->stat[i][k] * rc) >> sh) + rnd;
				if (v[i] >= 256)
					v[i] = 255;
			}
			y = (19 * v[0] + 38 * v[1] + 7 * v[2]) >> 6;
			if (y >= 256)
				y = 255;
			c->dy[k] = y - s->prev_y[k];
			s->prev_y[k] = y;
			if (s->ctl[0]) {
				/* simple luma mode: dark and bright ends */
				int32_t e = y < 128 ? y : 255 - y, g = s->ctl[4];
				int32_t f = 2 * e - ((e * e * e) >> 13);

				s->out[0][k] = (y < 128 ? 0 : f * g) >> 7;
				s->out[1][k] = (y < 128 ? f * g : 0) >> 7;
				s->out[3][k] = (e * g) >> 6;
				continue;
			}
			if (s->ctl[1]) {
				s->out[0][k] = s->out[1][k] = s->out[2][k] = t40_dfg_curve(s, y);
			} else {
				for (i = 0; i < 3; i++)
					s->out[i][k] = t40_dfg_curve(s, v[i]);
			}
			s->out[3][k] = t40_dfg_trans(s, c->tool, y);
		}
	if (s->ctl[5] == 1) {
		t40_dfg_motion(c);
		t40_dfg_max3(c->motion);
		t40_dfg_smooth(c->motion, s->k9, 1);
		for (k = 0; k < T40_DFG_CELLS; k++)
			if (c->motion[k])
				s->out[3][k] = ((256 - c->motion[k]) * s->out[3][k]) >> 8;
	}
	if (s->ctl[9] == 1) {
		t40_dfg_smooth(s->out[0], s->k25, 2);
		if (s->ctl[1]) {
			memcpy(s->out[1], s->out[0], T40_DFG_CELLS);
			memcpy(s->out[2], s->out[0], T40_DFG_CELLS);
		} else {
			t40_dfg_smooth(s->out[1], s->k25, 2);
			t40_dfg_smooth(s->out[2], s->k25, 2);
		}
	}
}

/* 5 x 32 weights: mean value per index, clipped to 31, non-decreasing (OEM defog_wei_interpcot) */
static void t40_dfg_weight_table(uint32_t n, const uint8_t *idx, const uint8_t *val, uint8_t *out)
{
	uint32_t sum[32] = { 0 }, cnt[32] = { 0 }, i, v;

	for (i = 0; i < n; i++) {
		sum[idx[i]] += val[i];	/* idx <= 31 by construction */
		cnt[idx[i]]++;
	}
	for (i = 0; i < 32; i++) {
		v = cnt[i] ? ((cnt[i] >> 1) + sum[i]) / cnt[i] & 0xff : 0;
		out[i] = v < 32 ? v : 31;
		if (i && out[i] < out[i - 1])
			out[i] = out[i - 1];
	}
}

/* position weights for a resolution and zoom (OEM defog_count_weight35abc) */
static int t40_dfg_count_weights(uint8_t *tool, uint32_t height, uint32_t width, uint32_t zoomk)
{
	uint32_t rows = (height + 5) / 10, cols = (width + 9) / 18, n = rows * cols, scale, i;
	uint16_t dist[63], w;
	uint8_t *m, *mm[8], *ix[5];
	int32_t row, col, yy, xx;

	scale = ((height * width + 1012) / 2025 * zoomk + 64) >> 7;
	for (i = 0; i < 24; i++)
		t40_dfg_put16(tool, DFG_T_W24 + 2 * i, (t40_dfg_w24_base[i] * scale + 8192) >> 14);
	for (i = 0; i < 63; i++)
		dist[i] = (t40_dfg_dist_base[i] * scale + 4096) >> 13;
	for (i = 0; i < 31; i++)
		t40_dfg_put16(tool, DFG_T_W31 + 2 * i, dist[2 * i + 1]);
	/* keep the tables inside their register fields, strictly increasing at the top */
	if (t40_dfg_u16(tool, DFG_T_W24 + 46) >> 13) {
		t40_dfg_put16(tool, DFG_T_W24 + 46, 8191);
		for (i = 22; i > 0; i--) {
			w = t40_dfg_u16(tool, DFG_T_W24 + 2 * i + 2);
			if (t40_dfg_u16(tool, DFG_T_W24 + 2 * i) < w)
				break;
			t40_dfg_put16(tool, DFG_T_W24 + 2 * i, w - 1);
		}
	}
	if (t40_dfg_u16(tool, DFG_T_W31 + 60) >> 14) {
		t40_dfg_put16(tool, DFG_T_W31 + 60, 16383);
		for (i = 29; i > 0; i--) {
			w = t40_dfg_u16(tool, DFG_T_W31 + 2 * i + 2);
			if (t40_dfg_u16(tool, DFG_T_W31 + 2 * i) < w)
				break;
			t40_dfg_put16(tool, DFG_T_W31 + 2 * i, w - 1);
		}
	}
	if (!n)
		return -EINVAL;
	m = vzalloc(13 * n);
	if (!m)
		return -ENOMEM;
	for (i = 0; i < 8; i++)
		mm[i] = m + i * n;
	for (i = 0; i < 5; i++)
		ix[i] = m + (8 + i) * n;
	/* distance class (63 - first table entry above the squared distance) of each
	 * cell to eight reference points, in units of half cells */
	for (row = 0, yy = 5 * rows - 1; row < (int32_t)rows; row++, yy -= 2) {
		int32_t y2[3] = { yy * yy, (yy - 2 * rows) * (yy - 2 * rows), (yy - 4 * rows) * (yy - 4 * rows) };

		for (col = 0, xx = 5 * cols - 1; col < (int32_t)cols; col++, xx -= 2) {
			int32_t x2[3] = { xx * xx, (xx - 2 * cols) * (xx - 2 * cols), (xx - 4 * cols) * (xx - 4 * cols) };
			static const uint8_t ref[8][2] = { { 0, 0 }, { 0, 1 }, { 0, 2 }, { 1, 0 }, { 1, 1 }, { 1, 2 }, { 2, 0 }, { 2, 1 } };

			for (i = 0; i < 8; i++) {
				uint32_t d = (y2[ref[i][0]] + x2[ref[i][1]] + 32) >> 6, j;

				for (j = 0; j < 63; j++)
					if (d < dist[j]) {
						mm[i][row * cols + col] = 63 - j;
						break;
					}
			}
		}
	}
	for (i = 0; i < n; i++) {
		ix[0][i] = mm[4][i] >> 1;
		ix[1][i] = (mm[4][i] + mm[5][i]) >> 2;
		ix[2][i] = (mm[4][i] + mm[7][i]) >> 2;
		ix[3][i] = mm[5][i] >> 1;
		ix[4][i] = mm[7][i] >> 1;
	}
	t40_dfg_weight_table(n, ix[0], mm[0], tool + DFG_T_W5X32);
	t40_dfg_weight_table(n, ix[1], mm[1], tool + DFG_T_W5X32 + 32);
	t40_dfg_weight_table(n, ix[2], mm[3], tool + DFG_T_W5X32 + 64);
	t40_dfg_weight_table(n, ix[3], mm[2], tool + DFG_T_W5X32 + 96);
	t40_dfg_weight_table(n, ix[4], mm[6], tool + DFG_T_W5X32 + 128);
	vfree(m);
	return 0;
}

/* ---- driver hooks ---- */

static bool regtrace_t40_defog_on = true;
module_param_named(t40_defog_on, regtrace_t40_defog_on, bool, 0644);
MODULE_PARM_DESC(t40_defog_on, "1 (with t40_gates_oem_offset=1) runs the defog chain and clears TOP bit 11 after a successful init (default 1)");
static bool regtrace_t40_defog_active T40C2;

/* statistics IRQ 10 (OEM tisp_defog_main_interrupt_static) */
static int32_t t40_dfg_isr(void)
{
	struct t40_dfg_ctx *c = t40_dfg;
	uint32_t at = system_reg_read(0xc2b4), phys = *(uint32_t *)(tispinfo + 64), ev[12];
	unsigned int slot, k;

	if (!c)
		return 1;
	for (slot = 0; slot < 4; slot++) {
		uint8_t *b;

		if (at != phys + slot * T40_DFG_STAT_SLOT)
			continue;
		b = (uint8_t *)(uintptr_t)*(uint32_t *)(tispinfo + 60);
		if (!b)
			continue;
		b += slot * T40_DFG_STAT_SLOT;
		dma_cache_sync(NULL, b, T40_DFG_STAT_SLOT, (enum dma_data_direction)0);
		/* 180 x 64 bit after an 8-byte header: three 21-bit sums per cell */
		for (k = 0; k < T40_DFG_CELLS; k++) {
			uint32_t lo = t40_dfg_u32(b, 8 + 8 * k), hi = t40_dfg_u32(b, 12 + 8 * k);

			c->soft.stat[2][k] = lo & 0x1fffff;
			c->soft.stat[1][k] = (hi << 11 | lo >> 21) & 0x1fffff;
			c->soft.stat[0][k] = hi >> 10 & 0x1fffff;
		}
	}
	t40_dfg_frames = t40_dfg_frames >> 30 == 1 ? 255 : t40_dfg_frames + 1;
	memset(ev, 0, sizeof(ev));
	ev[2] = 4;
	tisp_event_push(0, (uintptr_t)ev);
	return 1;
}

/* event 4 (OEM tisp_defog_main_process) */
static int32_t t40_dfg_process(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
	struct t40_dfg_ctx *c = t40_dfg;

	(void)a0; (void)a1; (void)a2; (void)a3; (void)a4;
	if (!c)
		return 0;
	t40_dfg_update_levels(c);
	/* motion weighting needs two frames of luma */
	c->soft.ctl[5] = t40_dfg_frames >= 2 ? c->tool[DFG_T_CTL + 5] : 0;
	t40_dfg_soft(c);
	t40_dfg_write_table(&c->soft);
	return 0;
}

/* registers + soft state from the parameter block (common tail of init and the day/night refresh) */
static void t40_dfg_apply(struct t40_dfg_ctx *c)
{
	struct t40_dfg_soft *s = &c->soft;

	if (!c->tool[DFG_T_MANUAL])
		t40_dfg_put32(c->tool, DFG_T_EV, t40_dfg_ev);
	t40_dfg_interp_levels(c, t40_dfg_u32(c->tool, DFG_T_EV));
	t40_dfg_update_reg2(c);
	t40_dfg_write_reg2(c);
	memset(s->out[0], 0x80, sizeof(s->out[0]) * 3);
	memset(s->out[3], 0, sizeof(s->out[3]));
	t40_dfg_write_table(s);
}

static int t40_defog_cleanup_state(void);

/* OEM tisp_main_init -> tisp_defog_init(sensor 0, width, height) */
static int t40_defog_init(uint32_t width, uint32_t height)
{
	struct t40_dfg_ctx *c;
	struct t40_dfg_soft *s;
	uint32_t ch = (height >> 1) / 10 * 2, cw = (width >> 1) / 18 * 2, a[4], i;
	int ret = -EINVAL;

	if (regtrace_t40_defog_active)
		t40_defog_cleanup_state();
	if (!ch || !cw || !tparamsN[0])
		goto out;
	ret = -ENOMEM;
	c = vzalloc(sizeof(*c));
	if (!c)
		goto out;
	t40_dfg = c;
	s = &c->soft;
	t40_dfg_frames = 0;

	a[0] = (ch + 2) * (cw + 2);
	a[1] = (ch + 2) * cw;
	a[2] = ch * (cw + 2);
	a[3] = ch * cw;
	for (i = 0; i < 4; i++)
		s->recip[i] = ((a[i] >> 1) + 0x80000000U) / a[i];
	s->rem_h = (height >> 1) % 10;
	s->rem_w = (width >> 1) % 18;
	s->prec = a[0] <= 8192 ? 0 : a[0] <= 16384 ? 1 : a[0] <= 32768 ? 2 : 3;
	system_reg_write(0xc008, (ch << 16 & 0xff0000) | (cw & 0xff));
	system_reg_write(0xc00c, (uint32_t)s->rem_h << 16 | s->rem_w);
	system_reg_write(0xc014, 0);
	system_reg_write(0xc280, 0);
	system_reg_write(0xc284, s->prec);
	system_reg_write(0xc28c, 2);
	system_reg_write(0xc004, 3);

	memcpy(c->tool, (const uint8_t *)(uintptr_t)tparamsN[0] + T40_DFG_TOOL_OFFSET, T40_DFG_TOOL_BYTES);
	c->wdr_sel = t40_dfg_wdr == 1;
	t40_dfg_zoomk = c->tool[DFG_T_ZOOMK];
	/* the bank weights fit only the resolution they were made for */
	if (t40_dfg_zoomk != 0x80 || t40_dfg_u16(c->tool, DFG_T_WH) != width ||
	    t40_dfg_u16(c->tool, DFG_T_WH + 2) != height) {
		ret = t40_dfg_count_weights(c->tool, height, width, t40_dfg_zoomk);
		if (ret)
			goto fail;
		t40_dfg_put16(c->tool, DFG_T_WH, width);
		t40_dfg_put16(c->tool, DFG_T_WH + 2, height);
	}
	t40_dfg_write_weights(c->tool);
	/* OEM: the init takes the EV of sensor 0 (same here) */
	t40_dfg_apply(c);
	system_irq_func_set(10, (int32_t)(uintptr_t)t40_dfg_isr);
	tisp_event_set_cb(0, 4, (int32_t)(uintptr_t)t40_dfg_process);
	ret = 0;
	goto out;
fail:
	t40_dfg = NULL;
	vfree(c);
out:
	regtrace_t40_defog_active = !ret;
	if (ret)	/* KERN_INFO lines of this driver are dropped (t40_printk) */
		printk(KERN_ERR "tx_isp_t40_recovered: defog init %ux%u failed (%d), TOP bit 11 stays bypassed\n",
		       width, height, ret);
	return ret;
}

/* SetModule_Ratio (defog), OEM tisp_s_defog_ratio: strength from the tuning attributes
 * (enable +864, ratio +868) scales the bank levels */
static int t40_defog_ratio_apply(struct t40_dfg_ctx *c)
{
	const uint8_t *bank = (const uint8_t *)(uintptr_t)tparamsN[0] + T40_DFG_TOOL_OFFSET;
	uint8_t *tune = (uint8_t *)(uintptr_t)tisp_get_tuning();
	/* OEM: the bank tables follow the WDR flag now, the destination the last bank load */
	unsigned int src = t40_dfg_wdr ? DFG_T_WDR_LEVELS : DFG_T_LEVELS, dst = t40_dfg_levels(c), j, i;
	uint8_t s = 0x80;

	if (tune && *(uint32_t *)(tune + 864))
		s = tune[868];
	t40_dfg_strength = s;
	for (j = 0; j < 5; j++) {
		const uint8_t *a = bank + src + 9 * j;
		uint8_t *b = c->tool + dst + 9 * j;

		for (i = 0; i < 9; i++) {
			if (s < 0x80)
				b[i] = (a[i] * s) >> 7;
			else if (s == 0x80)
				b[i] = a[i];
			else	/* above 128: towards 255 */
				b[i] = ~(((uint8_t)~a[i] * (uint8_t)~s) >> 7);
		}
	}
	if (c->tool[DFG_T_MANUAL])
		return 0;
	t40_dfg_put32(c->tool, DFG_T_EV, t40_dfg_ev);
	t40_dfg_relevel(c, t40_dfg_ev);
	return 0;
}

static int t40_defog_ratio(void)
{
	if (!regtrace_t40_defog_active || !t40_dfg || !tparamsN[0])
		return -ENODEV;
	return t40_defog_ratio_apply(t40_dfg);
}

/* OEM tisp_day_or_night_s_ctrl -> tisp_defog_dn_params_refresh(sensor, width, height) */
static int t40_defog_dn_refresh(uint32_t width, uint32_t height)
{
	struct t40_dfg_ctx *c = t40_dfg;
	uint8_t keep[DFG_T_WEIGHTS_END - DFG_T_W24];

	if (!regtrace_t40_defog_active || !c || !tparamsN[0])
		return 0;
	memcpy(keep, c->tool + DFG_T_W24, sizeof(keep));
	memcpy(c->tool, (const uint8_t *)(uintptr_t)tparamsN[0] + T40_DFG_TOOL_OFFSET, T40_DFG_TOOL_BYTES);
	c->wdr_sel = t40_dfg_wdr == 1;
	if (t40_dfg_zoomk != c->tool[DFG_T_ZOOMK]) {
		t40_dfg_zoomk = c->tool[DFG_T_ZOOMK];
		/* OEM: no failure path; on -ENOMEM the bank weights stay */
		(void)t40_dfg_count_weights(c->tool, height, width, t40_dfg_zoomk);
		t40_dfg_write_weights(c->tool);
	} else {
		memcpy(c->tool + DFG_T_W24, keep, sizeof(keep));
	}
	t40_dfg_apply(c);
	return t40_defog_ratio_apply(c);
}

/* OEM tisp_main_long_ev_update -> tisp_defog_ev_update(sensor, -, ev_lo, ev_hi): ev = IT x gain (Q10), kept >> 10 */
static void t40_defog_ev_update(uint64_t ev_q10)
{
	uint32_t ev = (uint32_t)(ev_q10 >> 32) << 22 | (uint32_t)ev_q10 >> 10;

	if (!regtrace_t40_defog_active || !t40_dfg || ev == t40_dfg_ev)
		return;
	t40_dfg_ev = ev;
	t40_dfg_update_levels(t40_dfg);
}

/* unhook IRQ 10 / event 4, free the state (module exit, re-init) */
static int t40_defog_cleanup_state(void)
{
	struct t40_dfg_ctx *c = t40_dfg;

	regtrace_t40_defog_active = false;
	system_irq_func_set(10, 0);
	tisp_event_set_cb(0, 4, 0);
	t40_dfg = NULL;
	vfree(c);
	return 0;
}

static void t40_defog_cleanup(void)
{
	if (regtrace_t40_defog_active)
		t40_defog_cleanup_state();
}

#endif
