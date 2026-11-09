// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: AWB per-frame paths.  Runs the unmodified vendor functions
 * (Matt Davis' recovered C, PR #15 @ 2e2e06bc) tisp_awb_get_statistics,
 * tisp_awb_interrupt_static, tisp_awb_tgain_update, tisp_awb_ev_update,
 * tisp_awb_dn_params_refresh, tisp_awb_wake_up and our ports from
 * driver/t32/tx_isp_t32_awb.h from the same random initial state, and compares
 * every register write (address, value, order) and the whole par/tuning region,
 * state and hardware shadow.
 *
 * The AWB sub functions called by these (regional_threshold, lum_th_freq,
 * params_refresh, set_hardware_param, set_riscv_param, system_reg_set_awb_trig)
 * are replaced on both sides by marker writes (reg 0xf000000x, val ch | arg << 8)
 * so call order and arguments are compared; their bodies are covered by
 * t32_awb_oracle.  Not compared: the event pushed by interrupt_static and the
 * dma cache sync arguments.  Statistics zone dimensions are limited to 32 x 32.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_awb.h"

#define BASE_SZ		10240u
#define PAR_OFF		3392u
#define STB		28672u
#define MAXW		192

extern u32 t32_data_e16b4[4], t32_data_e1694[4];
/* not provided by env.c: wake_up reads them (4 channels: 2 and 4 words each); set per case */
u32 t32_data_e1670[8];
u32 t32_data_e1630[16];
extern u8 t32_data_f0900[8];

u64 tisp_awb_get_statistics(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_awb_interrupt_static(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_awb_tgain_update(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_awb_ev_update(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_awb_dn_params_refresh(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_awb_wake_up(u32 a0, u32 a1, u32 a2, u32 a3);

/* marker stubs for the vendor sub calls */
static void mark(u32 id, u32 val)
{
	if (t32o_log.n < T32O_MAX_WRITES) {
		t32o_log.w[t32o_log.n].reg = 0xf0000000u + id;
		t32o_log.w[t32o_log.n].val = val;
		t32o_log.n++;
	}
}
u64 tisp_awb_set_regional_threshold(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	mark(1, a0);
	return 0;
}
u64 tisp_awb_set_lum_th_freq(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	mark(2, a0);
	return 0;
}
u64 tisp_awb_params_refresh(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	mark(3, a0 | (a1 - t32_data_dd0a4[a0]) << 8);
	return 0;
}
u64 tisp_awb_set_hardware_param(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	mark(4, a0);
	return 0;
}
u64 tisp_awb_set_riscv_param(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	mark(5, a0);
	return 0;
}
u64 system_reg_set_awb_trig(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	mark(6, a1 | a0 << 8);
	return 0;
}

static u32 rs = 0x51d3a7u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static u32 rd_salt;
static u32 rd_fn(u32 reg)
{
	u32 x = (reg ^ rd_salt) * 2654435761u;

	x ^= x >> 13; x *= 0x5bd1e995u; x ^= x >> 15;
	return (x & 0x300) ? x : (x & 0xff);
}
static u32 rd_ours(void *p, u32 reg) { (void)p; return rd_fn(reg); }

static void ours_call(void *priv, u32 id, u32 ch, u32 arg, struct t32_stats_write *w, unsigned int *n)
{
	(void)priv;
	w[*n].reg = 0xf0000000u + id;
	w[(*n)++].val = ch | arg << 8;
}

enum { K_STAT, K_INT, K_TG, K_EV, K_DN, K_WAKE, K_N };

static unsigned int compared;

static u8 base_i[BASE_SZ], st_i[sizeof(struct t32_awb_state)], hw_i[sizeof(struct t32_awb_hw)];
static u32 buf_i[4 * STB / 4];
static u8 our_base[BASE_SZ] __attribute__((aligned(16)));
static u8 our_st[sizeof(struct t32_awb_state)] __attribute__((aligned(16)));
static u8 our_hw[sizeof(struct t32_awb_hw)] __attribute__((aligned(16)));
static u32 our_buf[4 * STB / 4] __attribute__((aligned(16)));

static int fail(const char *what, int kind, const char *why)
{
	printf("MISMATCH kind %d (%s): %s\n", kind, what, why);
	return 1;
}

static int run_case(int kind, u32 ch, u32 zw, u32 zh, u32 fc, u32 arg, u8 riscv)
{
	u8 *rb, *rst, *rhw, *rmode, *info;
	u32 *ctx, *rbuf = NULL;
	struct t32_stats_write ow[MAXW];
	struct { u32 reg, val; } rw[MAXW];
	unsigned int ref_n, our_n = 0, i;
	u32 pa = rnd() & ~0xfu, cfg[2], gn[4], ref_par_off, our_par_off, ev_unused = 0;
	u32 par_init = (kind == K_DN) ? 16 : PAR_OFF;
	struct t32_awb a;
	struct t32_awb_ops ops = { 0, ours_call };
	struct t32_awb_wake wk;
	u32 evt;
	u8 ref_base_c[BASE_SZ];
	size_t bufb = (kind == K_INT ? 4 : 1) * STB;

	for (i = 0; i < BASE_SZ; i++) base_i[i] = (u8)rnd();
	for (i = 0; i < sizeof(st_i); i++) st_i[i] = (u8)rnd();
	for (i = 0; i < sizeof(hw_i); i++) hw_i[i] = (u8)rnd();
	for (i = 0; i < 4 * STB / 4; i++) buf_i[i] = rnd();
	for (i = 0; i < 2; i++) cfg[i] = rnd();
	for (i = 0; i < 4; i++) gn[i] = rnd();
	rd_salt = rnd();
	base_i[PAR_OFF + 3186] = (u8)zh; base_i[PAR_OFF + 3187] = 0;
	base_i[PAR_OFF + 3190] = (u8)zw; base_i[PAR_OFF + 3191] = 0;
	if (kind == K_INT) {
		st_i[79040] = (u8)fc; st_i[79041] = (u8)(fc >> 8);
		st_i[79042] = (u8)(fc >> 16); st_i[79043] = (u8)(fc >> 24);
	}
	if (kind == K_DN && (rnd() & 1))
		st_i[107597] = 0;

	/* vendor side */
	t32o_reset();
	rb = t32o_alloc(BASE_SZ);
	rst = t32o_alloc(sizeof(st_i) + 16);
	rhw = t32o_alloc(sizeof(hw_i) + 16);
	rmode = t32o_alloc(32);
	ctx = t32o_alloc(16);
	info = t32o_alloc(16);
	if (kind == K_STAT || kind == K_INT)
		rbuf = t32o_alloc((u32)bufb + 16);
	if (!rb || !rst || !rhw || !rmode || !ctx || !info || ((kind == K_STAT || kind == K_INT) && !rbuf)) {
		fprintf(stderr, "t32o_alloc pool exhausted\n");
		exit(2);
	}
	memcpy(rb, base_i, BASE_SZ);
	memcpy(rst, st_i, sizeof(st_i));
	memcpy(rhw, hw_i, sizeof(hw_i));
	if (rbuf)
		memcpy(rbuf, buf_i, bufb);
	ctx[0] = T32O_PTR(rb) + par_init;
	ctx[1] = T32O_PTR(rst);
	ctx[2] = T32O_PTR(rmode);
	ctx[3] = T32O_PTR(rhw);
	t32_data_e16b4[ch] = T32O_PTR(ctx);
	t32_data_dd0a4[ch] = T32O_PTR(rb);
	((u32 *)info)[0] = 0;
	((u32 *)info)[1] = T32O_PTR(rbuf);
	((u32 *)info)[2] = pa;
	t32_data_e1694[ch] = T32O_PTR(info);
	t32_data_e1670[ch * 2] = cfg[0];
	t32_data_e1670[ch * 2 + 1] = cfg[1];
	for (i = 0; i < 4; i++)
		t32_data_e1630[ch * 4 + i] = gn[i];
	t32_data_f0900[ch] = riscv;
	t32o_rd_hook = rd_fn;

	switch (kind) {
	case K_STAT: t32o_call((t32o_fn)(void (*)(void))tisp_awb_get_statistics, ch, T32O_PTR(rbuf), 0, 0, 0); break;
	case K_INT: t32o_call((t32o_fn)(void (*)(void))tisp_awb_interrupt_static, ch, 0, 0, 0, 0); break;
	case K_TG: t32o_call((t32o_fn)(void (*)(void))tisp_awb_tgain_update, ch, fc, arg, 0, 0); break;
	case K_EV: t32o_call((t32o_fn)(void (*)(void))tisp_awb_ev_update, ch, fc, arg, 0, 0); break;
	case K_DN: t32o_call((t32o_fn)(void (*)(void))tisp_awb_dn_params_refresh, ch, 0, 0, 0, 0); break;
	default: t32o_call((t32o_fn)(void (*)(void))tisp_awb_wake_up, ch, 0, 0, 0, 0); break;
	}
	ref_n = t32o_log.n;
	if (ref_n > MAXW) {
		fprintf(stderr, "ref_n %u > MAXW\n", ref_n);
		exit(2);
	}
	for (i = 0; i < ref_n; i++) {
		rw[i].reg = t32o_log.w[i].reg;
		rw[i].val = t32o_log.w[i].val;
	}
	memcpy(ref_base_c, rb, BASE_SZ);
	ref_par_off = ctx[0] - T32O_PTR(rb);
	(void)ev_unused;

	/* our side */
	memcpy(our_base, base_i, BASE_SZ);
	memcpy(our_st, st_i, sizeof(st_i));
	memcpy(our_hw, hw_i, sizeof(hw_i));
	memcpy(our_buf, buf_i, sizeof(our_buf));
	a.par = (struct t32_awb_par *)(our_base + par_init);
	a.st = (struct t32_awb_state *)our_st;
	a.hw = (struct t32_awb_hw *)our_hw;
	wk.pa = pa;
	wk.cfg[0] = cfg[0]; wk.cfg[1] = cfg[1];
	for (i = 0; i < 4; i++)
		wk.gain[i] = gn[i];
	switch (kind) {
	case K_STAT: t32_awb_get_statistics(&a, our_buf); break;
	case K_INT: t32_awb_interrupt_static(&a, rd_ours, NULL, (const u8 *)our_buf, &evt); break;
	case K_TG: our_n = t32_awb_tgain_update(&a, &ops, ch, arg, ow); break;
	case K_EV: t32_awb_ev_update(&a, arg); break;
	case K_DN: our_n = t32_awb_dn_params_refresh(&a, &ops, ch, our_base, riscv, ow); break;
	default: our_n = t32_awb_wake_up(&a, &ops, ch, &wk, rd_ours, NULL, ow); break;
	}
	our_par_off = (u32)((u8 *)a.par - our_base);

	compared++;
	if (our_n != ref_n)
		return fail("write count", kind, "count differs");
	for (i = 0; i < ref_n; i++)
		if (ow[i].reg != rw[i].reg || ow[i].val != rw[i].val) {
			printf("  #%u vendor %05x=%08x ours %05x=%08x\n", i, rw[i].reg, rw[i].val, ow[i].reg, ow[i].val);
			return fail("write", kind, "register write differs");
		}
	if (ref_par_off != our_par_off)
		return fail("par pointer", kind, "par offset differs");
	if (memcmp(ref_base_c, our_base, BASE_SZ))
		return fail("par/tuning", kind, "tuning region differs");
	if (memcmp(rst, our_st, sizeof(st_i)))
		return fail("state", kind, "state differs");
	if (memcmp(rhw, our_hw, sizeof(hw_i)))
		return fail("hw", kind, "hw shadow differs");
	return 0;
}

int main(void)
{
	const unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 700;
	unsigned int c, k;

	for (c = 0; c < n; c++) {
		u32 ch = rnd() & 3;
		u32 zw = (c & 1) ? rnd() % 33 : 1 + rnd() % 32;
		u32 zh = (c & 2) ? rnd() % 33 : 1 + rnd() % 32;

		for (k = 0; k < K_N; k++)
			if (k != K_INT && run_case((int)k, ch, zw, zh, rnd(), rnd(), (u8)(rnd() % 3)))
				return 1;
	}
	/* K_INT is not run: env.c already defines tisp_awb_interrupt_static, so the ref cannot be linked */
	for (k = 1; k <= 4; k++)
		if (run_case(K_STAT, 0, k, 4, 0, 0, 0) || run_case(K_STAT, 1, 0, 3, 0, 0, 0) ||
		    run_case(K_STAT, 2, 32, 0, 0, 0, 0))
			return 1;
	printf("t32_awbi_oracle: %u compared, 0 mismatches\n", compared);
	return 0;
}
