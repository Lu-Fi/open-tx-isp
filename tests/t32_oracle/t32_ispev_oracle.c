// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our readable core event queue (driver/t32/tx_isp_t32_ispev.h) against the
 * vendor functions isp_core_event_init, _set_cb, _work_empty, _push, _exit and _process from
 * Matt Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc, built for the host with env.c).
 *
 * The vendor tables (d9d30 .. d9e78 + 4 queues) are one block in the vendor layout.  Each case
 * builds a random state twice (random filler, channels initialised or not, callback row of
 * channel 0, 0..85 queued events, so full / empty / partial queues) and runs one function on
 * each side.  Kernel calls are recorder stubs with scripted results (irq flags, wait result
 * incl. -ERESTARTSYS and timeout, callback results); compared are the return value, the call log,
 * the completion count and every byte of the block and of the event buffer.
 * process runs on channel 0 only: for ch > 0 the vendor's callback rows are other tables.
 *
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_ispev.h"

u64 isp_core_event_init(u32 a0, u32 a1, u32 a2, u32 a3);
u64 isp_core_event_set_cb(u32 a0, u32 a1, u32 a2, u32 a3);
u64 isp_core_event_work_empty(u32 a0, u32 a1, u32 a2, u32 a3);
u64 isp_core_event_push(u32 a0, u32 a1, u32 a2, u32 a3);
u64 isp_core_event_exit(u32 a0, u32 a1, u32 a2, u32 a3);
u64 isp_core_event_process(u32 a0, u32 a1, u32 a2, u32 a3);
u64 private_complete(u32 a0, u32 a1, u32 a2, u32 a3);
extern u8 t32o_op5_evq[0x2500], t32_small_bss[64];
extern u32 t32_data_c69b0[1];

#define BSZ	sizeof(t32o_op5_evq)
#define MAXL	64
#define NREC	3

static u32 rs = 0xe7e47a5u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}
static u32 ss;
static u32 srnd(void)
{
	ss ^= ss << 13; ss ^= ss >> 17; ss ^= ss << 5;
	return ss;
}

struct lg {
	unsigned int n;
	u32 e[MAXL][7];
};
static struct lg lg, lg_ours;
static int quiet, printf_line;

static void L(u32 id, u32 a, u32 b, u32 c, u32 d, u32 e, u32 f)
{
	if (quiet)
		return;
	if (lg.n < MAXL) {
		u32 *p = lg.e[lg.n];

		p[0] = id; p[1] = a; p[2] = b; p[3] = c; p[4] = d; p[5] = e; p[6] = f;
	}
	lg.n++;
}

u64 __init_waitqueue_head(u32 wq, u32 name, u32 key, u32 a3)
{
	(void)a3;
	L(1, wq, name == T32O_PTR(t32_data_c69b0), key == T32O_PTR(t32_small_bss), 0, 0, 0);
	return 0;
}
u64 private_spin_lock_init(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(2, a0, 0, 0, 0, 0, 0); return 0; }
u64 __private_spin_lock_irqsave(u32 lock, u32 flagsp, u32 a2, u32 a3)
{
	u32 f = quiet ? 0 : srnd();

	(void)a2; (void)a3;
	L(3, lock, 0, 0, 0, 0, 0);
	memcpy(T32O_HOST(flagsp), &f, 4);
	return 0;
}
u64 private_spin_unlock_irqrestore(u32 lock, u32 flags, u32 a2, u32 a3) { (void)a2; (void)a3; L(4, lock, flags, 0, 0, 0, 0); return 0; }
u64 private_wait_for_completion_timeout(u32 c, u32 t, u32 a2, u32 a3)
{
	u32 k = srnd() % 8;

	(void)a2; (void)a3;
	L(5, c, t, 0, 0, 0, 0);
	return k == 0 ? (u32)T32_ERESTARTSYS : k == 1 ? 0 : k < 6 ? 1 + srnd() % 20 : srnd();
}
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5)
{
	(void)a1; (void)a2;
	/* line 148: a4 is the callback's 5th argument slot (callee-owned in o32, undefined), not compared */
	L(6, a0, printf_line ? a3 : 0, printf_line && a3 == 152 ? a4 : 0,
	  printf_line && a3 == 148 ? a5 : 0, 0, 0);
	return 0;
}
static u64 rec(u32 id, u32 a0, u32 a1, u32 a2, u32 a3, u32 a4)
{
	L(7, id, a0, a1, a2, a3, a4);
	return srnd() % 10 < 7 ? 0 : srnd() | 1;
}
static u64 rec0(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4) { return rec(0, a0, a1, a2, a3, a4); }
static u64 rec1(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4) { return rec(1, a0, a1, a2, a3, a4); }
static u64 rec2(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4) { return rec(2, a0, a1, a2, a3, a4); }
static u64 (*const recs[NREC])(u32, u32, u32, u32, u32) = { rec0, rec1, rec2 };

/* ---- our hooks ---- */
static void o_wq(u32 wq) { __init_waitqueue_head(wq, T32O_PTR(t32_data_c69b0), T32O_PTR(t32_small_bss), 0); }
static void o_sl(u32 l) { private_spin_lock_init(l, 0, 0, 0); }
static u32 o_lock(u32 l)
{
	static u32 f;	/* 32-bit address also when the build runs on the main stack */

	__private_spin_lock_irqsave(l, T32O_PTR(&f), 0, 0);
	return f;
}
static void o_unlock(u32 l, u32 f) { private_spin_unlock_irqrestore(l, f, 0, 0); }
static void o_cmp(u32 c) { if (!quiet) private_complete(c, 0, 0, 0); }
static int o_wait(u32 c, u32 t) { return (int)private_wait_for_completion_timeout(c, t, 0, 0); }
static int o_call5(u32 fn, u32 a0, u32 a1, u32 a2, u32 a3, u32 a4)
{
	return (int)((u64 (*)(u32, u32, u32, u32, u32))(uintptr_t)fn)(a0, a1, a2, a3, a4);
}
static void o_log(u32 lvl, u32 line, u32 a4, u32 a5) { isp_printf(lvl, 0, 0, line, a4, a5); }
static const struct t32_ev_ops ops = { o_wq, o_sl, o_lock, o_unlock, o_cmp, o_wait, o_call5, o_log };
static struct t32_ev_glob g;

static u32 evbuf[16] __attribute__((aligned(16)));
static u8 snap[sizeof(t32o_op5_evq)], snap_bss[64];
static u32 snap_ev[16], snap_cn, snap_ca;

static void build(u32 seed, int uninit)
{
	u32 save = rs, i, ch, n;

	rs = seed | 1;
	for (i = 0; i < BSZ; i += 4) {
		u32 v = rnd();

		memcpy(t32o_op5_evq + i, &v, 4);
	}
	for (i = 0; i < 64; i++)
		t32_small_bss[i] = rnd();
	for (i = 0; i < 16; i++)
		evbuf[i] = rnd() % 4 ? rnd() % 16 : rnd();
	g.drop_proc = T32O_PTR(t32o_op5_evq);
	g.drop_push = g.drop_proc + 4;
	g.cbs = g.drop_proc + 8;
	g.q = g.drop_proc + 0x148;
	g.lock = T32O_PTR(t32_small_bss);
	quiet = 1;
	/* callback row of channel 0 (all 80 words: node types reach beyond 10) */
	for (i = 0; i < T32_EVQ_CB_ROW; i++)
		t32_ic_w32(g.cbs, i * 4, rnd() % 4 ? T32O_PTR(recs[rnd() % NREC]) : 0);
	for (ch = 0; ch < 4; ch++) {
		if (uninit && rnd() % 5 == 0)
			continue;	/* an uninitialised queue: only init / set_cb / work_empty run then */
		t32_isp_core_event_init(&ops, &g, ch);
		n = rnd() % 6 == 0 ? 80 + rnd() % 6 : rnd() % 8 == 0 ? 0 : rnd() % 80;
		if (ch && n > 80)
			n = 80;	/* a push drop of channel ch > 0 counts in the callback row of channel 0 (vendor layout) */
		for (i = 0; i < n; i++) {
			static u32 ev[7];
			u32 k;

			for (k = 0; k < 7; k++)
				ev[k] = k == 2 ? rnd() % 16 : rnd();
			t32_isp_core_event_push(&ops, &g, ch, T32O_PTR(ev));
		}
	}
	quiet = 0;
	memset(&lg, 0, sizeof(lg));
	t32o_complete_n = t32o_complete_arg = 0;
	ss = (seed ^ 0x5bd1e995u) | 1;
	rs = save;
}

static void take_ours(void)
{
	memcpy(snap, t32o_op5_evq, BSZ);
	memcpy(snap_bss, t32_small_bss, 64);
	memcpy(snap_ev, evbuf, sizeof(evbuf));
	lg_ours = lg;
	snap_cn = t32o_complete_n;
	snap_ca = t32o_complete_arg;
}

static unsigned int compared, failed;
static void same(const char *what, u32 seed, int r_ours, int r_ref)
{
	unsigned int n = lg.n < MAXL ? lg.n : MAXL;

	compared++;
	if (r_ours == r_ref && lg.n == lg_ours.n && !memcmp(lg.e, lg_ours.e, n * sizeof(lg.e[0])) &&
	    !memcmp(snap, t32o_op5_evq, BSZ) && !memcmp(snap_bss, t32_small_bss, 64) &&
	    !memcmp(snap_ev, evbuf, sizeof(evbuf)) && snap_cn == t32o_complete_n && snap_ca == t32o_complete_arg)
		return;
	if (failed++ < 8)
		printf("t32_ispev_oracle: MISMATCH %s seed %08x: ret %d/%d log %u/%u\n",
		       what, seed, r_ours, r_ref, lg_ours.n, lg.n);
}

/* ours on the low stack (the exit event and the irq flags live on it) */
static u64 ours_tramp(u32 k, u32 a, u32 b, u32 c, u32 d)
{
	(void)d;
	switch (k) {
	case 0: return (u32)t32_isp_core_event_init(&ops, &g, a);
	case 1: return (u32)t32_isp_core_event_set_cb(&ops, &g, a, (int)b, c);
	case 2: return (u32)t32_isp_core_event_work_empty(&ops, &g, a);
	case 3: return (u32)t32_isp_core_event_push(&ops, &g, a, b);
	case 4: return (u32)t32_isp_core_event_exit(&ops, &g, a);
	default: return (u32)t32_isp_core_event_process(&ops, &g, a);
	}
}
#define OURS(k, a, b, c)	((int)(u32)t32o_call((t32o_fn)(void (*)(void))ours_tramp, (k), (a), (b), (c), 0))
#define REF(fn, a0, a1, a2)	((int)(u32)t32o_call((t32o_fn)(void (*)(void))(fn), (a0), (a1), (a2), rnd(), 0))

int main(void)
{
	static u64 (*const refs[6])(u32, u32, u32, u32) = {
		isp_core_event_init, isp_core_event_set_cb, isp_core_event_work_empty,
		isp_core_event_push, isp_core_event_exit, isp_core_event_process,
	};
	static const char *const names[6] = { "init", "set_cb", "work_empty", "push", "exit", "process" };
	unsigned int it;

	for (it = 0; it < 12000; it++) {
		u32 seed = rnd(), k = it % 6, ch = rnd() % 4 | (rnd() % 4 == 0 ? rnd() << 8 : 0), b = 0, c = 0;
		int r;

		printf_line = k != 1;
		if (k == 1) {	/* set_cb: types 0..9 and the refused / negative ones, rows of channel 0..3 */
			u32 t = rnd() % 5;

			b = t < 3 ? rnd() % 10 : t == 3 ? 10 + rnd() % 3 : (u32)-(int)(rnd() % 2);
			if ((int)b >= 10 && rnd() % 2)
				b = (rnd() | 0x10) & 0x7fffffffu;	/* a large negative type writes anywhere (vendor UB) */
			c = rnd();
		}
		if (k == 3)
			b = T32O_PTR(evbuf);
		if (k == 5)
			ch &= ~3u;	/* process: channel 0 only (low byte) */
		build(seed, k < 3);
		r = OURS(k, ch, b, c);
		take_ours();
		build(seed, k < 3);
		same(names[k], seed, r, REF(refs[k], ch, b, c));
	}
	printf("t32_ispev_oracle: %u cases compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
