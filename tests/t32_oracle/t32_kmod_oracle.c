// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: module entry / exit and the spin-lock wrapper of
 * driver/t32/tx_isp_t32_kmod.h against Matt Davis' reviewed kernel-wrapper C
 * (ref/, PR #15 @ 2e2e06bc, built with fix_op8_kwrap.sh): init_module,
 * cleanup_module, __private_spin_lock_irqsave.
 *
 * Compared: the calls in order, return values, the stored flags word.
 * Inputs: tx_isp_init results 0 / negative / positive, random saved flags.
 * Ours also counts preempt_disable (the vendor's preempt_count + 1, which
 * Matt's C keeps inside spin_lock_irqsave): one per lock call.
 * Exit 0 = all equal, 1 = mismatch.
 */
#include <stdlib.h>
#include "t32o8_rec.h"
#include "tx_isp_t32_kmod.h"

int init_module(void);
void cleanup_module(void);
void __private_spin_lock_irqsave(unsigned int *lock, unsigned long *flags);

enum { F_INIT = 1, F_SINIT, F_EXIT, F_SEXIT, F_IRQ, F_PREEMPT };
static struct t32o8_log *cur;
static u32 r_init, r_sinit, r_flags;

int tx_isp_init(void) { t32o8_rec(cur, F_INIT, 0, 0, 0, 0); return (int)r_init; }
int tx_isp_sinfo_init(void) { t32o8_rec(cur, F_SINIT, 0, 0, 0, 0); return (int)r_sinit; }
int tx_isp_exit(void) { t32o8_rec(cur, F_EXIT, 0, 0, 0, 0); return 0; }
int tx_isp_sinfo_exit(void) { t32o8_rec(cur, F_SEXIT, 0, 0, 0, 0); return 0; }
unsigned long t32o8_irq_save(void *lock)
{
	(void)lock;
	t32o8_rec(cur, F_IRQ, 0, 0, 0, 0);
	if (cur == &t32o8_v)	/* the vendor's preempt_count + 1 is part of spin_lock_irqsave */
		t32o8_rec(cur, F_PREEMPT, 0, 0, 0, 0);
	return r_flags;
}

static int o_init(void) { return tx_isp_init(); }
static int o_sinit(void) { return tx_isp_sinfo_init(); }
static void o_exit(void) { tx_isp_exit(); }
static void o_sexit(void) { tx_isp_sinfo_exit(); }
static unsigned long o_irq(void) { return t32o8_irq_save(NULL); }
static void o_preempt(void) { t32o8_rec(cur, F_PREEMPT, 0, 0, 0, 0); }
static const struct t32_kmod_ops ops = { o_init, o_sinit, o_exit, o_sexit, o_irq, o_preempt };

static u32 rs = 0x4b0d4b0du;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

int main(void)
{
	unsigned int c, failed = 0, compared = 0;

	for (c = 0; c < 3000; c++) {
		u32 which = c % 3, vr = 0, orr = 0;
		unsigned long fv = rnd(), fo = fv;
		unsigned int lock = rnd();

		r_init = (rnd() % 3) ? 0 : (rnd() & 1) ? (u32)-(int)(rnd() % 30) : rnd();
		r_sinit = rnd();
		r_flags = rnd();
		t32o8_clear();
		cur = &t32o8_v;
		switch (which) {
		case 0: vr = (u32)init_module(); break;
		case 1: cleanup_module(); break;
		default: __private_spin_lock_irqsave(&lock, &fv); break;
		}
		cur = &t32o8_o;
		switch (which) {
		case 0: orr = (u32)t32_init_module(&ops); break;
		case 1: t32_cleanup_module(&ops); break;
		default: t32_private_spin_lock_irqsave(&ops, &lock, &fo); break;
		}
		compared++;
		if ((t32o8_cmp("kmod", failed < 8) || vr != orr || fv != fo) && failed++ < 8)
			printf("MISMATCH case %u: ret %08x / %08x flags %lx / %lx\n", which, vr, orr, fv, fo);
	}
	printf("t32_kmod_oracle: %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
