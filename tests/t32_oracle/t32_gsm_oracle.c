// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our readable GSM port (driver/t32/tx_isp_t32_gsm.h)
 * against the unmodified vendor functions tisp_gsm_* from Matt Davis'
 * mechanical C recovery (ref/, PR #15 @ 2e2e06bc).
 *
 * The vendor data objects t32_data_d6e00..d7e28 and e2900/e2910/e2d10 are
 * contiguous in the vendor .bss; here they are aliases into one arena with
 * canary frames, so the whole state is compared byte for byte (ours is a
 * second arena of the same type).  Compared: every register write (address,
 * value, order), every register read (order), pushed event, return values
 * (where defined by the port), all state bytes and canaries.  Stubs for
 * locks / printf / irq set are weak: env.c definitions win when present.
 * Not compared: return values of read_ram and wake_up (they are the result
 * of the last system_reg_write stub), tisp_event_set_cb registration.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_gsm.h"

#define MAXW	16
#define RDMAX	1200

struct arena {
	u8 pre[64];
	struct t32_gsm_ctx ctx;
	u8 mid[64];
	struct t32_gsm_ram ram;
	u8 post[64];
};

_Static_assert(offsetof(struct arena, ctx) == 64, "ctx");
_Static_assert(offsetof(struct arena, ctx.api) == 76, "api");
_Static_assert(offsetof(struct arena, ctx.api.zone) == 1100, "zone");
_Static_assert(offsetof(struct arena, ctx.api.bound) == 1120, "bound");
_Static_assert(offsetof(struct arena, ctx.a) == 1128, "a");
_Static_assert(offsetof(struct arena, ctx.b) == 2152, "b");
_Static_assert(offsetof(struct arena, ctx.c) == 3176, "c");
_Static_assert(offsetof(struct arena, ctx.cfg) == 4200, "cfg");
_Static_assert(offsetof(struct arena, ram) == 4268, "ram");
_Static_assert(offsetof(struct arena, ram.ram_b) == 4284, "ram_b");
_Static_assert(offsetof(struct arena, ram.ram_a) == 5308, "ram_a");

struct arena t32_gsm_arena __attribute__((aligned(16)));
static struct arena oa;

/* vendor symbols as aliases into the arena */
__asm__(
".globl t32_data_d6e00\n.set t32_data_d6e00, t32_gsm_arena+64\n"
".globl t32_data_d6e04\n.set t32_data_d6e04, t32_gsm_arena+68\n"
".globl t32_data_d6e08\n.set t32_data_d6e08, t32_gsm_arena+72\n"
".globl t32_data_d6e0c\n.set t32_data_d6e0c, t32_gsm_arena+76\n"
".globl t32_data_d720c\n.set t32_data_d720c, t32_gsm_arena+1100\n"
".globl t32_data_d7220\n.set t32_data_d7220, t32_gsm_arena+1120\n"
".globl t32_data_d7228\n.set t32_data_d7228, t32_gsm_arena+1128\n"
".globl t32_data_d7628\n.set t32_data_d7628, t32_gsm_arena+2152\n"
".globl t32_data_d7a28\n.set t32_data_d7a28, t32_gsm_arena+3176\n"
".globl t32_data_d7e28\n.set t32_data_d7e28, t32_gsm_arena+4200\n"
".globl t32_data_e2900\n.set t32_data_e2900, t32_gsm_arena+4268\n"
".globl t32_data_e2910\n.set t32_data_e2910, t32_gsm_arena+4284\n"
".globl t32_data_e2d10\n.set t32_data_e2d10, t32_gsm_arena+5308\n"
);

/* weak glue: env.c wins when it defines the same names */
#define WEAK __attribute__((weak))
u8 t32_small_bss[64] WEAK __attribute__((aligned(16)));
u8 t32_data_dd6c0[1024] WEAK __attribute__((aligned(16)));
/* printf format string of the reference, only passed to isp_printf */
u8 t32_data_ca288[64] WEAK __attribute__((aligned(16)));
u8 t32_data_ca224[64] WEAK __attribute__((aligned(16)));
u8 t32_data_ca244[64] WEAK __attribute__((aligned(16)));
u8 t32_data_ca268[64] WEAK __attribute__((aligned(16)));
u8 t32_data_ca274[64] WEAK __attribute__((aligned(16)));
u8 t32_data_bfe70[64] WEAK __attribute__((aligned(16)));
u8 t32_data_bfe88[64] WEAK __attribute__((aligned(16)));
u64 private_spin_lock_init(u32 a0, u32 a1, u32 a2, u32 a3);
u64 __private_spin_lock_irqsave(u32 a0, u32 a1, u32 a2, u32 a3);
u64 private_spin_unlock_irqrestore(u32 a0, u32 a1, u32 a2, u32 a3);
u64 system_irq_func_set(u32 a0, u32 a1, u32 a2, u32 a3);
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_event_push(u32 a0, u32 a1, u32 a2, u32 a3);
WEAK u64 private_spin_lock_init(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 0; }
WEAK u64 __private_spin_lock_irqsave(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 0; }
WEAK u64 private_spin_unlock_irqrestore(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 0; }
WEAK u64 system_irq_func_set(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 0; }
WEAK u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; t32o_log.printfs++; return 0; }
WEAK u64 tisp_event_push(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	t32o_ev_n++;
	t32o_ev_ch = a0;
	memcpy(t32o_ev_w, T32O_HOST(a1), sizeof(t32o_ev_w));
	return 0;
}

extern u64 tisp_gsm_irq_frmae_done(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_gsm_process(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_gsm_read_ram(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_gsm_interrupt_hist(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_gsm_init(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_gsm_api_set(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_gsm_api_get(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_gsm_wake_up(u32 a0, u32 a1, u32 a2, u32 a3);

static u32 rs = 0x6d5a1fu;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static unsigned int compared;

/* register read model shared by both sides */
static u32 rd_rs, rd_status, rd_n, rd_log[RDMAX], rd_seed;
static u32 hook(u32 reg)
{
	if (rd_n < RDMAX)
		rd_log[rd_n] = reg;
	rd_n++;
	if (reg == 0x19044)
		return rd_status;
	rd_rs = rd_rs * 1664525u + 1013904223u;
	return rd_rs ^ (rd_rs >> 15) ^ (rd_rs << 9);
}

/* event capture of our side */
static unsigned int our_evn;
static u8 our_evch;
static struct t32_gsm_event our_ev;
static void push(u8 ch, const struct t32_gsm_event *ev)
{
	our_evn++;
	our_evch = ch;
	our_ev = *ev;
}

/* reference results */
static struct { u32 reg, val; } rw[MAXW];
static unsigned int rn, rrn;
static u32 rrl[RDMAX], ev0, ref_evn, ref_evch, ref_evw[3];
static int extra_bad;
static int exp_pf = -1;
static unsigned int pf0, ref_pf;

static void state_random(int mode)
{
	size_t i;
	u8 *p = (u8 *)&oa.ctx;

	memset(&oa, 0xa5, sizeof(oa));
	for (i = 0; i < sizeof(oa.ctx); i++)
		p[i] = (u8)rnd();
	for (i = 0; i < 4; i++)
		oa.ram.irq[i] = rnd();
	for (i = 0; i < 256; i++) {
		u32 a = rnd(), b = rnd();

		if (mode == 1) {
			a &= 0x3fffffu; b &= 0x3fffffu;
		} else if (mode == 2) {
			a &= 0xffu; b &= 0xffu;
		}
		oa.ram.ram_a[i] = a;
		oa.ram.ram_b[i] = b;
	}
	rd_seed = rnd();
	rd_status = 1;
	extra_bad = 0;
	exp_pf = -1;
}

static void state_commit(void)
{
	memcpy(&t32_gsm_arena, &oa, sizeof(oa));
}

static void ref_prepare(void)
{
	t32o_reset();
	t32o_rd_hook = hook;
	rd_rs = rd_seed;
	rd_n = 0;
	ev0 = t32o_ev_n;
	pf0 = t32o_log.printfs;
}

static u64 ref_call(void (*fn)(void), u32 a0, u32 a1, u32 a2, u32 a3, u32 a4)
{
	u64 r = t32o_call((t32o_fn)fn, a0, a1, a2, a3, a4);

	rn = t32o_log.n;
	ref_pf = t32o_log.printfs - pf0;
	if (rn > MAXW) {
		printf("t32_gsm_oracle: too many ref writes %u\n", rn);
		exit(1);
	}
	memcpy(rw, t32o_log.w, rn * sizeof(rw[0]));
	rrn = rd_n;
	memcpy(rrl, rd_log, (rd_n < RDMAX ? rd_n : RDMAX) * sizeof(u32));
	ref_evn = t32o_ev_n - ev0;
	ref_evch = t32o_ev_ch;
	memcpy(ref_evw, t32o_ev_w, sizeof(ref_evw));
	return r;
}

static void our_begin(void)
{
	rd_rs = rd_seed;
	rd_n = 0;
	our_evn = 0;
}

static void verify(const char *op, int retok, u64 rr, u32 orv, unsigned int on,
		   const struct t32_front_write *ow)
{
	unsigned int i;
	const char *why = NULL;

	compared++;
	if (retok && (u32)rr != orv)
		why = "return value";
	else if (rn != on)
		why = "write count";
	else {
		for (i = 0; i < on; i++)
			if (rw[i].reg != ow[i].reg || rw[i].val != ow[i].val) {
				why = "write";
				break;
			}
	}
	if (!why && rrn != rd_n)
		why = "read count";
	if (!why)
		for (i = 0; i < rrn && i < RDMAX; i++)
			if (rrl[i] != rd_log[i]) {
				why = "read order";
				break;
			}
	if (!why && memcmp(&oa, &t32_gsm_arena, sizeof(oa)))
		why = "state bytes";
	if (!why && ref_evn != our_evn)
		why = "event count";
	if (!why && ref_evn == 1 &&
	    (ref_evch != our_evch || ref_evw[0] != our_ev.rsvd0 ||
	     ref_evw[1] != our_ev.rsvd1 || ref_evw[2] != our_ev.type))
		why = "event";
	if (!why && extra_bad)
		why = "buffer";
	if (!why && exp_pf >= 0 && ref_pf != (unsigned)exp_pf)
		why = "printf count";
	if (why) {
		printf("MISMATCH %s: %s (writes ref %u ours %u, ref ret %08x ours %08x)\n",
		       op, why, rn, on, (u32)rr, orv);
		for (i = 0; i < rn && i < on; i++)
			printf("  #%u ref %05x=%08x ours %05x=%08x\n", i, rw[i].reg, rw[i].val,
			       ow[i].reg, ow[i].val);
		exit(1);
	}
}

static void case_irq(void)
{
	struct t32_front_write ow[MAXW];
	u32 ch = rnd() & 3, r;
	u64 rr;

	state_random(0);
	state_commit();
	ref_prepare();
	exp_pf = 0;
	rr = ref_call((void (*)(void))tisp_gsm_irq_frmae_done, ch, 0, 0, 0, 0);
	our_begin();
	r = t32_gsm_irq_frame_done(&oa.ram, (u8)ch);
	verify("irq_frame_done", 1, rr, r, 0, ow);
}

static void case_process(unsigned int c)
{
	struct t32_front_write ow[MAXW] = { { 0, 0 } };
	u8 *bd;
	u64 rr;
	u32 r, a, b, d, k;
	unsigned int i;

	state_random(c % 3);
	bd = oa.ctx.api.bound;
	switch ((c / 3) % 8) {
	case 1:
		a = rnd() & 0xff; b = rnd() & 0xff; d = rnd() & 0xff; k = rnd() & 0xff;
		bd[0] = a; bd[1] = a + (b % (256 - a)); bd[2] = bd[1] + (d % (256 - bd[1]));
		bd[3] = bd[2] + (k % (256 - bd[2]));
		break;
	case 2: bd[0] = bd[1] = bd[2] = bd[3] = 0; break;
	case 3: bd[0] = bd[1] = bd[2] = bd[3] = 255; break;
	case 4: bd[0] = 10; bd[1] = 20; bd[2] = 30; bd[3] = 255; break;
	case 5: bd[0] = 200; bd[1] = 150; bd[2] = 100; bd[3] = 50; break;
	case 6: bd[0] = 0; bd[1] = 0; bd[2] = 0; bd[3] = 255; break;
	case 7: bd[0] = 64; bd[1] = 64; bd[2] = 128; bd[3] = 128; break;
	default: break;
	}
	if (c % 4 == 0) {
		oa.ctx.pixels = (rnd() % 100000) + 1;
		oa.ctx.scale = T32_GSM_SCALE_ONE / oa.ctx.pixels;
		oa.ctx.scale_hi = 0;
	}
	(void)i;
	{
		struct t32_gsm_ctx tmp = oa.ctx;
		u32 sum = 0;

		t32_gsm_process_sum(&tmp, &oa.ram, &sum);
		if (c % 2 == 0)
			oa.ctx.pixels = sum;
		exp_pf = (sum != oa.ctx.pixels) ? 3 : 0;
	}
	state_commit();
	ref_prepare();
	rr = ref_call((void (*)(void))tisp_gsm_process, 0, 0, 0, 0, 0);
	our_begin();
	r = (u32)t32_gsm_process(&oa.ctx, &oa.ram);
	verify("process", 1, rr, r, 0, ow);
}

static void case_read_ram(void)
{
	struct t32_front_write ow[MAXW];
	unsigned int n = 0;
	u64 rr;

	state_random(0);
	state_commit();
	ref_prepare();
	rr = ref_call((void (*)(void))tisp_gsm_read_ram, 0, 0, 0, 0, 0);
	our_begin();
	t32_gsm_read_ram(&oa.ram, hook, ow, &n);
	verify("read_ram", 0, rr, 0, n, ow);
}

static void case_hist(unsigned int c)
{
	static const u32 st[5] = { 1, 0, 2, 0xffffffffu, 1 };
	struct t32_front_write ow[MAXW];
	unsigned int n = 0;
	u32 ch = rnd() & 3, r;
	u64 rr;

	state_random(0);
	rd_status = st[c % 5];
	state_commit();
	ref_prepare();
	rr = ref_call((void (*)(void))tisp_gsm_interrupt_hist, ch, 0, 0, 0, 0);
	our_begin();
	r = t32_gsm_interrupt_hist(&oa.ram, (u8)ch, hook, push, ow, &n);
	verify("interrupt_hist", 1, rr, r, n, ow);
}

static void case_init(unsigned int c)
{
	struct t32_front_write ow[MAXW];
	unsigned int n = 0;
	u32 w = rnd(), h = rnd(), ch = rnd() & 3, r, *wh;
	u64 rr;

	switch (c % 7) {
	case 0: w = 0; break;
	case 1: h = 0; break;
	case 2: w = 1; h = 1; break;
	case 3: w = 0x10000; h = 0x10000; break;
	case 4: w = 0xffffffffu; h = 2; break;
	case 5: w = (rnd() % 2000) + 1; h = (rnd() % 2000) + 1; break;
	default: w &= 0xffff; h &= 0xffff; break;
	}
	state_random(0);
	state_commit();
	ref_prepare();
	wh = t32o_alloc(8);
	wh[0] = w;
	wh[1] = h;
	rr = ref_call((void (*)(void))tisp_gsm_init, ch, T32O_PTR(wh), 0, 0, 0);
	our_begin();
	r = (u32)t32_gsm_init(&oa.ctx, w, h, ow, &n);
	verify("init", 1, rr, r, n, ow);
}

static void case_wake(void)
{
	struct t32_front_write ow[MAXW];
	unsigned int n = 0;
	u64 rr;

	state_random(0);
	state_commit();
	ref_prepare();
	rr = ref_call((void (*)(void))tisp_gsm_wake_up, rnd() & 3, 0, 0, 0, 0);
	our_begin();
	t32_gsm_wake_up(&oa.ctx, ow, &n);
	verify("wake_up", 0, rr, 0, n, ow);
}

static void case_api_set(void)
{
	struct t32_front_write ow[MAXW];
	static struct t32_gsm_api src;
	u8 *sb, *p = (u8 *)&src;
	size_t i;
	u64 rr;
	u32 r;

	for (i = 0; i < sizeof(src); i++)
		p[i] = (u8)rnd();
	state_random(0);
	state_commit();
	ref_prepare();
	sb = t32o_alloc(sizeof(src));
	memcpy(sb, &src, sizeof(src));
	rr = ref_call((void (*)(void))tisp_gsm_api_set, 0, T32O_PTR(sb), 0, 0, 0);
	extra_bad = memcmp(sb, &src, sizeof(src)) != 0;
	our_begin();
	r = (u32)t32_gsm_api_set(&oa.ctx, &src);
	verify("api_set", 1, rr, r, 0, ow);
}

static void case_api_get(void)
{
	struct t32_front_write ow[MAXW];
	static struct { u8 pre[64]; struct t32_gsm_api a; u8 post[64]; } oo;
	u8 *rb;
	u64 rr;
	u32 r;

	state_random(0);
	state_commit();
	memset(&oo, 0x5a, sizeof(oo));
	ref_prepare();
	rb = t32o_alloc(sizeof(oo));
	memset(rb, 0x5a, sizeof(oo));
	if (rnd() & 1) {
		/* api_get after a real process call, both sides */
		rr = ref_call((void (*)(void))tisp_gsm_process, 0, 0, 0, 0, 0);
		t32_gsm_process(&oa.ctx, &oa.ram);
	}
	rr = ref_call((void (*)(void))tisp_gsm_api_get, 0, T32O_PTR(rb + 64), 0, 0, 0);
	our_begin();
	r = (u32)t32_gsm_api_get(&oa.ctx, &oo.a);
	extra_bad = memcmp(rb, &oo, sizeof(oo)) != 0;
	verify("api_get", 1, rr, r, 0, ow);
}

int main(void)
{
	unsigned int c, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 3000;

	for (c = 0; c < n; c++) {
		case_irq();
		case_process(c);
		case_read_ram();
		case_hist(c);
		case_init(c);
		case_wake();
		case_api_set();
		case_api_get();
	}
	printf("t32_gsm_oracle: %u compared, 0 mismatches\n", compared);
	return 0;
}
