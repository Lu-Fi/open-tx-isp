// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: DPC parameter accessors (tx_isp_t32_dpc.h):
 * param_array_get, param_array_set, deinit.  Runs the unmodified vendor
 * functions (Matt Davis' recovered C, PR #15 @ 2e2e06bc) and our port from the
 * same state of four channels (the other three are decoys that must stay
 * unchanged) and compares every register write (address, value, order), the
 * whole state of all channels (section, value block, gain, reserved words,
 * saved argument, slot present, block pointer present), the output buffer with
 * a guard area behind it, the length word and, for deinit, the free calls.
 * Every set case also runs the vendor with t32_data_d03a0 = 1 (shadow
 * addresses): only the addresses may differ, through a one to one mapping.
 * The "wide" decision of the register function (did the vendor write 0x2144?)
 * is read from the vendor run and handed to ours, as in t32_dpcf_oracle.
 */
#include <signal.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_dpc.h"

#define SECB	T32_DPC_SEC_BYTES
#define VALB	T32_DPC_VALUES
#define MAXW	(T32_DPC_MAX_WRITES + 8)
#define GUARD	16

extern u32 t32_data_e35f0[4];
extern u64 tisp_dpc_param_array_get(u32, u32, u32, u32);
extern u64 tisp_dpc_param_array_set(u32, u32, u32, u32);
extern u64 tisp_dpc_deinit(u32, u32, u32, u32);

/*
 * deinit observer (the env.c stub is weak): ev[] holds the free calls in
 * order, kind 1 = a context, 3 = a value block, with the channel it belongs
 * to (9 = unknown).
 */
static u32 ev[8], kn;
static u32 g_cx[4], g_blk[4];		/* vendor context / value block addresses */

u64 private_kfree(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u32 k, kind = 1, c = 9;

	(void)a1; (void)a2; (void)a3;
	for (k = 0; k < 4; k++) {
		if (a0 == g_cx[k]) {
			kind = 1; c = k;
		}
		if (a0 == g_blk[k]) {
			kind = 3; c = k;
		}
	}
	if (kn < 8)
		ev[kn] = kind << 4 | c;
	kn++;
	return 0;
}

/*
 * tisp_reg_write (fn_1c790) is replaced as in t32_mscac_oracle: mode 0
 * (t32_data_d03a0 != 1) is its system_reg_write tail call; in mode 1 the
 * vendor queues (index, value) into a register shadow, logged as index / value.
 */
u64 system_reg_write(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_reg_write(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a3;
	if (t32_data_d03a0[0] != 1)
		return system_reg_write(a1, a2, 0, 0);
	if (t32o_log.n < T32O_MAX_WRITES) {
		t32o_log.w[t32o_log.n].reg = a1;
		t32o_log.w[t32o_log.n].val = a2;
		t32o_log.n++;
	}
	return 0;
}

static u32 rs = 0x6c2f1d5u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

struct st {
	u8 sec[4][SECB];
	u8 blk[4][VALB];
	u32 gain[4], gain1[4], rsvd0[4], arg[4];
	u8 tbl[4][88];
	u8 present[4], blknull[4];
};

struct res {
	struct st s;
	unsigned int n;
	struct t32_front_write w[MAXW];
	u8 out[SECB + GUARD];
	u32 len;
	u32 ret;
	u32 kn, kch[8];		/* kind << 4 | channel of each free call */
};

static unsigned int compared, skipped, seen[3][2];
static u32 idx2reg[0x10000], reg2idx[0x20000];
static struct t32_dpc_ctx pc[4];
static int g_wide;
static sigjmp_buf fault_env;

static void fault_handler(int sig)
{
	(void)sig;
	siglongjmp(fault_env, 1);
}

static void port_kfree(void *p)
{
	u32 k, kind = 1, c = 9;

	for (k = 0; k < 4; k++) {
		if (p == &pc[k]) {
			kind = 1; c = k;
		}
		if (p == pc[k].o) {
			kind = 3; c = k;
		}
	}
	if (kn < 8)
		ev[kn] = kind << 4 | c;
	kn++;
}

/* returns 1 when the vendor faulted (memory the oracle cannot provide) */
static int run(int which, u32 mode, u32 ch, const struct st *in, const u8 *buf, int ref, struct res *r)
{
	unsigned int i;
	u8 *sec[4], *blk[4], *cx[4], *ob, *lp, *bp, *base;
	struct t32_dpc_ctx *slot[4];

	memset(r, 0, sizeof(*r));
	kn = 0;
	t32o_reset();
	t32_data_d03a0[0] = (u8)mode;
	base = t32o_alloc(64);
	ob = t32o_alloc(SECB + GUARD);
	lp = t32o_alloc(4);
	bp = t32o_alloc(SECB);
	memset(ob, 0xa5, SECB + GUARD);
	memcpy(bp, buf, SECB);
	r->len = 0x77777777u;
	memcpy(lp, &r->len, 4);
	for (i = 0; i < 4; i++) {
		sec[i] = t32o_alloc(SECB);
		blk[i] = t32o_alloc(VALB + 16);
		cx[i] = t32o_alloc(0x200);
		memcpy(sec[i], in->sec[i], SECB);
		memcpy(blk[i], in->blk[i], VALB);
		memset(cx[i], 0, 0x200);
		if (ref) {
			*(u32 *)(cx[i] + 0) = T32O_PTR(sec[i]);
			*(u32 *)(cx[i] + 4) = in->blknull[i] ? 0 : T32O_PTR(blk[i]);
			*(u32 *)(cx[i] + 8) = in->rsvd0[i];
			*(u32 *)(cx[i] + 12) = in->gain[i];
			*(u32 *)(cx[i] + 16) = in->gain1[i];
			*(u32 *)(cx[i] + 20) = in->arg[i];
			memcpy(cx[i] + 24, in->tbl[i], 88);
			t32_data_e35f0[i] = in->present[i] ? T32O_PTR(cx[i]) : 0;
			g_cx[i] = T32O_PTR(cx[i]);
			g_blk[i] = in->blknull[i] ? 0 : T32O_PTR(blk[i]);
			t32_data_dd0a4[i] = T32O_PTR(base);
		} else {
			pc[i].sec = sec[i];
			pc[i].o = in->blknull[i] ? NULL : blk[i];
			pc[i].gain = in->gain[i];
			pc[i].rsvd0 = in->rsvd0[i];
			pc[i].gain1 = in->gain1[i];
			memcpy(pc[i].tbl, in->tbl[i], 88);
			pc[i].arg = in->arg[i];
			slot[i] = in->present[i] ? &pc[i] : NULL;
		}
	}
	if (ref) {
		if (sigsetjmp(fault_env, 1))
			return 1;
		switch (which) {
		case 0: r->ret = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_dpc_param_array_get, ch, T32O_PTR(ob), T32O_PTR(lp), 0, 0); break;
		case 1: r->ret = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_dpc_param_array_set, ch, T32O_PTR(bp), 0, 0, 0); break;
		default: r->ret = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_dpc_deinit, ch, 0, 0, 0, 0); break;
		}
		r->n = t32o_log.n;
		if (r->n > MAXW) {
			fprintf(stderr, "ref wrote %u > MAXW\n", r->n);
			exit(2);
		}
		for (i = 0; i < r->n; i++) {
			r->w[i].reg = t32o_log.w[i].reg;
			r->w[i].val = t32o_log.w[i].val;
			if (mode == 0 && which == 1 && r->w[i].reg == 0x2144)
				g_wide = 1;
		}
		for (i = 0; i < 4; i++) {
			r->s.present[i] = t32_data_e35f0[i] ? 1 : 0;
			r->s.rsvd0[i] = *(u32 *)(cx[i] + 8);
			r->s.gain[i] = *(u32 *)(cx[i] + 12);
			r->s.gain1[i] = *(u32 *)(cx[i] + 16);
			memcpy(r->s.tbl[i], cx[i] + 24, 88);
			r->s.arg[i] = *(u32 *)(cx[i] + 20);
			r->s.blknull[i] = *(u32 *)(cx[i] + 4) ? 0 : 1;
		}
		for (i = 0; i < kn && i < 8; i++)
			r->kch[i] = ev[i];
		r->kn = kn;
		memcpy(&r->len, lp, 4);
	} else {
		static struct t32_front_write w[MAXW];

		switch (which) {
		case 0: r->ret = (u32)t32_dpc_param_array_get(slot[ch], ob, &r->len); break;
		case 1: r->n = (unsigned int)t32_dpc_param_array_set(slot[ch], bp, g_wide, w); break;
		default: r->ret = (u32)t32_dpc_deinit(port_kfree, &slot[ch]); break;
		}
		memcpy(r->w, w, r->n * sizeof(w[0]));
		for (i = 0; i < 4; i++) {
			r->s.present[i] = slot[i] ? 1 : 0;
			r->s.gain[i] = pc[i].gain;
			r->s.rsvd0[i] = pc[i].rsvd0;
			r->s.gain1[i] = pc[i].gain1;
			memcpy(r->s.tbl[i], pc[i].tbl, 88);
			r->s.arg[i] = pc[i].arg;
			r->s.blknull[i] = pc[i].o ? 0 : 1;
		}
		for (i = 0; i < kn && i < 8; i++)
			r->kch[i] = ev[i];
		r->kn = kn;
	}
	for (i = 0; i < 4; i++) {
		memcpy(r->s.sec[i], sec[i], SECB);
		memcpy(r->s.blk[i], blk[i], VALB);
	}
	memcpy(r->out, ob, SECB + GUARD);
	return 0;
}

/* mode 1 must differ from mode 0 only in the addresses */
static int transport_same(const char *what, const struct res *a, const struct res *b)
{
	unsigned int i;

	if (a->n != b->n || memcmp(&a->s, &b->s, sizeof(a->s)) || memcmp(a->out, b->out, sizeof(a->out)) ||
	    a->len != b->len || a->ret != b->ret || a->kn != b->kn) {
		printf("MISMATCH %s: shadow mode (d03a0 1) changes more than the addresses\n", what);
		return 1;
	}
	for (i = 0; i < a->n; i++) {
		u32 reg = a->w[i].reg, x = b->w[i].reg;

		if (a->w[i].val != b->w[i].val) {
			printf("MISMATCH %s: shadow mode write #%u value %08x / %08x\n", what, i, a->w[i].val, b->w[i].val);
			return 1;
		}
		if (x == reg)
			continue;
		if (x >= 0x10000 || reg >= 0x20000 || (idx2reg[x] && idx2reg[x] != reg) ||
		    (reg2idx[reg] && reg2idx[reg] != x + 1)) {
			printf("MISMATCH %s: shadow index %u <-> register %05x not one to one\n", what, x, reg);
			return 1;
		}
		idx2reg[x] = reg;
		reg2idx[reg] = x + 1;
	}
	return 0;
}

int main(void)
{
	static const char *const names[3] = { "param_array_get", "param_array_set", "deinit" };
	unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 3000;
	unsigned int c, i, k;
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = fault_handler;
	sigaction(SIGSEGV, &sa, NULL);
	sigaction(SIGBUS, &sa, NULL);

	for (c = 0; c < n; c++)
		for (k = 0; k < 3; k++) {
			static struct st in;
			static struct res r, o, r1;
			u8 buf[SECB];
			u32 ch = rnd() & 3;

			for (i = 0; i < 4; i++) {
				unsigned int j;

				for (j = 0; j < SECB; j++)
					in.sec[i][j] = (j % 3) ? (u8)rnd() : (u8)(rnd() & 7);
				for (j = 0; j < VALB; j++)
					in.blk[i][j] = (u8)rnd();
				in.gain[i] = ((rnd() % 11) << 16) | (rnd() & 0xffff);
				in.gain1[i] = ((rnd() % 11) << 16) | (rnd() & 0xffff);
				in.rsvd0[i] = rnd();
				for (j = 0; j < 88; j++)
					in.tbl[i][j] = (u8)rnd();
				in.arg[i] = rnd();
				in.present[i] = i == ch ? (k == 2 ? rnd() % 4 != 0 : 1) : (u8)(rnd() % 4 != 0);
				in.blknull[i] = k == 2 && rnd() % 4 == 0;
			}
			for (i = 0; i < SECB; i++)
				buf[i] = (i % 3) ? (u8)rnd() : (u8)(rnd() & 7);
			t32o_reg88 = rnd() % 40;
			g_wide = 0;
			if (run((int)k, 0, ch, &in, buf, 1, &r)) {
				skipped++;
				continue;
			}
			run((int)k, 0, ch, &in, buf, 0, &o);
			compared++;
			seen[k][in.present[ch]]++;
			if (r.n != o.n || memcmp(&r.s, &o.s, sizeof(r.s)) || memcmp(r.out, o.out, sizeof(r.out)) ||
			    r.len != o.len || r.ret != o.ret || r.kn != o.kn || memcmp(r.kch, o.kch, sizeof(r.kch))) {
				printf("MISMATCH %s ch %u: writes %u/%u len %u/%u ret %u/%u frees %u/%u or state\n", names[k], ch,
				       r.n, o.n, r.len, o.len, r.ret, o.ret, r.kn, o.kn);
				for (i = 0; i < sizeof(r.s); i++)
					if (((u8 *)&r.s)[i] != ((u8 *)&o.s)[i]) {
						printf("  state byte %u (sec %u, blk %u, rest %u) differs (vendor %02x ours %02x)\n", i,
						       i < 4 * SECB ? i : 99999, i >= 4 * SECB && i < 4 * SECB + 4 * VALB ? i - 4 * SECB : 99999,
						       i >= 4 * SECB + 4 * VALB ? i - 4 * SECB - 4 * VALB : 99999, ((u8 *)&r.s)[i], ((u8 *)&o.s)[i]);
						break;
					}
				return 1;
			}
			for (i = 0; i < r.n; i++)
				if (r.w[i].reg != o.w[i].reg || r.w[i].val != o.w[i].val) {
					printf("MISMATCH %s: write #%u vendor %08x=%08x ours %08x=%08x\n", names[k], i,
					       r.w[i].reg, r.w[i].val, o.w[i].reg, o.w[i].val);
					return 1;
				}
			if (k == 1) {
				if (run((int)k, 1, ch, &in, buf, 1, &r1)) {
					printf("MISMATCH %s: vendor faults in shadow mode only\n", names[k]);
					return 1;
				}
				if (transport_same(names[k], &r, &r1))
					return 1;
			}
		}
	for (k = 0; k < 3; k++)
		if (!seen[k][1] || (k == 2 && !seen[k][0])) {
			printf("t32_dpcp_oracle: %s never ran both ways\n", names[k]);
			return 1;
		}
	printf("t32_dpcp_oracle: %u compared, %u skipped, 0 mismatches\n", compared, skipped);
	return 0;
}
