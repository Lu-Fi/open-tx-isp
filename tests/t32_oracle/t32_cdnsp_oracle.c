// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: CDNS parameter accessors (tx_isp_t32_cdns.h):
 * param_array_get, param_array_set, all_reg_refresh, deinit.  Runs the unmodified
 * vendor functions (Matt Davis' recovered C, PR #15 @ 2e2e06bc) and our port
 * from the same state of four channels (the other three are decoys that must
 * stay unchanged) and compares every register write (address, value, order),
 * the whole state of all channels (section, parameter bytes, gain, reserved
 * word, slot present), the output buffer with a guard area behind it, the
 * length word and, for deinit, the free calls.  Every case also runs the
 * vendor with t32_data_d03a0 = 1 (shadow addresses): only the addresses may
 * differ, through a one to one mapping.  The vendor returns 0 from
 * get / set / deinit and the (unused) tail call result from all_reg_refresh;
 * the returns of the port are compared where the vendor's are defined.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_cdns.h"

#define SECB	T32_CDNS_PARAM_BYTES
#define NPAR	39
#define MAXW	96
#define GUARD	16

extern u32 t32_data_e23d0[4];
extern u64 tisp_cdns_param_array_get(u32, u32, u32, u32);
extern u64 tisp_cdns_param_array_set(u32, u32, u32, u32);
extern u64 tisp_cdns_all_reg_refresh(u32, u32, u32, u32);
extern u64 tisp_cdns_deinit(u32, u32, u32, u32);

/* deinit observer: the env.c stub is weak */
static u32 kf[8], kn;
u64 private_kfree(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	if (kn < 8)
		kf[kn] = a0;
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

static u32 rs = 0x51c4a37u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

struct st {
	u8 sec[4][SECB];
	u8 par[4][64];
	u32 gain[4], rsvd[4];
	u8 present[4];
};

struct res {
	struct st s;
	unsigned int n;
	struct t32_front_write w[MAXW];
	u8 out[SECB + GUARD];
	u32 len;
	u32 ret;
	u32 kn, kch[8];		/* channel of each free call, 9 = unknown */
};

static unsigned int compared, seen[4][2];
static u32 idx2reg[0x10000], reg2idx[0x20000];
static struct t32_cdns_ctx pc[4];

static void port_kfree(void *p)
{
	unsigned int k, c = 9;

	for (k = 0; k < 4; k++)
		if (p == &pc[k])
			c = k;
	if (kn < 8)
		kf[kn] = c;
	kn++;
}

static void run(int which, u32 mode, u32 ch, u32 gain_arg, const struct st *in, const u8 *buf,
		int ref, struct res *r)
{
	unsigned int i;
	u8 *sec[4], *par[4], *cx[4], *ob, *lp, *bp;
	struct t32_cdns_ctx *slot[4];

	memset(r, 0, sizeof(*r));
	kn = 0;
	t32o_reset();
	t32_data_d03a0[0] = (u8)mode;
	ob = t32o_alloc(SECB + GUARD);
	lp = t32o_alloc(4);
	bp = t32o_alloc(SECB);
	memset(ob, 0xa5, SECB + GUARD);
	memcpy(bp, buf, SECB);
	r->len = 0x77777777u;
	memcpy(lp, &r->len, 4);
	for (i = 0; i < 4; i++) {
		sec[i] = t32o_alloc(SECB);
		par[i] = t32o_alloc(64);
		cx[i] = t32o_alloc(16);
		memcpy(sec[i], in->sec[i], SECB);
		memcpy(par[i], in->par[i], 64);
		if (ref) {
			*(u32 *)(cx[i] + 0) = T32O_PTR(sec[i]);
			*(u32 *)(cx[i] + 4) = T32O_PTR(par[i]);
			*(u32 *)(cx[i] + 8) = in->gain[i];
			*(u32 *)(cx[i] + 12) = in->rsvd[i];
			t32_data_e23d0[i] = in->present[i] ? T32O_PTR(cx[i]) : 0;
		} else {
			pc[i].sec = sec[i];
			pc[i].param = par[i];
			pc[i].gain = in->gain[i];
			pc[i].rsvd = in->rsvd[i];
			slot[i] = in->present[i] ? &pc[i] : NULL;
		}
	}
	if (ref) {
		switch (which) {
		case 0: r->ret = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_cdns_param_array_get, ch, T32O_PTR(ob), T32O_PTR(lp), 0, 0); break;
		case 1: r->ret = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_cdns_param_array_set, ch, T32O_PTR(bp), 0, 0, 0); break;
		case 2: t32o_call((t32o_fn)(void (*)(void))tisp_cdns_all_reg_refresh, ch, gain_arg, 0, 0, 0); break;
		default: r->ret = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_cdns_deinit, ch, 0, 0, 0, 0); break;
		}
		r->n = t32o_log.n;
		if (r->n > MAXW) {
			fprintf(stderr, "ref wrote %u > MAXW\n", r->n);
			exit(2);
		}
		for (i = 0; i < r->n; i++) {
			r->w[i].reg = t32o_log.w[i].reg;
			r->w[i].val = t32o_log.w[i].val;
		}
		for (i = 0; i < 4; i++) {
			r->s.present[i] = t32_data_e23d0[i] ? 1 : 0;
			r->s.gain[i] = *(u32 *)(cx[i] + 8);
			r->s.rsvd[i] = *(u32 *)(cx[i] + 12);
		}
		for (i = 0; i < kn && i < 8; i++) {
			unsigned int k;

			r->kch[i] = 9;
			for (k = 0; k < 4; k++)
				if (kf[i] == T32O_PTR(cx[k]))
					r->kch[i] = k;
		}
		r->kn = kn;
		memcpy(&r->len, lp, 4);
	} else {
		static struct t32_front_write w[MAXW];

		switch (which) {
		case 0: r->ret = (u32)t32_cdns_param_array_get(slot[ch], ob, &r->len); break;
		case 1: r->n = (unsigned int)t32_cdns_param_array_set(slot[ch], (u8)ch, bp, w); break;
		case 2: r->n = (unsigned int)t32_cdns_all_reg_refresh(slot[ch], (u8)ch, gain_arg, w); break;
		default: r->ret = (u32)t32_cdns_deinit(port_kfree, &slot[ch]); break;
		}
		memcpy(r->w, w, r->n * sizeof(w[0]));
		for (i = 0; i < 4; i++) {
			r->s.present[i] = slot[i] ? 1 : 0;
			r->s.gain[i] = pc[i].gain;
			r->s.rsvd[i] = pc[i].rsvd;
		}
		for (i = 0; i < kn && i < 8; i++)
			r->kch[i] = kf[i];
		r->kn = kn;
	}
	for (i = 0; i < 4; i++) {
		memcpy(r->s.sec[i], sec[i], SECB);
		memcpy(r->s.par[i], par[i], 64);
	}
	memcpy(r->out, ob, SECB + GUARD);
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
	static const char *const names[4] = { "param_array_get", "param_array_set", "all_reg_refresh", "deinit" };
	unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 3000;
	unsigned int c, i, k;

	for (c = 0; c < n; c++)
		for (k = 0; k < 4; k++) {
			static struct st in;
			static struct res r, o, r1;
			u8 buf[SECB];
			u32 ch = rnd() & 3, gain = rnd();

			for (i = 0; i < 4; i++) {
				unsigned int j;

				for (j = 0; j < SECB; j++)
					in.sec[i][j] = (j % 3) ? (u8)rnd() : (u8)(rnd() & 7);
				for (j = 0; j < 64; j++)
					in.par[i][j] = (u8)rnd();
				in.gain[i] = rnd() % 5 == 0 ? 0xffffffffu : rnd();
				in.rsvd[i] = rnd();
				in.present[i] = i == ch ? (k == 3 ? rnd() % 4 != 0 : 1) : (u8)(rnd() % 4 != 0);
			}
			for (i = 0; i < SECB; i++)
				buf[i] = (i % 3) ? (u8)rnd() : (u8)(rnd() & 7);
			if (rnd() % 3 == 0)
				gain &= 0x0003ffffu;
			run((int)k, 0, ch, gain, &in, buf, 1, &r);
			run((int)k, 0, ch, gain, &in, buf, 0, &o);
			compared++;
			seen[k][in.present[ch]]++;
			if (r.n != o.n || memcmp(&r.s, &o.s, sizeof(r.s)) || memcmp(r.out, o.out, sizeof(r.out)) ||
			    r.len != o.len || (k != 2 && r.ret != o.ret) || r.kn != o.kn ||
			    memcmp(r.kch, o.kch, sizeof(r.kch))) {
				printf("MISMATCH %s ch %u: writes %u/%u len %u/%u ret %u/%u frees %u/%u or state\n", names[k], ch,
				       r.n, o.n, r.len, o.len, r.ret, o.ret, r.kn, o.kn);
				return 1;
			}
			for (i = 0; i < r.n; i++)
				if (r.w[i].reg != o.w[i].reg || r.w[i].val != o.w[i].val) {
					printf("MISMATCH %s: write #%u vendor %08x=%08x ours %08x=%08x\n", names[k], i,
					       r.w[i].reg, r.w[i].val, o.w[i].reg, o.w[i].val);
					return 1;
				}
			if (k == 1 || k == 2) {
				run((int)k, 1, ch, gain, &in, buf, 1, &r1);
				if (transport_same(names[k], &r, &r1))
					return 1;
			}
		}
	for (k = 0; k < 4; k++)
		if (!seen[k][1] || (k == 3 && !seen[k][0])) {
			printf("t32_cdnsp_oracle: %s never ran both ways\n", names[k]);
			return 1;
		}
	printf("t32_cdnsp_oracle: %u compared, 0 mismatches\n", compared);
	return 0;
}
