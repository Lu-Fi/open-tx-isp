// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: the ISP core event queue of tx_isp_t32_event.h
 * (tisp_event_init, _push, _work_empty, _exit, _process) against the vendor
 * functions (Matt Davis' recovered C, PR #15 @ 2e2e06bc).  Stateful random
 * runs: per run one channel is initialised (node memory pre-filled with the
 * same garbage on both sides), then 600 random operations (push of random
 * events, process with scripted wait results 1 / 0 / -ERESTARTSYS / other,
 * work_empty with high channel bits, exit, re-init), queue full / empty
 * forced.  Compared after every operation: return value, both lists as node
 * order (links checked both ways), every node's type / word 3 / arguments,
 * the full / empty counters, the lock / unlock sequence with the flags
 * tokens, completion signals, printf count, handler calls (channel and
 * arguments).
 *
 * tisp_event_push is also an env.c stub (it records the event for other
 * oracles): the reference copy is renamed ref_tisp_event_push at build time
 * (fix_op11_event.sh), and tisp_event_exit is checked through that stub (the
 * recorded event is then pushed with ref_tisp_event_push).
 * Exit 1 on the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_event.h"

#define CHB 3872
u8 t32_data_dd7e0[T32_EV_CHANS * CHB] __attribute__((aligned(16), weak));	/* .bss: event queues */
u32 t32_data_dd6a0[4] __attribute__((aligned(16), weak));			/* .bss: process "empty" counters */
u32 t32_data_dd6b0[4] __attribute__((aligned(16), weak));			/* .bss: push "full" counters */
extern u8 t32_data_dd6c0[0x400], t32_small_bss[64];						/* env.c: handler table */
char t32_data_c849c[4] __attribute__((weak)) = "f", t32_data_c84a8[4] __attribute__((weak)) = "f",
     t32_data_bf2f0[4] __attribute__((weak)) = "f";

extern u64 tisp_event_init(u32, u32, u32, u32), ref_tisp_event_push(u32, u32, u32, u32),
	   tisp_event_work_empty(u32, u32, u32, u32), tisp_event_exit(u32, u32, u32, u32),
	   tisp_event_process(u32, u32, u32, u32);

static u32 rs = 0x51ed2701u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

/* hook log, the same shape for both sides: kind, a, b, ... */
#define LOGN 64
struct hlog { unsigned int n; u32 e[LOGN][11]; };
static struct hlog vl, ol;
static void hrec(struct hlog *l, u32 kind, const u32 *v, unsigned int nv)
{
	if (l->n < LOGN) {
		memset(l->e[l->n], 0, sizeof(l->e[0]));
		l->e[l->n][0] = kind;
		memcpy(&l->e[l->n][1], v, nv * 4);
	}
	l->n++;
}
enum { K_LINIT = 1, K_LOCK, K_UNLOCK, K_CINIT, K_COMPL, K_WAIT, K_PRINT, K_HANDLER };

static u32 vbase(void) { return T32O_PTR(t32_data_dd7e0); }
static u32 run_ch;		/* channel of the run (completion objects map to it) */
static u32 wait_ret;		/* scripted wait_for_completion_timeout result */
static u32 tok_v, tok_o;	/* flags tokens */

/* vendor OS stubs */
u64 __init_waitqueue_head(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u32 v[1] = { (a0 - vbase() - 4) / CHB };

	(void)a3;
	if ((a0 - vbase() - 4) % CHB || a1 != T32O_PTR(t32_data_c849c) || a2 != T32O_PTR(t32_small_bss))
		v[0] = 0xdead;
	hrec(&vl, K_CINIT, v, 1);
	return 0;
}
u64 private_spin_lock_init(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u32 v[1] = { a0 == T32O_PTR(t32_small_bss) };

	(void)a1; (void)a2; (void)a3;
	hrec(&vl, K_LINIT, v, 1);
	return 0;
}
u64 __private_spin_lock_irqsave(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u32 v[1] = { ++tok_v };

	(void)a2; (void)a3;
	if (a0 != T32O_PTR(t32_small_bss))
		v[0] = 0xdead;
	*(u32 *)T32O_HOST(a1) = tok_v;
	hrec(&vl, K_LOCK, v, 1);
	return 0;
}
u64 private_spin_unlock_irqrestore(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u32 v[1] = { a1 };

	(void)a2; (void)a3;
	if (a0 != T32O_PTR(t32_small_bss))
		v[0] = 0xdead;
	hrec(&vl, K_UNLOCK, v, 1);
	return 0;
}
u64 private_wait_for_completion_timeout(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u32 v[2] = { (a0 - vbase()) / CHB, a1 };

	(void)a2; (void)a3;
	if ((a0 - vbase()) % CHB)
		v[0] = 0xdead;
	hrec(&vl, K_WAIT, v, 2);
	return wait_ret;
}
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u32 v[1] = { a0 == 2 && a1 == T32O_PTR(t32_data_c84a8) && a2 == T32O_PTR(t32_data_bf2f0) && a3 == 107 };

	hrec(&vl, K_PRINT, v, 1);
	return 0;
}
/* private_complete is env.c's (t32o_complete_n / _arg) */

static u64 v_handler(u32 ch, u32 flags, u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6, u32 a7)
{
	u32 v[9] = { ch, a0, a1, a2, a3, a4, a5, a6, a7 };

	(void)flags;	/* left-over lock flags register in the vendor, not a parameter */
	hrec(&vl, K_HANDLER, v, 9);
	return rnd();
}

/* ours */
static void o_linit(void) { u32 v[1] = { 1 }; hrec(&ol, K_LINIT, v, 1); }
static void o_lock(unsigned long *f) { u32 v[1] = { ++tok_o }; *f = tok_o; hrec(&ol, K_LOCK, v, 1); }
static void o_unlock(unsigned long f) { u32 v[1] = { (u32)f }; hrec(&ol, K_UNLOCK, v, 1); }
static void o_cinit(u32 ch) { u32 v[1] = { ch }; hrec(&ol, K_CINIT, v, 1); }
static unsigned int o_complete_n;
static u32 o_complete_ch;
static void o_complete(u32 ch) { o_complete_n++; o_complete_ch = ch; }
static long o_wait(u32 ch, u32 t) { u32 v[2] = { ch, t }; hrec(&ol, K_WAIT, v, 2); return (long)(int)wait_ret; }
static void o_print(void) { u32 v[1] = { 1 }; hrec(&ol, K_PRINT, v, 1); }
static int o_handler(u32 ch, u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6, u32 a7)
{
	u32 v[9] = { ch, a0, a1, a2, a3, a4, a5, a6, a7 };

	hrec(&ol, K_HANDLER, v, 9);
	return (int)rnd();
}
static const struct t32_event_ops ops = {
	.lock_init = o_linit, .lock = o_lock, .unlock = o_unlock, .completion_init = o_cinit,
	.complete = o_complete, .wait = o_wait, .print_restart = o_print,
};
static struct t32_event_q q;

/* vendor list -> node indices (-1 on a broken link) */
static int vwalk(u32 ch, u32 head_off, int *idx)
{
	u32 b = vbase() + ch * CHB, h = b + head_off, p = h, nx;
	int n = 0;

	for (;;) {
		nx = *(u32 *)T32O_HOST(p);
		if (*(u32 *)T32O_HOST(nx + 4) != p)
			return -1;
		if (nx == h)
			return n;
		if (nx < b + 16 || nx >= b + 16 + 48 * T32_EV_NODES || (nx - b - 16) % 48 || n >= T32_EV_NODES)
			return -1;
		idx[n++] = (int)((nx - b - 16) / 48);
		p = nx;
	}
}
static int owalk(struct t32_ev_chan *c, struct t32_ev_link *h, int *idx)
{
	struct t32_ev_link *p = h, *nx;
	int n = 0;

	for (;;) {
		nx = p->next;
		if (nx->prev != p)
			return -1;
		if (nx == h)
			return n;
		if ((struct t32_ev_node *)nx < c->node || (struct t32_ev_node *)nx >= c->node + T32_EV_NODES ||
		    n >= T32_EV_NODES)
			return -1;
		idx[n++] = (int)((struct t32_ev_node *)nx - c->node);
		p = nx;
	}
}

static int check(u32 ch, const char *what, unsigned int c, unsigned int op, u32 rv, u32 ro)
{
	int vi[T32_EV_NODES], oi[T32_EV_NODES], nv, no, l;
	unsigned int k;
	const char *bad = NULL;

	if (rv != ro)
		bad = "return";
	for (l = 0; l < 2 && !bad; l++) {
		nv = vwalk(ch, l ? 3864 : 3856, vi);
		no = owalk(&q.ch[ch], l ? &q.ch[ch].free : &q.ch[ch].busy, oi);
		if (nv < 0 || no < 0 || nv != no || memcmp(vi, oi, nv * sizeof(int)))
			bad = l ? "free list" : "busy list";
	}
	for (k = 0; k < T32_EV_NODES && !bad; k++) {
		const u32 *vn = T32O_HOST(vbase() + ch * CHB + 16 + 48 * k);
		const struct t32_ev_node *on = &q.ch[ch].node[k];

		if (vn[2] != on->type || vn[3] != on->rsvd1 || memcmp(vn + 4, on->arg, 32))
			bad = "node";
	}
	if (!bad && (t32_data_dd6a0[ch] != q.process_empty[ch] || t32_data_dd6b0[ch] != q.push_full[ch]))
		bad = "counters";
	if (!bad && (vl.n != ol.n || memcmp(&vl, &ol, sizeof(vl))))
		bad = "hook log";
	if (!bad && (t32o_complete_n != o_complete_n ||
		     (o_complete_n && t32o_complete_arg != vbase() + o_complete_ch * CHB)))
		bad = "completion";
	if (bad) {
		printf("t32_event_oracle: run %u op %u %s (ch %u): %s differs (return vendor 0x%x ours 0x%x, hooks %u/%u)\n",
		       c, op, what, ch, bad, rv, ro, vl.n, ol.n);
		return 1;
	}
	return 0;
}

static void prefill(u32 ch)
{
	unsigned int k, w;

	for (k = 0; k < T32_EV_NODES; k++) {
		u32 *vn = T32O_HOST(vbase() + ch * CHB + 16 + 48 * k);
		struct t32_ev_node *on = &q.ch[ch].node[k];

		for (w = 0; w < 12; w++)
			vn[w] = rnd();
		on->type = vn[2];
		on->rsvd1 = vn[3];
		memcpy(on->arg, vn + 4, 32);
	}
}

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 1500;
	unsigned int c, op, k, cmp = 0, handled = 0, full = 0, empty = 0;
	u32 *vtab = (u32 *)t32_data_dd6c0;

	q.ops = &ops;
	for (c = 0; c < n; c++) {
		u32 ch = rnd() % T32_EV_CHANS, rv, ro;
		unsigned int bias = rnd() % 3;	/* 0: push heavy (fills), 1: process heavy, 2: balanced */

		t32o_reset();
		memset(t32_data_dd7e0, 0x5a, sizeof(t32_data_dd7e0));
		memset(&q.ch, 0x5a, sizeof(q.ch));
		for (k = 0; k < 4; k++) {
			t32_data_dd6a0[k] = q.process_empty[k] = rnd();
			t32_data_dd6b0[k] = q.push_full[k] = rnd();
		}
		for (k = 0; k < T32_EV_CHANS * T32_EV_TYPES; k++) {
			int on = rnd() % 3 != 0;

			vtab[k] = on ? T32O_PTR((void *)v_handler) : 0;
			q.handler[k] = on ? o_handler : NULL;
		}
		run_ch = ch;
		prefill(ch);
		for (op = 0; op < 600; op++) {
			unsigned int kind = op == 0 ? 0 : rnd() % 100;
			const char *what;

			memset(&vl, 0, sizeof(vl));
			memset(&ol, 0, sizeof(ol));
			t32o_complete_n = o_complete_n = 0;
			wait_ret = 1;
			if (kind == 0 || kind == 99) {		/* (re-)init */
				what = "init";
				if (kind)
					prefill(ch);
				rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_event_init, ch, rnd(), rnd(), rnd(), 0);
				ro = (u32)t32_event_init(&q, ch);
			} else if (kind < (bias == 0 ? 60 : bias == 1 ? 25 : 45)) {	/* push */
				struct t32_event ev;
				u32 *vev = t32o_alloc(48), maxt = T32_EV_CHANS * T32_EV_TYPES - ch * T32_EV_TYPES;

				what = "push";
				for (k = 0; k < 12; k++)
					vev[k] = rnd();
				vev[2] = rnd() % 8 ? rnd() % T32_EV_TYPES : rnd() % maxt;
				memcpy(&ev, vev, 48);
				rv = (u32)t32o_call((t32o_fn)(void (*)(void))ref_tisp_event_push, ch, T32O_PTR(vev), rnd(), rnd(), 0);
				ro = (u32)t32_event_push(&q, ch, &ev);
				full += ro != 0;
			} else if (kind < 90) {			/* process */
				u32 r = rnd() % 10;

				what = "process";
				wait_ret = r < 7 ? 1 : r == 7 ? 0 : r == 8 ? (u32)-512 : rnd() | 2;
				rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_event_process, ch, rnd(), rnd(), rnd(), 0);
				ro = (u32)t32_event_process(&q, ch);
				empty += ro != 0;
			} else if (kind < 96) {			/* work_empty, high channel bits */
				u32 a = ch | (rnd() % 4 ? 0 : rnd() << 8);

				what = "work_empty";
				rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_event_work_empty, a, rnd(), rnd(), rnd(), 0);
				ro = (u32)t32_event_work_empty(&q, a);
			} else {				/* exit: through the env.c push stub, then replayed */
				u32 *vev = t32o_alloc(48);

				what = "exit";
				t32o_ev_n = 0;
				rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_event_exit, ch, rnd(), rnd(), rnd(), 0);
				if (t32o_ev_n != 1 || t32o_ev_ch != ch || t32o_ev_w[2] != 0) {
					printf("t32_event_oracle: run %u op %u exit: pushed %u events, ch %u type %u\n",
					       c, op, t32o_ev_n, t32o_ev_ch, t32o_ev_w[2]);
					return 1;
				}
				memset(vev, 0, 48);	/* the vendor's other words: its stack (zero in the recovered C) */
				t32o_call((t32o_fn)(void (*)(void))ref_tisp_event_push, ch, T32O_PTR(vev), 0, 0, 0);
				ro = (u32)t32_event_exit(&q, ch);
			}
			for (k = 0; k < vl.n && k < LOGN; k++)
				handled += vl.e[k][0] == K_HANDLER;
			if (check(ch, what, c, op, rv, ro))
				return 1;
			cmp++;
		}
	}
	printf("t32_event_oracle: %u operations compared (%u runs; %u handler calls, %u full pushes, %u empty/failed processes), 0 mismatches\n",
	       cmp, n, handled, full, empty);
	return 0;
}
