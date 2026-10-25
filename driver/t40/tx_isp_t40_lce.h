/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T40 LCE (local contrast enhancement, TOP bit 21) in plain C.
 *
 * Rebuilt from the flow of the OEM tisp_lce_* / Tisp_lce_soft functions
 * (tiziano-lce.c in the sc830ai tx-isp-t40.ko), with the T41 C version
 * (driver/t41/tx_isp_t41_lce.h) as the model for the histogram steps:
 *
 *  - init: frame split into 5 x 9 statistics cells (cell = height/5 x
 *    2*(width/18)), the 2x2 neighbourhood areas and their reciprocals go to
 *    0xe000..0xe00c; the 984-byte LCE section of the bank (+0x10c84) is
 *    unpacked into the parameter block, identity curves are loaded.
 *  - statistics IRQ 8: the 45 x 32-bin histograms and 45 luma sums are read
 *    from the statistics RAM (ping/pong), event 0x10 runs the process step.
 *  - process (per frame): global histogram (sum of all cells) and 32 local
 *    ones (2x2 cells) -> smoothing, centre/head/tail, histogram shaping,
 *    light correction, optional WDR light lock -> CDF curves, converged
 *    towards the previous curve, local and global curve blended into the
 *    32 x 16 byte LUT written through 0x501e0/0x501e4.
 *  - total gain change: the gain-dependent strengths are re-interpolated.
 *
 * This replaces the literal mips2c chain (tx_isp_t40_lce_lit.inc up to
 * open-tx-isp next 6d63667b); tests/t40/c_chains_host compares both.
 * Only the main channel (sensor 0) exists here.
 */
#ifndef TX_ISP_T40_LCE_H
#define TX_ISP_T40_LCE_H

#include "tx_isp_t40_intp.h"

#define T40_LCE_TOOL_OFFSET 0x10c84U	/* LCE section inside a tuning bank */
#define T40_LCE_TOOL_BYTES  984U
#define T40_LCE_CELLS       45U		/* 5 rows x 9 columns */
#define T40_LCE_BINS        32U
#define T40_LCE_TILES       32U		/* 4 x 8 overlapping 2x2 neighbourhoods */
#define T40_LCE_TOTAL       0x100000	/* a normalised histogram sums to 2^20 */

/* Parameter block (OEM lce_main_used_para, 388 bytes).  p[] blocks of 82
 * bytes hold one histogram pipeline's settings (local and global). */
struct t40_lce_par {
	uint32_t pixels;		/*   0 width * height */
	uint32_t recip_all;		/*   4 2^31 / (pixels / 4) */
	uint32_t shift_all;		/*   8 12 */
	uint32_t rem_y;			/*  12 height % 5 */
	uint32_t rem_x;			/*  16 (width / 2) % 9 */
	uint32_t area[4];		/*  20 cell areas: (h+1|h) x (w+2|w) */
	uint32_t nsum[9];		/*  36 2x2 neighbourhood areas */
	uint32_t nrecip[9];		/*  72 2^30 / nsum */
	uint8_t blend[20];		/* 108 u32 global/local blend (of 128) */
	uint8_t local[82];		/* 128 local pipeline */
	uint8_t global[82];		/* 210 global pipeline */
	uint8_t weights[5];		/* 292 smoothing kernel */
	uint8_t hist_en;		/* 297 skip unchanged histograms */
	uint8_t global_thr;		/* 298 */
	uint8_t local_thr;		/* 299 */
	uint8_t pad300[2];
	uint16_t wdr[8];		/* 302 [0] WDR light lock on, [2] lock threshold, [3..7] lock knees */
	uint8_t misc[6];		/* 318 [4],[5]: debug tile y, x */
	int16_t lock[32];		/* 324 light lock per tile */
};

/* Workspace (OEM lce_main_save_para, 16088 bytes) */
struct t40_lce_work {
	uint32_t hist[T40_LCE_CELLS][T40_LCE_BINS];	/*     0 statistics copy */
	uint32_t sumy[T40_LCE_CELLS];			/*  5760 */
	uint8_t lut[T40_LCE_TILES][16];			/*  5940 hardware LUT */
	uint32_t lhist[T40_LCE_TILES][T40_LCE_BINS];	/*  6452 last used local histograms */
	uint32_t ghist[T40_LCE_BINS];			/* 10548 last used global histogram */
	uint16_t gcur[T40_LCE_BINS];			/* 10676 applied global curve */
	uint16_t gtgt[T40_LCE_BINS];			/* 10740 global target curve */
	uint16_t lcur[T40_LCE_TILES][T40_LCE_BINS];	/* 10804 applied local curves */
	uint16_t ltgt[T40_LCE_TILES][T40_LCE_BINS];	/* 12852 local target curves */
	uint8_t lhead[T40_LCE_TILES], lcenter[T40_LCE_TILES], ltail[T40_LCE_TILES];	/* 14900 */
	uint8_t ghead, gcenter, gtail, pad14999;	/* 14996 */
	uint32_t graw[T40_LCE_BINS];			/* 15000 debug copies */
	uint32_t gdbg_method[T40_LCE_BINS], gdbg_light[T40_LCE_BINS];
	uint32_t ldbg_method[T40_LCE_BINS], ldbg_light[T40_LCE_BINS], ldbg_lock[T40_LCE_BINS];
	uint32_t raw[T40_LCE_BINS];			/* 15768 */
	uint32_t flt[T40_LCE_BINS];			/* 15896 */
	int16_t lock[T40_LCE_TILES];			/* 16024 applied light lock */
};

struct t40_lce_ctx {
	uint8_t tool[T40_LCE_TOOL_BYTES] __attribute__((aligned(4)));	/* bank copy */
	struct t40_lce_par par;
	struct t40_lce_work work;
	uint32_t stat_hist[T40_LCE_CELLS][T40_LCE_BINS];	/* filled by the statistics IRQ */
	uint32_t stat_sumy[T40_LCE_CELLS];
};

/* the C layout must be the OEM one (the host check compares it) */
typedef char t40_lce_par_size[sizeof(struct t40_lce_par) == 388 ? 1 : -1];
typedef char t40_lce_work_size[sizeof(struct t40_lce_work) == 16088 ? 1 : -1];

/* T40C2: state of the C chains lives outside .bss/.data (see T40-LAYOUT in tx_isp_t40_recovered.c) */
static struct t40_lce_ctx *t40_lce T40C2;
static void t40_lce_cleanup(void);
static uint32_t t40_lce_wdr_on T40C2;		/* OEM lce_wdr_en[0] */
static uint32_t t40_lce_tgain T40C2;		/* OEM lce_tgain[0] */
static uint32_t t40_lce_refresh T40C2;	/* OEM lce_data_refresh_flag[0] */
static uint32_t t40_lce_frames T40C2;		/* OEM lce_frm_num[0] */
static uint32_t t40_lce_wdr_duan T40C2 = 0x1f;	/* OEM lce_wdr_save_duan[0] */
static uint32_t t40_lce_idle_irqs T40C2, t40_lce_dual_irqs T40C2;	/* OEM lce_debug[0..1] */

static inline int32_t t40_lce_mul(int32_t a, int32_t b)
{
	return (int32_t)((uint32_t)a * (uint32_t)b);
}

/* ---- tuning section -> parameter block ---- */

/* Tool section offsets (OEM tisp_lce_tool_to_awdr / awdr_to_used): the
 * linear half starts at 0, the WDR half at 284; 11-knot gain tables. */
#define T40_LCE_WDR_HALF 284U

static void t40_lce_load_params(struct t40_lce_ctx *c)
{
	const uint8_t *t = c->tool + (t40_lce_wdr_on ? T40_LCE_WDR_HALF : 0);
	struct t40_lce_par *p = &c->par;

	memcpy(p->blend, t + 0, 20);
	memcpy(p->local + 0, t + 20, 5);
	memcpy(p->local + 10, t + 80, 3);	/* converge ratio / min / max */
	memcpy(p->local + 13, t + 83, 5);	/* light correction */
	memcpy(p->local + 18, t + 88, 32);	/* upper ratios */
	memcpy(p->local + 50, t + 120, 32);	/* lower ratios */
	memcpy(p->global + 0, t + 152, 5);
	memcpy(p->global + 10, t + 212, 3);
	memcpy(p->global + 13, t + 215, 5);
	memcpy(p->global + 18, t + 220, 32);
	memcpy(p->global + 50, t + 252, 32);
	memcpy(p->weights, c->tool + 568, 5);
	memcpy(&p->hist_en, c->tool + 573, 5);
	memcpy(p->wdr, c->tool + 578, 16);
	memcpy(p->misc, c->tool + 626, 6);
	if (!t40_lce_wdr_on) {
		p->wdr[0] = 0;
	} else if (!p->wdr[1]) {
		uint16_t v = (*(volatile uint32_t *)wdr_lce_para >> 7) & 0xffff;

		p->wdr[2] = v;
		c->tool[578 + 4] = v;	/* OEM writes it back into the section copy */
		c->tool[578 + 5] = v >> 8;
	}
}

/* tisp_lce_tgain_interp_strength: the five histogram-shaping strengths of
 * both pipelines follow the total gain */
static void t40_lce_gain_strengths(struct t40_lce_ctx *c)
{
	const uint8_t *t = c->tool + (t40_lce_wdr_on ? T40_LCE_WDR_HALF : 0);
	unsigned int i;

	for (i = 0; i < 5; i++) {
		c->par.local[5 + i] = t40_intp_u8_at(t40_lce_tgain, t + 25 + 11 * i);
		c->par.global[5 + i] = t40_intp_u8_at(t40_lce_tgain, t + 157 + 11 * i);
	}
}

/* ---- hardware access ---- */

/* tisp_lce_write_all_reg: the 32 x 16 byte LUT */
static void t40_lce_write_lut(const struct t40_lce_ctx *c)
{
	const uint8_t *l = &c->work.lut[0][0];
	unsigned int i;

	system_reg_write(0xe080, 1);
	system_reg_write(0x501e0, 0x101);
	for (i = 0; i < 128; i++)
		system_reg_write(0x501e4, l[4 * i] | (l[4 * i + 1] << 8) |
				 (l[4 * i + 2] << 16) | ((uint32_t)l[4 * i + 3] << 24));
	system_reg_write(0x501e0, 0x7f0102);
	system_reg_write(0xe084, 1);
}

/* tisp_lce_clr_ram0/1: zero one statistics RAM (1440 words) */
static void t40_lce_clear_ram(uint32_t ctl)
{
	unsigned int i;

	system_reg_write(ctl, 0x101);
	for (i = 0; i < 1440; i++)
		system_reg_write(ctl + 4, 0);
	system_reg_write(ctl, 0x59f0102);
}

/* tisp_lce_get_data: read one statistics RAM (ping 0 / pong 1) */
static void t40_lce_read_stats(struct t40_lce_ctx *c, unsigned int ram)
{
	uint32_t hctl = ram ? 0x501d0 : 0x501c0, sctl = ram ? 0x50210 : 0x50200;
	unsigned int i, j;

	system_reg_write(hctl, 0x201);
	for (i = 0; i < T40_LCE_CELLS; i++)
		for (j = 0; j < T40_LCE_BINS; j++)
			c->stat_hist[i][j] = system_reg_read(hctl + 8);
	system_reg_write(hctl, 0x59f0202);
	system_reg_write(sctl, 0x201);
	for (i = 0; i < T40_LCE_CELLS; i++)
		c->stat_sumy[i] = system_reg_read(sctl + 8);
	system_reg_write(sctl, 0x2c0202);
}

/* tisp_lce_curve_init_default: identity LUT and curves */
static void t40_lce_identity(struct t40_lce_ctx *c)
{
	struct t40_lce_work *w = &c->work;
	unsigned int i, j;

	for (i = 0; i < T40_LCE_TILES; i++)
		for (j = 0; j < 16; j++)
			w->lut[i][j] = j * 16;
	t40_lce_write_lut(c);
	for (i = 0; i < T40_LCE_BINS; i++)
		w->gcur[i] = i * 32;
	for (i = 0; i < T40_LCE_TILES; i++)
		for (j = 0; j < T40_LCE_BINS; j++)
			w->lcur[i][j] = j * 32;
}

/* ---- histogram steps (OEM lce_* helpers) ---- */

/* lce_hist_filter_and_judge: 5-tap smoothing, Q20 sum restored (missing
 * mass spread over all bins, excess taken evenly from the non-empty bins);
 * peak and median bin */
static void t40_lce_filter(const uint32_t *in, uint32_t *out, const uint8_t *wt, uint32_t res[2])
{
	uint32_t v[T40_LCE_BINS], sum = 0, peak = 0, median = 0, cum = 0;
	unsigned int i;
	bool found = false;

	for (i = 0; i < T40_LCE_BINS; i++) {
		uint32_t num = 0, den = 0;
		int j;

		for (j = -2; j <= 2; j++)
			if ((int)i + j >= 0 && (int)i + j < (int)T40_LCE_BINS) {
				num += in[i + j] * wt[j + 2];
				den += wt[j + 2];
			}
		v[i] = den ? (num + (den >> 1)) / den : 0;
		sum += v[i];
	}
	if (sum < T40_LCE_TOTAL) {
		uint32_t miss = T40_LCE_TOTAL - sum;

		for (i = 0; i < T40_LCE_BINS; i++)
			v[i] += (miss >> 5) + (i < (miss & 31));
	} else if (sum > T40_LCE_TOTAL) {
		int32_t excess = sum - T40_LCE_TOTAL, left = 0, step;

		for (i = 0; i < T40_LCE_BINS; i++)
			left += v[i] != 0;
		step = excess / left;
		while (excess >= left) {
			/* take step from every bin; bins at or below step empty */
			left = T40_LCE_BINS;
			for (i = 0; i < T40_LCE_BINS; i++) {
				if ((uint32_t)step < v[i]) {
					v[i] -= step;
					excess -= step;
				} else {
					left--;
					excess -= v[i];
					v[i] = 0;
				}
			}
			if (left > 0)
				step = excess / left;
		}
		for (i = 0; i < T40_LCE_BINS && excess > 0; i++)
			if (v[i]) {
				v[i]--;
				excess--;
			}
	}
	for (i = 0; i < T40_LCE_BINS; i++) {
		out[i] = v[i];
		if (v[peak] < v[i])
			peak = i;
		if (!found) {
			cum += v[i];
			if (cum >= 0x80001) {
				median = i;
				found = true;
				if (v[i] + T40_LCE_TOTAL < cum * 2)
					median = (i - 1) & 255;
			}
		}
	}
	res[0] = peak;
	res[1] = median;
}

/* lce_head_tail_search: first/last bin where the cut-off mass is exceeded */
static void t40_lce_head_tail(const uint32_t *h, uint32_t head_cut, uint32_t tail_cut, uint32_t res[2])
{
	uint32_t sum = 0, head = 0, tail = 31;
	unsigned int i;

	for (i = 0; i < T40_LCE_BINS; i++) {
		sum += h[i];
		if (sum > (head_cut << 11)) {
			head = i;
			break;
		}
	}
	sum = 0;
	for (i = T40_LCE_BINS; i > head;) {
		--i;
		sum += h[i];
		if (sum > (tail_cut << 11)) {
			tail = i;
			break;
		}
	}
	res[0] = head;
	res[1] = tail;
}

/* lce_hist_method: clip the histogram under a triangle (head, centre, tail),
 * redistribute the clipped mass by weight */
static int t40_lce_hist_method(const uint32_t *in, uint32_t *out, const uint32_t p[8])
{
	int32_t wts[T40_LCE_BINS], v[T40_LCE_BINS], i, tw = 0, sum = 0, total = 0;
	int32_t head = p[0], center = p[1], tail = p[2], base = p[3], height = p[4], wb = p[5], wh = p[6];
	int32_t range = tail - head + 1, residue;

	/* the OEM does not check the knots; tail > 31 or no weight would make
	 * it write past the histogram or divide by zero */
	if (tail > 31)
		return -1;
	for (i = 0; i < (int)T40_LCE_BINS; i++) {
		int32_t ceil, wt, lim;

		if (i < head || i > tail) {
			ceil = 0;
			wt = 0;
		} else if (i == center) {
			ceil = (base + height) * 256;
			wt = wb + wh;
		} else if (i < center) {
			ceil = (i * height * 256) / center + base * 256;
			wt = i * wh / center + wb;
		} else {
			ceil = ((31 - i) * height * 256) / (31 - center) + base * 256;
			wt = (31 - i) * wh / (31 - center) + wb;
		}
		wts[i] = wt;
		tw += wt;
		lim = (t40_lce_mul((int32_t)(in[i] - 0x8000), p[7]) >> 7) + 0x8000;
		v[i] = lim < ceil ? lim : ceil;
		total += v[i];
	}
	residue = range * 0x8000 - total;
	if (residue >= 0 && !tw)
		return -1;	/* OEM: division by zero */
	for (i = 0; i < (int)T40_LCE_BINS; i++) {
		if (i < head || i > tail)
			v[i] = 0x8000;
		else if (residue >= 0)
			v[i] += t40_lce_mul(residue, wts[i]) / tw;
		else
			v[i] = (int32_t)((uint32_t)v[i] << 11) / ((total / range >> 4) + 1);
		sum += v[i];
	}
	residue = T40_LCE_TOTAL - sum;
	for (i = head; i <= tail; i++)
		v[i] += residue / range + (i - head < residue % range);
	for (i = 0; i < (int)T40_LCE_BINS; i++)
		out[i] = v[i];
	return 0;
}

/* lce_pdf_to_cdf: Q10 cumulative curve, saturated */
static void t40_lce_cdf(const uint32_t *pdf, uint16_t *cdf)
{
	uint32_t sum = 0;
	unsigned int i;

	for (i = 0; i < T40_LCE_BINS; i++) {
		if (sum >> 20)
			sum = 0xfffff;
		cdf[i] = sum >> 10;
		sum += pdf[i];
	}
}

/* lce_16bit_data_converge: move each value towards the target by
 * ratio/128 of the distance, at least min and at most max per call */
static void t40_lce_converge(uint32_t ratio, uint32_t min, uint32_t max, unsigned int n,
			     const uint16_t *old, const uint16_t *tgt, uint16_t *out)
{
	unsigned int i;

	for (i = 0; i < n; i++) {
		int32_t a = old[i], b = tgt[i], dist = a > b ? a - b : b - a, step;

		if (dist <= (int32_t)min) {
			out[i] = b;
			continue;
		}
		step = (t40_lce_mul(dist, ratio) + 64) >> 7;
		if (step < (int32_t)min)
			step = min;
		else if (step > (int32_t)max)
			step = max;
		out[i] = a + (b > a ? step : -step);
	}
}

/* lce_self_light_correct: pull the CDF towards identity on the bright and
 * the dark side, smooth, renormalise; optional contrast limit */
static int t40_lce_light_correct(const uint32_t *in, uint32_t *out, const uint32_t p[4],
				 const uint8_t *upper, const uint8_t *lower)
{
	static const uint8_t wt[5] = { 1, 2, 2, 2, 1 };
	int32_t v[T40_LCE_BINS], cdf[T40_LCE_BINS + 1], f[T40_LCE_BINS], i, j, total = 0, min = 0;

	if ((p[2] & 255) || (p[3] & 255)) {
		cdf[0] = 0;
		for (i = 0; i < (int)T40_LCE_BINS; i++)
			cdf[i + 1] = (int32_t)((uint32_t)cdf[i] + in[i]);
		for (i = 1; i <= (int)T40_LCE_BINS; i++) {
			int32_t val = cdf[i], id = i * 0x8000;

			if (val > id ? (p[2] & 255) != 0 : (p[3] & 255) != 0) {
				uint32_t r = val > id ? upper[i - 1] : lower[i - 1];

				cdf[i] = (int32_t)((uint32_t)t40_lce_mul(val, r) + (128 - r) * (uint32_t)id) >> 7;
			}
			v[i - 1] = (int32_t)((uint32_t)cdf[i] - (uint32_t)cdf[i - 1]);
		}
		for (i = 0; i < (int)T40_LCE_BINS; i++) {
			uint32_t num = 0, den = 0;

			for (j = -2; j <= 2; j++)
				if (i + j >= 0 && i + j < (int)T40_LCE_BINS) {
					num += (uint32_t)v[i + j] * wt[j + 2];
					den += wt[j + 2];
				}
			f[i] = (int32_t)num / (int32_t)den;
			total += f[i];
			if (f[i] < min)
				min = f[i];
		}
		if (min) {
			total -= min * 32;
			for (i = 0; i < (int)T40_LCE_BINS; i++)
				f[i] -= min;
		}
		if (total != T40_LCE_TOTAL) {
			int32_t den = (total >> 10) + 1, sum = 0, residue;

			if (!den)
				return -1;
			for (i = 0; i < (int)T40_LCE_BINS; i++) {
				f[i] = (int32_t)((uint32_t)f[i] << 10) / den;
				sum += f[i];
			}
			residue = T40_LCE_TOTAL - sum;
			for (i = 0; i < (int)T40_LCE_BINS; i++)
				f[i] += (residue >> 5) + (i < (residue & 31));
		}
		for (i = 0; i < (int)T40_LCE_BINS; i++)
			v[i] = f[i];
	} else {
		for (i = 0; i < (int)T40_LCE_BINS; i++)
			v[i] = in[i];
	}
	if ((p[0] & 255) == 1) {
		int32_t diff = 0;
		uint32_t max = 0;

		for (i = 0; i < (int)T40_LCE_BINS; i++) {
			uint32_t d;

			diff += v[i] - 0x8000;
			d = diff < 0 ? -(uint32_t)diff : (uint32_t)diff;
			if (d > max)
				max = d;
		}
		if (max > (p[1] << 12)) {
			uint32_t r = ((p[1] << 19) + (max >> 1)) / max;
			int32_t sum = 0, residue;

			for (i = 0; i < (int)T40_LCE_BINS; i++) {
				v[i] = (t40_lce_mul(v[i] - 0x8000, r) >> 7) + 0x8000;
				sum += v[i];
			}
			residue = T40_LCE_TOTAL - sum;
			for (i = 0; i < (int)T40_LCE_BINS; i++)
				v[i] += (residue >> 5) + (i < (residue & 31));
		}
	}
	for (i = 0; i < (int)T40_LCE_BINS; i++)
		out[i] = v[i];
	return 0;
}

/* lce_wdr_light_lock: per cell how much light sits above bin p[2]; the
 * maximum over each 2x2 neighbourhood is the lock strength of that tile */
static int t40_lce_wdr_lock(const struct t40_lce_ctx *c, int16_t *out)
{
	const struct t40_lce_par *pp = &c->par;
	const uint16_t *p = pp->wdr;
	int32_t val[T40_LCE_CELLS];
	unsigned int y, x, i;

	for (y = 0; y < 5; y++)
		for (x = 0; x < 9; x++) {
			int32_t half = pp->area[(y < pp->rem_y ? 0 : 2) + (x < pp->rem_x ? 0 : 1)] >> 1;
			int32_t sum = 0, lo = t40_lce_mul(p[3], half) >> 10, mid = t40_lce_mul(p[4], half) >> 10;
			int32_t hi = t40_lce_mul(p[5], half) >> 10, v;

			for (i = p[2]; i < T40_LCE_BINS; i++)
				sum += (int32_t)c->work.hist[y * 9 + x][i];
			if (sum < lo) {
				v = 0;
			} else if (sum < mid) {
				int32_t den = t40_lce_mul((int32_t)p[4] - p[3], half) >> 10;

				if (!den)
					return -1;
				v = (int32_t)((uint32_t)(sum - lo) << 10) / den;
			} else if (sum < hi) {
				int32_t den = t40_lce_mul((int32_t)p[5] - p[4], half) >> 10;

				if (!den)
					return -1;
				v = t40_lce_mul(sum - mid, p[6]) / den + 1024;
			} else {
				v = p[6] + 1024;
			}
			val[y * 9 + x] = v;
		}
	for (y = 0; y < 4; y++)
		for (x = 0; x < 8; x++) {
			int32_t a = val[y * 9 + x], b = val[y * 9 + x + 1];
			int32_t c2 = val[(y + 1) * 9 + x], d = val[(y + 1) * 9 + x + 1];

			if (b > a)
				a = b;
			if (c2 > a)
				a = c2;
			if (d > a)
				a = d;
			out[y * 8 + x] = a;
		}
	return 0;
}

/* lce_light_lock_adjust_hist: lift the bins above thr by the lock strength */
static int t40_lce_lock_hist(const uint32_t *in, uint32_t *out, uint32_t thr, int32_t strength)
{
	int32_t v[T40_LCE_BINS], i, tail = 0, nsum = 0, hsum = 0, residue;

	if (thr >= 31 || strength <= 0)
		goto copy;
	for (i = thr + 1; i < (int)T40_LCE_BINS; i++)
		tail += (int32_t)in[i];
	if ((31 - (int32_t)thr) * 0x8000 < tail)
		goto copy;
	for (i = 0; i < (int)T40_LCE_BINS; i++) {
		v[i] = in[i];
		if (i <= (int32_t)thr)
			continue;
		if (strength >= 1024)
			v[i] = (t40_lce_mul(strength - 1024, v[i]) >> 10) + 0x8000;
		else if (v[i] < 0x8000)
			v[i] = (int32_t)((uint32_t)t40_lce_mul(1024 - strength, v[i]) +
					 ((uint32_t)strength << 15) + 1023) >> 10;
		nsum += v[i];
	}
	if (tail == T40_LCE_TOTAL)
		return -1;	/* OEM: division by zero */
	for (i = 0; i <= (int32_t)thr; i++) {
		int32_t r = (int32_t)((0x100000U - (uint32_t)nsum) << 10) / (T40_LCE_TOTAL - tail);

		v[i] = t40_lce_mul(r, v[i]) >> 10;
		hsum += v[i];
	}
	residue = T40_LCE_TOTAL - nsum - hsum;
	for (i = 0; i <= (int32_t)thr; i++)
		v[i] += residue / ((int32_t)thr + 1) + (i < residue % ((int32_t)thr + 1));
	for (i = 0; i < (int)T40_LCE_BINS; i++)
		out[i] = v[i];
	return 0;
copy:
	for (i = 0; i < (int)T40_LCE_BINS; i++)
		out[i] = in[i];
	return 0;
}

/* lce_std_hist_transform: scale counts to Q20 (count * recip >> shift) and
 * spread the missing mass.  OEM quirk kept: a scaled sum above 2^20 is
 * taken as is, a sum of exactly 2^20 leaves the counts unscaled. */
static void t40_lce_normalize(uint32_t *h, uint32_t recip, uint32_t shift)
{
	uint32_t v[T40_LCE_BINS], sum = 0;
	unsigned int i;

	for (i = 0; i < T40_LCE_BINS; i++) {
		v[i] = ((uint64_t)h[i] * recip) >> (shift & 63);
		sum += v[i];
	}
	if ((int32_t)sum < T40_LCE_TOTAL) {
		uint32_t miss = T40_LCE_TOTAL - sum;

		for (i = 0; i < T40_LCE_BINS; i++)
			v[i] += ((int32_t)miss >> 5) + (i < (miss & 31));
	} else if (sum == T40_LCE_TOTAL) {
		return;
	}
	memcpy(h, v, sizeof(v));
}

/* get_distance_1dim: L1 distance in 1/512 */
static uint32_t t40_lce_distance(const uint32_t *a, const uint32_t *b)
{
	uint32_t sum = 0;
	unsigned int i;

	for (i = 0; i < T40_LCE_BINS; i++)
		sum += a[i] > b[i] ? a[i] - b[i] : b[i] - a[i];
	return sum >> 9;
}

static inline uint8_t t40_lce_step_byte(uint32_t old, uint32_t tgt)
{
	return old < tgt ? old + 1 : old > tgt ? old - 1 : old;
}

/* One histogram -> curve (global or one tile).  p: pipeline settings,
 * h/ce/t: head/centre/tail history bytes. */
static int t40_lce_curve(struct t40_lce_ctx *c, const uint8_t *p, uint32_t mean, uint32_t reset,
			 uint8_t *h, uint8_t *ce, uint8_t *t, bool global, int32_t lock,
			 uint32_t *dbg_method, uint32_t *dbg_light, uint32_t *dbg_lock, uint16_t *curve)
{
	struct t40_lce_work *w = &c->work;
	uint32_t judge[2], head = 0, tail = 31, center, m[8], lp[4], i;

	t40_lce_filter(w->raw, w->flt, c->par.weights, judge);
	center = p[0] == 0 ? judge[0] : p[0] == 1 ? judge[1] : p[0] == 3 ? p[1] : mean;
	if (reset < 2)
		center = t40_lce_step_byte(*ce, center);
	*ce = center;
	if (p[2]) {
		t40_lce_head_tail(w->flt, p[3], p[4], judge);
		head = judge[0];
		tail = judge[1];
		if (reset < 2) {
			head = t40_lce_step_byte(*h, head);
			tail = t40_lce_step_byte(*t, tail);
		}
		if (head > center)
			head = center;
		if (tail < center)
			tail = center;
		*h = head;
		*t = tail;
	}
	m[0] = head;
	m[1] = center;
	m[2] = tail;
	for (i = 0; i < 5; i++)
		m[3 + i] = p[5 + i];
	t40_lce_hist_method(w->raw, w->flt, m);
	if (dbg_method)
		memcpy(dbg_method, w->flt, sizeof(w->flt));
	if (global ? p[13] == 1 : p[13] != 0) {
		for (i = 0; i < 4; i++)
			lp[i] = p[14 + i];
		t40_lce_light_correct(w->flt, w->raw, lp, p + 18, p + 50);
		memcpy(w->flt, w->raw, sizeof(w->flt));
	}
	if (dbg_light)
		memcpy(dbg_light, w->flt, sizeof(w->flt));
	if (lock > 0) {
		t40_lce_lock_hist(w->flt, w->raw, c->par.wdr[2], lock);
		memcpy(w->flt, w->raw, sizeof(w->flt));
	}
	if (dbg_lock)
		memcpy(dbg_lock, w->flt, sizeof(w->flt));
	t40_lce_cdf(w->flt, curve);
	return 0;
}

/* Tisp_lce_soft: one frame of statistics -> new LUT.  reset: 0 normal,
 * 3 after a bank switch, 4 after a WDR light-lock change (jump, no converge) */
static int t40_lce_soft(struct t40_lce_ctx *c, uint32_t reset)
{
	struct t40_lce_par *pp = &c->par;
	struct t40_lce_work *w = &c->work;
	uint16_t gcur[T40_LCE_BINS], lcur[T40_LCE_BINS];
	uint32_t i, j, y, x, sum = 0, blend = *(uint32_t *)pp->blend, changed;
	bool wdr = pp->wdr[0] == 1;

	memset(gcur, 0, sizeof(gcur));
	memset(lcur, 0, sizeof(lcur));
	if (wdr) {
		int r = t40_lce_wdr_lock(c, pp->lock);

		(void)r;	/* OEM ignores the result */
		if (reset == 4) {
			memcpy(w->lock, pp->lock, sizeof(w->lock));
		} else {
			for (i = 0; i < T40_LCE_TILES; i++) {
				int32_t a = w->lock[i], b = pp->lock[i];

				if (a < b)
					w->lock[i] = a + 4 < b ? a + 4 : b;
				else if (a > b)
					w->lock[i] = a - 4 > b ? a - 4 : b;
			}
		}
	}
	memset(w->raw, 0, sizeof(w->raw));
	memset(w->flt, 0, sizeof(w->flt));
	for (i = 0; i < T40_LCE_CELLS; i++) {
		for (j = 0; j < T40_LCE_BINS; j++)
			w->raw[j] += w->hist[i][j];
		sum += w->sumy[i];
	}
	t40_lce_normalize(w->raw, pp->recip_all, pp->shift_all);
	memcpy(w->graw, w->raw, sizeof(w->raw));

	changed = 1;
	if (pp->hist_en) {
		if (!reset && t40_lce_distance(w->ghist, w->raw) <= pp->global_thr)
			changed = 0;
		else
			memcpy(w->ghist, w->raw, sizeof(w->raw));
	}
	if (changed) {
		uint32_t mean = (((pp->pixels >> 2) + sum) / (pp->pixels >> 1) >> 3) & 255;

		t40_lce_curve(c, pp->global, mean, reset, &w->ghead, &w->gcenter, &w->gtail, true, 0,
			      w->gdbg_method, w->gdbg_light, NULL, gcur);
		memcpy(w->gtgt, gcur, sizeof(gcur));
	}
	if (reset != 4)
		t40_lce_converge(pp->global[10], pp->global[11], pp->global[12], T40_LCE_BINS,
				 w->gcur, w->gtgt, gcur);
	memcpy(w->gcur, gcur, sizeof(gcur));

	for (y = 0; y < 4; y++)
		for (x = 0; x < 8; x++) {
			uint32_t tile = y * 8 + x, cell = y * 9 + x, recip, ratio;
			uint32_t ri = y + 1 < pp->rem_y ? 0 : y + 1 == pp->rem_y ? 3 : 6;
			uint32_t xi = x + 1 < pp->rem_x ? 0 : x + 1 == pp->rem_x ? 1 : 2;
			bool dbg = pp->misc[4] == y && pp->misc[5] == x;
			int32_t lock = wdr ? w->lock[tile] : 0, other;

			for (i = 0; i < T40_LCE_BINS; i++)
				w->raw[i] = w->hist[cell][i] + w->hist[cell + 1][i] +
					    w->hist[cell + 9][i] + w->hist[cell + 10][i];
			recip = pp->nrecip[ri + xi];
			t40_lce_normalize(w->raw, recip, 10);
			if (changed || t40_lce_distance(w->lhist[tile], w->raw) > pp->local_thr) {
				uint64_t prod = (uint64_t)(w->sumy[cell] + w->sumy[cell + 1] +
							   w->sumy[cell + 9] + w->sumy[cell + 10]) * recip;
				uint32_t mean = (((uint32_t)(prod >> 30) + ((uint32_t)(prod >> 29) & 1)) >> 3) & 255;

				memcpy(w->lhist[tile], w->raw, sizeof(w->raw));
				t40_lce_curve(c, pp->local, mean, reset, &w->lhead[tile], &w->lcenter[tile],
					      &w->ltail[tile], false, lock,
					      dbg ? w->ldbg_method : NULL, dbg ? w->ldbg_light : NULL,
					      dbg ? w->ldbg_lock : NULL, lcur);
				memcpy(w->ltgt[tile], lcur, sizeof(lcur));
			}
			if (reset != 4)
				t40_lce_converge(pp->local[10], pp->local[11], pp->local[12], T40_LCE_BINS,
						 w->lcur[tile], w->ltgt[tile], lcur);
			memcpy(w->lcur[tile], lcur, sizeof(lcur));

			ratio = blend;
			if (lock > 0)
				ratio = lock > 1024 ? 0 : (uint32_t)(t40_lce_mul(1024 - lock, blend) >> 10);
			other = 128 - ratio;
			for (i = 0; i < 16; i++) {
				uint32_t v = lcur[2 * i] * other + gcur[2 * i] * ratio;

				v = (v >> 9) + ((v >> 8) & 1);
				w->lut[tile][i] = v > 255 ? 255 : v;
			}
		}
	return 0;
}

/* ---- driver hooks ---- */

static bool regtrace_t40_lce_on = true;
module_param_named(t40_lce_on, regtrace_t40_lce_on, bool, 0644);
MODULE_PARM_DESC(t40_lce_on, "1 (with t40_gates_oem_offset=1) runs the LCE chain and clears TOP bit 21 after a successful init (default 1)");
static bool regtrace_t40_lce_active T40C2;

/* statistics IRQ 8 (OEM tisp_lce_main_interrupt_static, one sensor) */
static int32_t t40_lce_isr(void)
{
	struct t40_lce_ctx *c = t40_lce;
	uint32_t st = system_reg_read(0xe014), ev[12];

	if (!c)
		return 1;
	if (st & 1) {
		if (st & 0x100) {
			/* both RAMs full: take the newer one, clear the other */
			t40_lce_dual_irqs++;
			if (st & 0x10000) {
				t40_lce_clear_ram(0x501c0);
				system_reg_write(0xe010, 1);
				t40_lce_read_stats(c, 1);
				system_reg_write(0xe010, 0x10000);
			} else {
				t40_lce_clear_ram(0x501d0);
				system_reg_write(0xe010, 0x10000);
				t40_lce_read_stats(c, 0);
				system_reg_write(0xe010, 1);
			}
		} else {
			t40_lce_read_stats(c, 0);
			system_reg_write(0xe010, 1);
		}
	} else if (st & 0x100) {
		t40_lce_read_stats(c, 1);
		system_reg_write(0xe010, 0x10000);
	} else {
		t40_lce_idle_irqs++;
		return 1;
	}
	memset(ev, 0, sizeof(ev));
	ev[2] = 0x10;
	tisp_event_push(0, (uintptr_t)ev);
	return 1;
}

/* event 0x10 (OEM tisp_lce_main_process) */
static int32_t t40_lce_process(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
	struct t40_lce_ctx *c = t40_lce;
	uint32_t duan = (int32_t)*(volatile uint32_t *)wdr_lce_para >> 7;

	(void)a0; (void)a1; (void)a2; (void)a3; (void)a4;
	if (!c)
		return 0;
	if (duan != t40_lce_wdr_duan) {
		t40_lce_wdr_duan = duan;
		if (c->par.wdr[0] == 1 && !c->par.wdr[1]) {
			c->par.wdr[2] = duan;
			t40_lce_refresh = 4;
		}
	}
	memcpy(c->work.hist, c->stat_hist, sizeof(c->stat_hist));
	memcpy(c->work.sumy, c->stat_sumy, sizeof(c->stat_sumy));
	t40_lce_soft(c, t40_lce_refresh & 0xff);
	t40_lce_refresh = 0;
	t40_lce_write_lut(c);
	t40_lce_frames = t40_lce_frames + 1 == 0x1fffffff ? 15 : t40_lce_frames + 1;
	return 0;
}

static void t40_lce_load_bank(struct t40_lce_ctx *c)
{
	memcpy(c->tool, (const uint8_t *)(uintptr_t)tparamsN[0] + T40_LCE_TOOL_OFFSET, T40_LCE_TOOL_BYTES);
	t40_lce_load_params(c);
	t40_lce_gain_strengths(c);
}

/* OEM tisp_main_init -> tisp_lce_init(sensor 0, width, height, sensors 1) */
static int t40_lce_init(uint32_t width, uint32_t height)
{
	static const uint32_t precision_offset[4] = { 0x40000000, 0x20000000, 0x10000000, 0x08000000 };
	struct t40_lce_ctx *c;
	struct t40_lce_par *p;
	uint32_t ch = height / 5, cw = (width >> 1) / 9 * 2, a[4], prec, i;
	int ret = -1;

	/* OEM tears LCE down in tisp_deinit at every session end */
	if (regtrace_t40_lce_active)
		t40_lce_cleanup();
	if (ch * cw < 16384 || !tparamsN[0])
		goto out;
	c = vzalloc(sizeof(*c));
	if (!c)
		goto out;
	t40_lce = c;
	p = &c->par;
	t40_lce_load_bank(c);

	a[0] = (ch + 1) * (cw + 2);
	a[1] = (ch + 1) * cw;
	a[2] = ch * (cw + 2);
	a[3] = ch * cw;
	prec = a[3] < 0x8001 ? 3 : a[3] < 0x10001 ? 2 : a[3] < 0x20001 ? 1 : 0;
	p->nsum[0] = 2 * a[0];
	p->nsum[1] = a[0] + a[1];
	p->nsum[2] = 2 * a[1];
	p->nsum[3] = a[0] + a[2];
	p->nsum[4] = (a[0] + a[2] + a[3] + a[1]) >> 1;
	p->nsum[5] = a[1] + a[3];
	p->nsum[6] = 2 * a[2];
	p->nsum[7] = a[2] + a[3];
	p->nsum[8] = 2 * a[3];
	for (i = 0; i < 9; i++)
		p->nrecip[i] = 0x40000000U / p->nsum[i];
	memcpy(p->area, a, sizeof(a));
	p->pixels = width * height;
	p->recip_all = 0x80000000U / ((width * height) >> 2);
	p->shift_all = 12;
	p->rem_y = height % 5;
	p->rem_x = (width >> 1) % 9;
	system_reg_write(0xe000, ((ch << 16) & 0x1ff0000) | (cw & 0x1ff));
	system_reg_write(0xe004, p->rem_x | 0x10000 | (p->rem_y << 8) | (prec << 20));
	system_reg_write(0xe008, ((((a[1] >> 1) + precision_offset[prec]) / a[1] << 16) & 0x1fff0000) |
			 ((((a[0] >> 1) + precision_offset[prec]) / a[0]) & 0x1fff));
	system_reg_write(0xe00c, ((((a[3] >> 1) + precision_offset[prec]) / a[3] << 16) & 0x1fff0000) |
			 ((((a[2] >> 1) + precision_offset[prec]) / a[2]) & 0x1fff));
	t40_lce_identity(c);
	t40_lce_clear_ram(0x501c0);
	t40_lce_clear_ram(0x501d0);
	system_irq_func_set(8, (int32_t)(uintptr_t)t40_lce_isr);
	tisp_event_set_cb(0, 0x10, (int32_t)(uintptr_t)t40_lce_process);
	ret = 0;
out:
	regtrace_t40_lce_active = !ret;
	if (ret)	/* KERN_INFO lines of this driver are dropped (t40_printk) */
		printk(KERN_ERR "tx_isp_t40_recovered: lce init %ux%u failed (%d), TOP bit 21 stays bypassed\n",
		       width, height, ret);
	return ret;
}

/* OEM tisp_day_or_night_s_ctrl -> tisp_lce_dn_params_refresh */
static int t40_lce_dn_refresh(void)
{
	struct t40_lce_ctx *c = t40_lce;

	if (!regtrace_t40_lce_active || !c || !tparamsN[0])
		return 0;
	t40_lce_load_bank(c);
	t40_lce_clear_ram(0x501c0);
	t40_lce_clear_ram(0x501d0);
	t40_lce_identity(c);
	t40_lce_refresh = 3;
	return 0;
}

/* OEM tisp_main_long_tgain_update -> tisp_lce_tgain_update(sensor, tgain 16.16 log2) */
static void t40_lce_tgain_update(uint32_t tgain)
{
	if (!regtrace_t40_lce_active || !t40_lce)
		return;
	if ((int32_t)((int32_t)tgain < (int32_t)t40_lce_tgain ? t40_lce_tgain - tgain : tgain - t40_lce_tgain) >> 7 <= 0)
		return;
	t40_lce_tgain = tgain;
	t40_lce_gain_strengths(t40_lce);
}

/* module exit / session end: unhook IRQ 8 and event 0x10, free the state */
static void t40_lce_cleanup(void)
{
	struct t40_lce_ctx *c = t40_lce;

	if (!regtrace_t40_lce_active)
		return;
	regtrace_t40_lce_active = false;
	system_irq_func_set(8, 0);
	tisp_event_set_cb(0, 0x10, 0);
	t40_lce = NULL;
	vfree(c);
	/* OEM tisp_slake_all -> tisp_lce_remove: the next session starts clean.
	 * The literal chain kept the frame counter here, so its next init left
	 * the parameter table pointers NULL (tisp_lce_tool_to_awdr fills them only
	 * while the counter is 0) and copied from address 0. */
	t40_lce_wdr_on = 0;
	t40_lce_frames = 0;
	t40_lce_refresh = 0;
	t40_lce_tgain = 0;
	t40_lce_wdr_duan = 0x1f;
}

#endif
