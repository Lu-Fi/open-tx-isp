// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: tisp_lsf_interrupt_static of driver/t32/tx_isp_t32_lsf.h
 * against Matt Davis' recovered vendor C (ref/, PR #15 @ 2e2e06bc, built
 * through fix_op8_lsf.sh).  Compared: the cache sync, histogram read and
 * event push (channel and all 8 event words; the vendor's unset words are
 * the ref's zeroed stack) in order, and the return value.
 * Inputs: channel byte 0..3, random buffer sets.  Exit 0 = all equal.
 */
#include <stdlib.h>
#include "t32o8_rec.h"
#include "tx_isp_t32_lsf.h"

u64 t32o8_lsf_interrupt_static(u32, u32, u32, u32);
u8 t32o8_e1d78[4];	/* t32_data_e1d78: the current LCE channel (fix_op8_lsf.sh) */

enum { F_SYNC = 1, F_HIST, F_EVENT };
static struct t32o8_log *cur;
u64 t32o8_dma_cache_sync(u32 a0, u32 a1, u32 a2, u32 a3) { t32o8_rec(cur, F_SYNC, a0, a1, a2, a3); return 0; }
u64 tisp_lce_get_hist_data(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; t32o8_rec(cur, F_HIST, a0, a1, 0, 0); return 0; }
static void o_sync(void *d, u32 a, u32 s, int dir) { t32o8_rec(cur, F_SYNC, T32O_PTR(d), a, s, (u32)dir); }
static void o_hist(u32 ch, u32 b) { t32o8_rec(cur, F_HIST, ch, b, 0, 0); }
static void o_ev(u32 ch, const u32 *ev)
{
	struct t32o8_call *c = t32o8_rec(cur, F_EVENT, ch, 0, 0, 0);

	memcpy(c->data, ev, 32);
}
static const struct t32_lsf_ops ops = { o_sync, o_hist, o_ev };

static u32 rs = 0x15f15f15u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

int main(void)
{
	unsigned int c, k, failed = 0;

	for (c = 0; c < 4000; c++) {
		u32 vr, orr;
		u8 ch = (u8)(rnd() % 4);

		t32o_reset();
		t32o8_clear();
		for (k = 0; k < sizeof(t32_data_e1db8); k++)
			t32_data_e1db8[k] = (u8)rnd();
		t32o8_e1d78[0] = ch;
		cur = &t32o8_v;
		t32o_ev_n = 0;
		vr = (u32)t32o_call((t32o_fn)(void (*)(void))t32o8_lsf_interrupt_static, rnd(), 0, 0, 0, 0);
		if (t32o_ev_n) {	/* env.c's tisp_event_push records the vendor event */
			struct t32o8_call *e = t32o8_rec(cur, F_EVENT, t32o_ev_ch, 0, 0, 0);

			memcpy(e->data, t32o_ev_w, 32);
		}
		cur = &t32o8_o;
		orr = (u32)t32_lsf_interrupt_static(&ops, ch, t32_data_e1db8);
		if ((t32o8_cmp("lsf", failed < 8) || vr != orr || t32o_ev_n != 1) && failed++ < 8)
			printf("MISMATCH ch %u: ret %u / %u\n", ch, vr, orr);
	}
	printf("t32_lsf_oracle: 4000 compared, %u mismatches\n", failed);
	return failed ? 1 : 0;
}
