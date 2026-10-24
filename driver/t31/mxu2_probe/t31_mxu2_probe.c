// SPDX-License-Identifier: GPL-2.0
/*
 * Debug-only: does this T31 have a physically working MXU2 unit, even if the
 * Ingenic kernel blob locks it (CPPSR 0x10000034 == 0x110)?  Runs in a kernel
 * thread with IRQs off and CU2 set locally (no CU2-unusable trap, so the
 * blob's licence gate is not involved).  Staged:
 *   1. one lu1q/addw/su1q round trip vs C
 *   2. the AWB sweep/assign kernels vs their C twins on random data
 * Reserved-Instruction traps are skipped and counted (DIE_RI notifier).
 * Never leaves the module loaded: init returns -ECANCELED when done.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/completion.h>
#include <linux/kdebug.h>
#include <linux/io.h>
#include <linux/sched.h>
#include <linux/ktime.h>
#include <asm/mipsregs.h>
#include <asm/hazards.h>
#include <asm/cpu-features.h>

#include "../tx_isp_t31_awb_simd.h"

static bool guard;
static int ri_hits;

static int die_notify(struct notifier_block *nb, unsigned long val, void *data)
{
	struct die_args *a = data;

	if (val != DIE_RI || !guard || user_mode(a->regs))
		return NOTIFY_DONE;
	a->regs->cp0_epc += 4;
	ri_hits++;
	return NOTIFY_STOP;
}
static struct notifier_block die_nb = { .notifier_call = die_notify };

static u32 rng = 0x31a5b001;
static u32 rnd(void)
{
	rng = rng * 1664525u + 1013904223u;
	return rng >> 8;
}

static u32 zr[T31_AWB_ZONES + 3] __aligned(16), zb[T31_AWB_ZONES + 3] __aligned(16);
static u32 ra[T31_AWB_ZONES + 3] __aligned(16), rb[T31_AWB_ZONES + 3] __aligned(16);
static u32 wv[T31_AWB_ZONES + 3] __aligned(16);
static u32 bj[2][T31_AWB_ZONES + 3] __aligned(16), bc[2][T31_AWB_ZONES + 3] __aligned(16);
static u32 va[4] __aligned(16), vb[4] __aligned(16), vo[4] __aligned(16);

static DECLARE_COMPLETION(done);
static int result = -1;
static u32 cppsr_before, cppsr_after, sku, e3a;

static int stage1(void)
{
	unsigned long flags, st;
	int i, bad = 0, rep;

	for (rep = 0; rep < 8; rep++) {
		for (i = 0; i < 4; i++) {
			va[i] = rnd() * 2654435761u;
			vb[i] = rnd() * 40503u + rep;
			vo[i] = 0xdeadbeef;
		}
		local_irq_save(flags);
		st = read_c0_status();
		write_c0_status(st | ST0_CU2);
		back_to_back_c0_hazard();
		VLOAD(0, va);
		VLOAD(1, vb);
		VADDW(2, 0, 1);
		VSTORE(2, vo);
		t31_mxu2_scrub();
		write_c0_status(st);
		back_to_back_c0_hazard();
		local_irq_restore(flags);
		if (ri_hits)
			return -1;
		for (i = 0; i < 4; i++)
			if (vo[i] != va[i] + vb[i])
				bad++;
	}
	return bad;
}

static int stage2(u64 *ns_c, u64 *ns_v)
{
	int bad = 0, n, i, k;
	unsigned long flags;
	ktime_t t0;

	for (n = 0; n < 96; n++) {
		u32 cx, cy, thr, c1[3], c2[3], w1[2], w2[2], mode = n & 3;

		for (i = 0; i < T31_AWB_ZONES; i++) {
			u32 r = rnd();

			zr[i] = mode == 3 ? (r << 8) ^ rnd() : rnd() & 0x3ff;
			zb[i] = mode == 3 ? (rnd() << 8) ^ r : rnd() & 0x3ff;
			ra[i] = rnd() << 8;
			rb[i] = rnd();
			wv[i] = (rnd() & 3) ? rnd() & 0xffff : 0;
		}
		cx = mode == 3 ? rnd() << 8 : rnd() & 0x3ff;
		cy = mode == 3 ? rnd() << 8 : rnd() & 0x3ff;
		thr = mode == 1 ? 0 : (mode == 2 ? 0xffffffffu : rnd() & 0xffff);
		zr[223] = zr[224] = cx;
		zb[223] = zb[224] = cy + (n & 1);
		local_irq_save(flags);
		t0 = ktime_get();
		t31_awb_sweep_sum_c(zr, zb, ra, rb, cx, cy, thr, &c1[0], &c1[1], &c1[2]);
		t31_awb_sweep_wcount_c(zr, zb, wv, cx, cy, thr, &w1[0], &w1[1]);
		*ns_c += ktime_to_ns(ktime_sub(ktime_get(), t0));
		t0 = ktime_get();
		t31_awb_sweep_sum(2, zr, zb, ra, rb, cx, cy, thr, &c2[0], &c2[1], &c2[2]);
		t31_awb_sweep_wcount(2, zr, zb, wv, cx, cy, thr, &w2[0], &w2[1]);
		*ns_v += ktime_to_ns(ktime_sub(ktime_get(), t0));
		local_irq_restore(flags);
		if (ri_hits)
			return -1;
		if (memcmp(c1, c2, sizeof(c1)) || memcmp(w1, w2, sizeof(w1))) {
			bad++;
			pr_err("t31_mxu2_probe: sweep mismatch n=%d\n", n);
		}
		cond_resched();
	}
	for (n = 0; n < 48; n++) {
		for (i = 0; i < T31_AWB_ZONES; i++) {
			zr[i] = rnd() & 0x1ff;
			zb[i] = rnd() & 0x1ff;
			bj[0][i] = bj[1][i] = (rnd() & 1) ? 1 + (rnd() % 60) : 0;
			bc[0][i] = bc[1][i] = rnd() & 7;
		}
		local_irq_save(flags);
		for (k = 0; k < 6; k++) {
			u32 cx = rnd() & 0x1ff, cy = rnd() & 0x1ff, thr = rnd() & 0x7fff;
			u32 id = 1 + (rnd() % 60), cc = rnd() & 7;

			t31_awb_assign_c(zr, zb, cx, cy, thr, id, cc, bj[0], bc[0]);
			t31_awb_assign(2, zr, zb, cx, cy, thr, id, cc, bj[1], bc[1]);
		}
		local_irq_restore(flags);
		if (ri_hits)
			return -1;
		if (memcmp(bj[0], bj[1], sizeof(u32) * T31_AWB_ZONES) ||
		    memcmp(bc[0], bc[1], sizeof(u32) * T31_AWB_ZONES)) {
			bad++;
			pr_err("t31_mxu2_probe: assign mismatch n=%d\n", n);
		}
		cond_resched();
	}
	return bad;
}

static int probe_thread(void *arg)
{
	u64 nc = 0, nv = 0;
	int b1, b2 = -2;
	u32 cfg1 = read_c0_config1();

	cppsr_before = readl((void __iomem *)0xb0000034);
	sku = (readl((void __iomem *)0xb3540230) >> 24) & 0xff;
	e3a = readl((void __iomem *)0xb3540238) >> 16;
	pr_info("t31_mxu2_probe: CPPSR 0x%x, eFuse SKU(0x33) 0x%02x, [0x3a..3b] 0x%04x, cpu_has_mxu_v2 %d, Config1 0x%08x (C2=%d)\n",
		cppsr_before, sku, e3a, !!cpu_has_mxu_v2, cfg1, !!(cfg1 & 1));
	register_die_notifier(&die_nb);
	guard = true;
	pr_info("t31_mxu2_probe: stage 1 (lu1q/addw/su1q)\n");
	b1 = stage1();
	if (b1 >= 0) {
		pr_info("t31_mxu2_probe: stage 1 %s (%d wrong lanes)\n", b1 ? "FAILED" : "ok", b1);
		if (!b1) {
			pr_info("t31_mxu2_probe: stage 2 (AWB kernels)\n");
			b2 = stage2(&nc, &nv);
		}
	}
	guard = false;
	unregister_die_notifier(&die_nb);
	cppsr_after = readl((void __iomem *)0xb0000034);
	if (ri_hits || b1 < 0 || b2 == -1) {
		pr_err("t31_mxu2_probe: RESULT TRAP: %d reserved-instruction hits, MXU2 not executable\n", ri_hits);
		result = 2;
	} else if (b1 || b2 > 0) {
		pr_err("t31_mxu2_probe: RESULT MISMATCH stage1 %d stage2 %d\n", b1, b2);
		result = 1;
	} else {
		pr_info("t31_mxu2_probe: RESULT BITEXACT (stage1 ok, stage2 0 mismatches); 96 sweeps C %llu us MXU2 %llu us; CPPSR after 0x%x\n",
			div_u64(nc, 1000), div_u64(nv, 1000), cppsr_after);
		result = 0;
	}
	complete(&done);
	return 0;
}

static int __init probe_init(void)
{
	struct task_struct *t = kthread_run(probe_thread, NULL, "mxu2probe");

	if (IS_ERR(t))
		return PTR_ERR(t);
	wait_for_completion(&done);
	return -ECANCELED;	/* do not stay loaded */
}
module_init(probe_init);
MODULE_LICENSE("GPL");
