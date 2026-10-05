/*
 * Host test of driver/t23/tx_isp_t23_awb_stock.h: the AWB history, the
 * change test and the gain ramp of Tiziano_awb_fpga of the stock
 * tx-isp-t23.ko (md5 8237acb1).  The golden sequences were produced by the
 * stock module in the MIPS emulator with scripted Tiziano_Awb_Ct_Detect
 * results (driver/t23/audit/awb_stock_emu.py, GOLDEN=); the emulator audit
 * itself compares many more.  The hand written cases pin the semantics.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../driver/t23/tx_isp_t23_awb_stock.h"
#include "tx_isp_t23_awb_stock_golden.inc"

static uint32_t mf_of(uint32_t ratio)
{
	return (((1U << 26) / ratio) + 512U) >> 10;
}

static void test_golden(void)
{
	unsigned int s, f, k;
	unsigned int frames = 0;

	for (s = 0; s < sizeof(gold_seqs) / sizeof(gold_seqs[0]); s++) {
		const struct gold_seq *q = &gold_seqs[s];
		struct t23x_awb_history h;
		uint32_t mf[6], orig_rg = 0, orig_bg = 0;

		memset(&h, 0, sizeof(h));
		memcpy(mf, q->mf0, sizeof(mf));
		for (f = 0; f < q->n; f++) {
			const struct gold_frame *g = &q->f[f];
			uint32_t rg = g->rg, bg = g->bg, ct = g->ct, steps;
			bool first, upd;

			steps = t23x_awb_history_push(&h, &rg, &bg, &ct,
						      q->window, &first);
			upd = t23x_awb_need_update(first, rg, bg, orig_rg,
						   orig_bg, false, mf);
			if (upd) {
				orig_rg = rg;
				orig_bg = bg;
				t23x_awb_ramp(mf, mf_of(rg), mf_of(bg), steps,
					      g->flag, q->iq_th, q->tol_en,
					      q->tol_th);
			}
			assert((unsigned int)upd == g->upd);
			assert(orig_rg == g->orig_rg && orig_bg == g->orig_bg);
			assert(ct == g->ct_out);
			for (k = 0; k < 6; k++)
				assert(mf[k] == g->mf[k]);
			frames++;
		}
	}
	assert(frames > 100);
}

static void test_history(void)
{
	struct t23x_awb_history h;
	uint32_t rg = 400, bg = 300, ct = 5000, steps;
	bool first;
	unsigned int i;

	memset(&h, 0, sizeof(h));
	steps = t23x_awb_history_push(&h, &rg, &bg, &ct, 5, &first);
	assert(first && steps == 1 && rg == 400 && bg == 300 && ct == 5000);
	for (i = 0; i < 15; i++)
		assert(h.rg[i] == 400 && h.ct[i] == 5000);

	/* second run: the window of 5 still holds four copies of the first */
	rg = 500; bg = 300; ct = 5000;
	steps = t23x_awb_history_push(&h, &rg, &bg, &ct, 5, &first);
	assert(!first && steps == 5);
	/* weights 11..15, newest (500) heaviest: (4 * ... + 500 * 15) */
	assert(rg == (11U * 400 + 12U * 400 + 13U * 400 + 14U * 400 + 15U * 500 +
		      32U) / 65U);
	assert(bg == 300 && ct == 5000);

	/* the window is limited to 1..15 */
	steps = t23x_awb_history_push(&h, &rg, &bg, &ct, 0, &first);
	assert(steps == 1);
	steps = t23x_awb_history_push(&h, &rg, &bg, &ct, 99, &first);
	assert(steps == 15);
}

static void test_ramp(void)
{
	uint32_t mf[6] = { 0, 0, 324, 255, 324, 255 };
	unsigned int i;

	/* an unchanged target below the tolerance does nothing */
	t23x_awb_ramp(mf, 326, 255, 5, false, 4, 0, 0);
	assert(mf[0] == 0 && mf[1] == 0 && mf[2] == 324 && mf[4] == 324);

	/* a change above the tolerance starts a 5 step ramp ... */
	t23x_awb_ramp(mf, 374, 255, 5, false, 4, 0, 0);
	assert(mf[0] == 1 && mf[1] == 1 && mf[2] == 374 && mf[4] == 334);
	/* ... that keeps its target and arrives on the step counter */
	for (i = 1; i < 5; i++) {
		t23x_awb_ramp(mf, 374, 255, 5, false, 4, 0, 0);
		if (i < 4)
			assert(mf[0] == 1 && mf[4] < 374);
	}
	assert(mf[0] == 0 && mf[4] == 374 && mf[2] == 374);

	/* ToleranceEn takes the tolerance of the cluster object */
	mf[0] = 0; mf[1] = 0;
	t23x_awb_ramp(mf, 380, 255, 5, false, 4, 1, 100);
	assert(mf[0] == 0 && mf[2] == 374);
	t23x_awb_ramp(mf, 480, 255, 5, false, 4, 1, 100);
	assert(mf[0] == 1 && mf[2] == 480);

	/* an empty run sets target and gains to unity (a running ramp goes on
	 * towards the unity target over its remaining steps) */
	t23x_awb_ramp(mf, 256, 256, 5, true, 4, 0, 0);
	assert(mf[2] == 0x100 && mf[3] == 0x100 && mf[1] == 2);
	mf[0] = 0; mf[1] = 0; mf[4] = 400; mf[5] = 300;
	t23x_awb_ramp(mf, 256, 256, 5, true, 4, 0, 0);
	assert(mf[0] == 0 && mf[2] == 0x100 && mf[3] == 0x100 &&
	       mf[4] == 0x100 && mf[5] == 0x100);
}

int main(void)
{
	uint32_t mf[6] = { 0, 0, 1, 1, 1, 1 };

	test_golden();
	test_history();
	test_ramp();
	assert(t23x_awb_need_update(true, 1, 1, 1, 1, false, mf));
	assert(!t23x_awb_need_update(false, 1, 1, 1, 1, false, mf));
	assert(t23x_awb_need_update(false, 2, 1, 1, 1, false, mf));
	assert(t23x_awb_need_update(false, 1, 1, 1, 1, true, mf));
	mf[3] = 2;
	assert(t23x_awb_need_update(false, 1, 1, 1, 1, false, mf));
	puts("tx_isp_t23_awb_stock_test ok");
	return 0;
}
