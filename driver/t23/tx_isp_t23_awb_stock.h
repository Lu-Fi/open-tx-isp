/*
 * T23 AWB: the history and the gain ramp of Tiziano_awb_fpga of the stock
 * tx-isp-t23.ko (md5 8237acb1, 0x1ec9c; the history at 0x1f3c8..0x1f4fc, the
 * ramp at 0x1f5a8..0x1f7a4), as the open AWB runtime uses them.
 *
 * Plain C on 32 bit values (no kernel includes, no 64 bit division) so the
 * host test (tests/tx_isp_t23_awb_stock_test.c) and the emulator audit
 * (driver/t23/audit/awb_stock_emu.py) compile exactly what the module runs.
 */
#ifndef TX_ISP_T23_AWB_STOCK_H
#define TX_ISP_T23_AWB_STOCK_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdbool.h>
#include <stdint.h>
#endif

#define T23X_AWB_HIST 15U

/* the stock .data/.bss history: 15 entries of rg, bg and ct, and the
 * "first run" flag (set again by a day/night parameter refresh) */
struct t23x_awb_history {
	uint32_t rg[T23X_AWB_HIST];
	uint32_t bg[T23X_AWB_HIST];
	uint32_t ct[T23X_AWB_HIST];
	bool primed;
};

/*
 * Pushes the result of one run.  The first run fills all entries with it
 * and leaves it as it is (returns ramp length 1, *first set).  Later runs
 * shift, append and replace rg/bg/ct by the linearly weighted mean (weights
 * 1..15, newest heaviest) of the last `window` entries, rounded; `window` is
 * the IQ's _awb_cof[0] limited to 1..15.  The window reaches back into the
 * copies the first run made, as in stock.  Returns the ramp length.
 */
static inline uint32_t t23x_awb_history_push(struct t23x_awb_history *h,
					     uint32_t *rg, uint32_t *bg,
					     uint32_t *ct, uint32_t window,
					     bool *first)
{
	uint32_t i, start, weight_sum = 0, rg_sum = 0, bg_sum = 0, ct_sum = 0;

	if (!h->primed) {
		for (i = 0; i < T23X_AWB_HIST; ++i) {
			h->rg[i] = *rg;
			h->bg[i] = *bg;
			h->ct[i] = *ct;
		}
		h->primed = true;
		*first = true;
		return 1U;
	}
	*first = false;
	for (i = 0; i + 1U < T23X_AWB_HIST; ++i) {
		h->rg[i] = h->rg[i + 1U];
		h->bg[i] = h->bg[i + 1U];
		h->ct[i] = h->ct[i + 1U];
	}
	h->rg[T23X_AWB_HIST - 1U] = *rg;
	h->bg[T23X_AWB_HIST - 1U] = *bg;
	h->ct[T23X_AWB_HIST - 1U] = *ct;

	window = window >= 16U ? 15U : (window ? window : 1U);
	start = T23X_AWB_HIST - window;
	for (i = start; i < T23X_AWB_HIST; ++i) {
		uint32_t weight = i + 1U;

		rg_sum += weight * h->rg[i];
		bg_sum += weight * h->bg[i];
		ct_sum += weight * h->ct[i];
		weight_sum += weight;
	}
	*rg = (rg_sum + (weight_sum >> 1)) / weight_sum;
	*bg = (bg_sum + (weight_sum >> 1)) / weight_sum;
	*ct = (ct_sum + (weight_sum >> 1)) / weight_sum;
	return window;
}

/*
 * Tiziano_awb_fpga runs set_gain (and the ramp) only when something changed:
 * the first run, a different history mean rg or bg than at the last update,
 * a manual white balance mode, SetWB's one-shot mark (awb_moa), or a ramp
 * that has not arrived.
 */
static inline bool t23x_awb_need_update(bool first, uint32_t rg, uint32_t bg,
					uint32_t orig_rg, uint32_t orig_bg,
					bool manual_or_moa, const uint32_t *mf)
{
	return first || rg != orig_rg || bg != orig_bg || manual_or_moa ||
	       mf[2] != mf[4] || mf[3] != mf[5];
}

static inline uint32_t t23x_awb_absdiff(uint32_t a, uint32_t b)
{
	return a < b ? b - a : a - b;
}

/*
 * The gain ramp.  mf is stock _awb_mf_para: [0] ramp running, [1] step
 * counter, [2]/[3] target, [4]/[5] current Q8 gains (the values the gains
 * are written from).  target_* are the Q8 gains of the new history mean,
 * steps the ramp length (history window, 1 on the first run), no_weight the
 * flag of an empty run (it resets the gains to 0x100).  A new target is only
 * taken when it differs from the previous target by more than the tolerance
 * (the IQ's _awb_cof[1] = iq_th, or _awb_cluster[9] = tol_th with
 * ToleranceEn = tol_en; a running ramp always uses iq_th); a running ramp
 * keeps its target and arrives after `steps` runs.  Divisions are signed
 * and truncate toward 0 as the stock `div`.
 * Beyond stock: a window that shrank below the step counter (stock divides
 * by zero there) takes one step.
 */
static inline void t23x_awb_ramp(uint32_t *mf, uint32_t target_rg,
				 uint32_t target_bg, uint32_t steps,
				 bool no_weight, uint32_t iq_th,
				 uint32_t tol_en, uint32_t tol_th)
{
	const uint32_t e0 = mf[0], e1 = mf[1], e2 = mf[2], e3 = mf[3];
	const int32_t e4 = (int32_t)mf[4], e5 = (int32_t)mf[5];
	const uint32_t delta = t23x_awb_absdiff(e2, target_rg) +
			       t23x_awb_absdiff(e3, target_bg);
	const uint32_t th = tol_en ? tol_th : iq_th;
	const int32_t n = (int32_t)steps;

	if (no_weight)
		mf[2] = mf[3] = mf[4] = mf[5] = 0x100U;

	if (!e0) {
		if (th < delta && !no_weight) {
			mf[0] = 1U;
			mf[1] = 1U;
			mf[2] = target_rg;
			mf[4] = (uint32_t)(((int32_t)target_rg - (int32_t)e2) / n + e4);
			mf[3] = target_bg;
			mf[5] = (uint32_t)(((int32_t)target_bg - (int32_t)e3) / n + e5);
		} else {
			mf[0] = 0;
			mf[1] = 0;
		}
		return;
	}

	if (iq_th < delta && !no_weight) {
		mf[0] = 1U;
		mf[1] = 1U;
		mf[2] = target_rg;
		mf[4] = (uint32_t)(((int32_t)target_rg - e4) / n + e4);
		mf[3] = target_bg;
		mf[5] = (uint32_t)(((int32_t)target_bg - e5) / n + e5);
		return;
	}
	if (n == (int32_t)e1 + 1 || n == 1) {
		mf[0] = 0;
		mf[4] = mf[2];
		mf[5] = mf[3];
	} else {
		int32_t left = n - (int32_t)e1;

		if (left <= 0)
			left = 1;
		mf[0] = 1U;
		mf[4] = (uint32_t)(((int32_t)target_rg - e4) / left + e4);
		mf[5] = (uint32_t)(((int32_t)target_bg - e5) / left + e5);
	}
	mf[1] = mf[1] + 1U;
}

#endif /* TX_ISP_T23_AWB_STOCK_H */
