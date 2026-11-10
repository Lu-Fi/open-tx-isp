// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: DECMP.  Runs the unmodified vendor functions (Matt Davis'
 * recovered C, PR #15 @ 2e2e06bc) and our driver/t32/tx_isp_t32_decmp.h from
 * the same initial state; compares every register write (address, value,
 * order), the whole tuning buffer (canaries around the 100 byte section),
 * all four channel context slots (decoy channels), the get output buffer
 * with its length, and the return values.
 *
 * Environment: env.c is not changed.  Data that the reference reads and env.c
 * may not provide (t32_data_e3f60 context table, the two isp_printf string
 * symbols, private_kmalloc / private_kfree / isp_printf) is defined here WEAK,
 * so a strong env.c definition wins.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_decmp.h"

__attribute__((weak)) u32 t32_data_e3f60[4] = {0};
__attribute__((weak)) unsigned char t32_data_c2ef0[16] = {0};
__attribute__((weak)) unsigned char t32_data_cb698[16] = {0};
__attribute__((weak)) u32 t32o_km_n, t32o_km_size, t32o_km_flags, t32o_kf_n, t32o_kf_p;
__attribute__((weak)) u64 private_kmalloc(u32 size, u32 flags, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	t32o_km_n++;
	t32o_km_size = size;
	t32o_km_flags = flags;
	return T32O_PTR(t32o_alloc(size));
}
__attribute__((weak)) u64 private_kfree(u32 p, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	t32o_kf_n++;
	t32o_kf_p = p;
	return 0;
}
__attribute__((weak)) u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a1; (void)a2; (void)a3;
	return 0;
}

extern u64 tisp_decmp_ref_reg_cfg(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_decmp_all_reg_refresh(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_decmp_init(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_decmp_deinit(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_decmp_dn_params_refresh(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_decmp_param_array_get(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_decmp_param_array_set(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_decmp_wake_up(u32 a0, u32 a1, u32 a2, u32 a3);

#define PO	T32_DECMP_PARAM_OFF
#define NB	100u
#define BUFSZ	(0xf00cu + 100u + 64u)
#define MAXW	64

enum { OP_REG, OP_ALL, OP_DN, OP_GET, OP_SET, OP_WAKE, OP_INIT, OP_DEINIT, OP_N };
static const char *opname[OP_N] = { "ref_reg_cfg", "all_reg_refresh", "dn_params_refresh",
	"param_array_get", "param_array_set", "wake_up", "init", "deinit" };

static u32 rs = 0xdec3a5u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

struct input {
	int op, ch, nullslot;
	u8 mem[BUFSZ];
	u32 off0[4], w1[4];
	u8 sbuf[NB];
};

struct snap {
	unsigned int n;
	struct t32_front_write w[MAXW];
	u8 mem[BUFSZ];
	int slotnull[4];
	u32 off0[4], w1[4];
	u8 ob[NB + 64];
	u32 len;
	u32 ret;
};

static struct input in;
static struct snap R, O;
static int kfree_calls;
static u32 r_slot0, o_slot0, o_flags, o_kfp;
static u32 vret[OP_N];
static int vret_set[OP_N];
static unsigned int compared;

static void gen_params(u32 *p, int kind)
{
	unsigned int i;
	u32 acc;

	p[0] = rnd();
	switch (kind) {
	case 0:
		for (i = 1; i < 25; i++)
			p[i] = rnd();
		break;
	case 1:
		for (i = 1; i < 25; i++)
			p[i] = rnd() & 0xfff;
		break;
	case 2:
	case 3:
		acc = rnd() & 0xff;
		for (i = 1; i <= 12; i++) {
			if (!(kind == 3 && (rnd() & 1)))
				acc += rnd() & 0xff;
			p[i] = acc;
		}
		acc = rnd() & 0xfff;
		for (i = 13; i <= 24; i++) {
			acc += rnd() & 0xfff;
			p[i] = acc;
		}
		break;
	case 4:
		for (i = 1; i < 25; i++)
			p[i] = 0;
		break;
	default:
		for (i = 1; i < 25; i++)
			p[i] = (rnd() & 0xfff) | ((rnd() & 3) << 12) | ((rnd() & 3) << 16) | ((rnd() & 1) << 31);
		break;
	}
}

static void gen_input(int op, int ch, int kind)
{
	unsigned int i;
	u32 p[25];

	in.op = op;
	in.ch = ch;
	in.nullslot = (rnd() % 4) == 0;
	for (i = 0; i < BUFSZ; i++)
		in.mem[i] = (u8)rnd();
	gen_params(p, kind);
	memcpy(in.mem + PO, p, sizeof(p));
	for (i = 0; i < NB; i++)
		in.sbuf[i] = (u8)rnd();
	if (op == OP_SET && (rnd() & 1)) {
		gen_params(p, (int)(rnd() % 6));
		memcpy(in.sbuf, p, NB);
	}
	for (i = 0; i < 4; i++) {
		in.off0[i] = (rnd() & 1) ? PO : (rnd() % 64) * 4;
		in.w1[i] = rnd();
	}
}

static void side_ref(void)
{
	u8 *rb, *dec, *obuf, *sbuf;
	u32 *lenp, *cw;
	unsigned int c;

	t32o_reset();
	rb = t32o_alloc(BUFSZ);
	dec = t32o_alloc(64);
	memcpy(rb, in.mem, BUFSZ);
	for (c = 0; c < 4; c++) {
		t32_data_dd0a4[c] = T32O_PTR((int)c == in.ch ? rb : dec);
		cw = t32o_alloc(8);
		cw[0] = T32O_PTR(rb) + in.off0[c];
		cw[1] = in.w1[c];
		t32_data_e3f60[c] = T32O_PTR(cw);
	}
	if (in.op == OP_DEINIT && in.nullslot)
		t32_data_e3f60[in.ch] = 0;
	if (in.op == OP_INIT)
		t32_data_e3f60[in.ch] = 0x1234;
	r_slot0 = t32_data_e3f60[in.ch];
	t32o_km_n = t32o_kf_n = 0;
	obuf = t32o_alloc(NB + 64);
	memset(obuf, 0xc3, NB + 64);
	lenp = t32o_alloc(4);
	*lenp = 0x55555555u;
	sbuf = t32o_alloc(NB);
	memcpy(sbuf, in.sbuf, NB);
	{
		u32 ch = (u32)in.ch;
		u64 (*f)(u32, u32, u32, u32) = 0;
		u32 a1 = 0, a2 = 0;

		switch (in.op) {
		case OP_REG: f = tisp_decmp_ref_reg_cfg; break;
		case OP_ALL: f = tisp_decmp_all_reg_refresh; break;
		case OP_DN: f = tisp_decmp_dn_params_refresh; break;
		case OP_GET: f = tisp_decmp_param_array_get; a1 = T32O_PTR(obuf); a2 = T32O_PTR(lenp); break;
		case OP_SET: f = tisp_decmp_param_array_set; a1 = T32O_PTR(sbuf); break;
		case OP_WAKE: f = tisp_decmp_wake_up; break;
		case OP_INIT: f = tisp_decmp_init; break;
		default: f = tisp_decmp_deinit; break;
		}
		R.ret = (u32)t32o_call((t32o_fn)(void (*)(void))f, ch, a1, a2, 0, 0);
	}
	R.n = t32o_log.n;
	if (R.n > MAXW) {
		fprintf(stderr, "ref wrote %u registers\n", R.n);
		exit(2);
	}
	memcpy(R.w, t32o_log.w, R.n * sizeof(R.w[0]));
	memcpy(R.mem, rb, BUFSZ);
	for (c = 0; c < 4; c++) {
		u32 s = t32_data_e3f60[c];

		R.slotnull[c] = s == 0;
		R.off0[c] = R.w1[c] = 0;
		if (s) {
			cw = T32O_HOST(s);
			R.off0[c] = cw[0] - T32O_PTR(rb);
			R.w1[c] = cw[1];
		}
	}
	memcpy(R.ob, obuf, NB + 64);
	R.len = *lenp;
}

static void *our_alloc(u32 size, u32 flags)
{
	void *p = t32o_alloc(size);

	o_flags = flags;
	memset(p, 0xa5, size);
	return p;
}

static void our_kfree(void *p)
{
	o_kfp = T32O_PTR(p);
	kfree_calls++;
}

static void side_our(void)
{
	u8 *ob, *obuf;
	u32 *lenp;
	struct t32_decmp_ctx *sc[4];
	struct t32_front_write ow[MAXW];
	unsigned int c;
	int n = 0;

	t32o_reset();
	ob = t32o_alloc(BUFSZ);
	memcpy(ob, in.mem, BUFSZ);
	for (c = 0; c < 4; c++) {
		sc[c] = t32o_alloc(sizeof(*sc[c]));
		sc[c]->param = (u32 *)(ob + in.off0[c]);
		sc[c]->rsvd = in.w1[c];
	}
	if (in.op == OP_DEINIT && in.nullslot)
		sc[in.ch] = NULL;
	obuf = t32o_alloc(NB + 64);
	memset(obuf, 0xc3, NB + 64);
	lenp = t32o_alloc(4);
	*lenp = 0x55555555u;
	kfree_calls = 0;
	o_kfp = o_flags = 0;
	o_slot0 = T32O_PTR(sc[in.ch]);
	O.ret = 0;
	switch (in.op) {
	case OP_REG: n = t32_decmp_ref_reg_cfg(sc[in.ch], ow); break;
	case OP_ALL: n = t32_decmp_all_reg_refresh(sc[in.ch], ow); break;
	case OP_DN: n = t32_decmp_dn_params_refresh(sc[in.ch], ob, ow); break;
	case OP_GET: O.ret = (u32)t32_decmp_param_array_get(sc[in.ch], obuf, lenp); break;
	case OP_SET: n = t32_decmp_param_array_set(sc[in.ch], in.sbuf, ow); break;
	case OP_WAKE: n = t32_decmp_wake_up(sc[in.ch], ow); break;
	case OP_INIT: O.ret = (u32)t32_decmp_init(&sc[in.ch], our_alloc, ob); break;
	default: O.ret = (u32)t32_decmp_deinit(our_kfree, &sc[in.ch]); break;
	}
	if (in.op == OP_REG || in.op == OP_ALL || in.op == OP_DN || in.op == OP_SET || in.op == OP_WAKE)
		O.ret = (u32)n;
	if (in.op == OP_DEINIT && kfree_calls != (in.nullslot ? 0 : 1)) {
		printf("MISMATCH deinit kfree calls %d\n", kfree_calls);
		exit(1);
	}
	O.n = (unsigned int)n;
	memcpy(O.w, ow, O.n * sizeof(O.w[0]));
	memcpy(O.mem, ob, BUFSZ);
	for (c = 0; c < 4; c++) {
		O.slotnull[c] = sc[c] == NULL;
		O.off0[c] = O.w1[c] = 0;
		if (sc[c]) {
			O.off0[c] = (u32)((u8 *)sc[c]->param - ob);
			O.w1[c] = sc[c]->rsvd;
		}
	}
	memcpy(O.ob, obuf, NB + 64);
	O.len = *lenp;
}

static void bad(const char *what, unsigned int i)
{
	printf("MISMATCH %s op %s ch %d at %u (ref n %u ours n %u)\n", what, opname[in.op], in.ch, i, R.n, O.n);
	if (i < R.n && i < O.n)
		printf("  vendor %05x=%08x ours %05x=%08x\n", R.w[i].reg, R.w[i].val, O.w[i].reg, O.w[i].val);
	exit(1);
}

static void run_case(int op, int ch, int kind)
{
	unsigned int i;

	gen_input(op, ch, kind);
	side_ref();
	side_our();
	compared++;
	if (R.n != O.n)
		bad("write count", 0);
	for (i = 0; i < R.n; i++)
		if (R.w[i].reg != O.w[i].reg || R.w[i].val != O.w[i].val)
			bad("register write", i);
	if (op == OP_REG || op == OP_ALL || op == OP_WAKE || op == OP_DN || op == OP_SET) {
		/* port returns the write count; vendor returns the writer stub's value */
		if (O.ret != R.n || O.ret != (u32)T32_DECMP_WRITES)
			bad("port return value", 0);
		if (!vret_set[op]) {
			vret_set[op] = 1;
			vret[op] = R.ret;
		} else if (vret[op] != R.ret)
			bad("vendor return value", 0);
	} else if (R.ret != O.ret)
		bad("return value", 0);
	if (op == OP_INIT && t32o_km_n && (t32o_km_n != 1 || t32o_km_flags != o_flags)) {
		printf("  vendor kmalloc n %u size %u flags %#x ours flags %#x\n", t32o_km_n,
		       t32o_km_size, t32o_km_flags, o_flags);
		bad("kmalloc flags", 0);
	}
	if (op == OP_DEINIT && !in.nullslot && t32o_kf_n &&
	    (t32o_kf_n != 1 || t32o_kf_p != r_slot0 || o_kfp != o_slot0))
		bad("kfree pointer", 0);
	if (memcmp(R.mem, O.mem, BUFSZ)) {
		for (i = 0; R.mem[i] == O.mem[i]; i++)
			;
		bad("tuning buffer", i);
	}
	for (i = 0; i < 4; i++)
		if (R.slotnull[i] != O.slotnull[i] || R.off0[i] != O.off0[i] || R.w1[i] != O.w1[i])
			bad("context", i);
	if (memcmp(R.ob, O.ob, NB + 64) || R.len != O.len)
		bad("get output", 0);
}

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 2500;
	unsigned int c;
	int op, ch, kind;

	for (kind = 0; kind < 6; kind++)
		for (op = 0; op < OP_N; op++)
			for (ch = 0; ch < 4; ch++)
				run_case(op, ch, kind);
	for (c = 0; c < n; c++)
		for (op = 0; op < OP_N; op++)
			run_case(op, (int)(rnd() & 3), (int)(rnd() % 6));
	printf("t32_decmp_oracle: %u compared, 0 mismatches\n", compared);
	return 0;
}
