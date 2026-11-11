// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our LDC offline open / release and the LDC ISR (driver/t32/tx_isp_t32_ldcirq.h) against Matt
 * Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc).
 *
 * The OS calls are recorded on both sides (hook, argument, number of completes signalled before it) and compared
 * in order, plus the return value and the final complete count / object.  Inputs: random device / interrupt
 * object addresses, random lib_tisp_ldc_intp_handle results (bit 0 set / clear).
 *
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_ldcirq.h"

u64 ldc_offline_open(u32, u32, u32, u32);
u64 ldc_offline_release(u32, u32, u32, u32);
u64 ldc_interrupt_service_routine(u32, u32, u32, u32);

#define F(fn)	((t32o_fn)(void (*)(void))(fn))

u32 t32_data_d9944[1], t32_data_d9948[1];

static u32 rs = 0x1d0c4a77u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

enum { H_INIT = 1, H_COMPLETE, H_EN, H_DIS, H_HANDLE };
struct rec { u32 id, a0, done_n; };
static struct rec vlog[8], olog[8];
static unsigned int vn, on;
static u32 handle_ret, o_done_n, o_done_arg;

static void vrec(u32 id, u32 a0)
{
	if (vn < 8)
		vlog[vn] = (struct rec){ id, a0, t32o_complete_n };
	vn++;
}

u64 private_init_completion(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; vrec(H_INIT, a0); return 0; }
u64 tx_ldc_enable_irq(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; vrec(H_EN, a0); return 0; }
u64 tx_ldc_disable_irq(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; vrec(H_DIS, a0); return 0; }
u64 lib_tisp_ldc_intp_handle(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a1; (void)a2; (void)a3;
	vrec(H_HANDLE, 0);
	return ((u64)rnd() << 32) | handle_ret;
}

static void orec(u32 id, u32 a0)
{
	if (on < 8)
		olog[on] = (struct rec){ id, a0, o_done_n };
	on++;
}
static void o_init(void *c, void *p) { (void)c; orec(H_INIT, (u32)(uintptr_t)p); }
static void o_complete(void *c, void *p) { (void)c; o_done_n++; o_done_arg = (u32)(uintptr_t)p; }
static void o_en(void *c, void *p) { (void)c; orec(H_EN, (u32)(uintptr_t)p); }
static void o_dis(void *c, void *p) { (void)c; orec(H_DIS, (u32)(uintptr_t)p); }
static u32 o_handle(void *c) { (void)c; orec(H_HANDLE, 0); return handle_ret; }

int main(void)
{
	unsigned int c, compared = 0, failed = 0, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 20000;
	struct t32_ldcirq_hooks h = { .init_completion = o_init, .complete = o_complete, .enable_irq = o_en,
				      .disable_irq = o_dis, .intp_handle = o_handle };

	for (c = 0; c < n; c++) {
		unsigned char *dev;
		u32 r, o, k = c % 3, bad;

		t32o_reset();
		t32o_complete_n = t32o_complete_arg = 0;
		vn = on = o_done_n = o_done_arg = 0;
		memset(vlog, 0, sizeof(vlog));
		memset(olog, 0, sizeof(olog));
		dev = t32o_alloc(64);
		h.dev = dev;
		h.irq = (void *)(uintptr_t)(c % 7 ? (rnd() & 0x7ffffffcu) : 0);
		t32_data_d9948[0] = T32O_PTR(dev);
		t32_data_d9944[0] = (u32)(uintptr_t)h.irq;
		handle_ret = c % 5 == 0 ? 0 : c % 5 == 1 ? 1 : rnd();
		if (k == 0) {
			r = (u32)t32o_call(F(ldc_offline_open), rnd(), rnd(), 0, 0, 0);
			o = t32_ldc_offline_open(&h);
		} else if (k == 1) {
			r = (u32)t32o_call(F(ldc_offline_release), rnd(), rnd(), 0, 0, 0);
			o = t32_ldc_offline_release(&h);
		} else {
			r = (u32)t32o_call(F(ldc_interrupt_service_routine), rnd(), rnd(), 0, 0, 0);
			o = t32_ldc_isr(&h);
		}
		bad = r != o || vn != on || memcmp(vlog, olog, sizeof(vlog)) || t32o_complete_n != o_done_n ||
		      t32o_complete_arg != o_done_arg;
		compared++;
		if (bad && failed++ < 10)
			printf("MISMATCH fn %u case %u: ret %x/%x calls %u/%u completes %u/%u arg %x/%x\n", k, c, r, o,
			       vn, on, t32o_complete_n, o_done_n, t32o_complete_arg, o_done_arg);
	}
	printf("t32_ldcirq_oracle: %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
