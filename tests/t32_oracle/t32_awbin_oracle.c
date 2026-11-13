// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: AWB per-frame algorithm paths.  Runs the unmodified vendor
 * functions tisp_awb_algo_{waitdone,getinfo,init} and tisp_awb_sleep (Matt
 * Davis' recovered C, PR #15 @ 2e2e06bc) and our t32_awb_algo_{waitdone,
 * getinfo,init} / t32_awb_sleep from driver/t32/tx_isp_t32_stats.h from the
 * same initial state, and compares return values, the structures the vendor
 * changes (state words, zone pointer, caller info, out words, zone buffer,
 * saved registers) and the hook calls.
 *
 * Glue: private_{wait_for_completion_timeout,init_completion,vmalloc,vfree}
 * are defined here (env.c's versions are weak) and record their arguments;
 * the port's hooks get the same results, and the calls / arguments of both
 * sides are compared (the oracle refuses to run with env's glue).
 * tisp_reg_read, system_reg_read and the data e16b4 / e16c4 / e1670 / e1630
 * are WEAK here, so env.c definitions win.  tisp_awb_set_lum_th_freq (called by getinfo) is a hook in
 * the port: only its call and channel are checked (the vendor writes
 * registers iff it is called).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_stats.h"

#define TB	0x3000u
#define SECBUF	0x1b000u
#define ZB	12288u

extern u64 tisp_awb_algo_waitdone(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_awb_algo_getinfo(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_awb_algo_init(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_awb_sleep(u32 a0, u32 a1, u32 a2, u32 a3);

__attribute__((weak)) u32 t32_data_e16b4[4];
__attribute__((weak)) u32 t32_data_e16c4[16];
__attribute__((weak)) u32 t32_data_e1670[8];
__attribute__((weak)) u32 t32_data_e1630[16];
/* isp_printf format string of the out-of-memory message; only its address is used */
__attribute__((weak)) unsigned char t32_data_c9458[64];

static u32 rs = 0x51a7e3u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static void put32(u8 *p, u32 v) { memcpy(p, &v, 4); }

/* buffers handed to the 32-bit reference code: static, i.e. below 4 GiB */
static u8 tbuf[TB], secbuf[SECBUF], zone_r[ZB], zone_o[ZB], zinit[ZB];
static u8 new_r[64], new_o[64], ctxo_mem[64];
static u8 info_r[20];
static u32 ctxr[8] __attribute__((aligned(16)));
static u32 out_r[9];

struct hk {
	u32 wait_ret, vm_ret, vm_on;
	u32 wait_n, wait_arg, wait_ms;
	u32 initc_n, initc_arg;
	u32 vfree_n, vfree_arg;
	u32 vm_n, vm_size;
	u32 lum_n, lum_ch, oom_n, pre_bad;
};
static struct hk rh, oh;

static u32 rsalt;
static u32 fake_read(u32 reg) { return reg * 0x9e3779b1u ^ rsalt; }

__attribute__((weak)) u64 tisp_reg_read(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a2; (void)a3;
	return t32o_rd_hook ? t32o_rd_hook(a1) : 0;
}

__attribute__((weak)) u64 system_reg_read(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	return t32o_rd_hook ? t32o_rd_hook(a0) : 0;
}

u64 private_wait_for_completion_timeout(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	rh.wait_n++;
	rh.wait_arg = a0 - T32O_PTR(ctxr);
	rh.wait_ms = a1;
	return rh.wait_ret;
}

u64 private_init_completion(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	rh.initc_n++;
	rh.initc_arg = a0 - T32O_PTR(ctxr);
	return 0;
}

u64 private_vmalloc(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	rh.vm_n++;
	rh.vm_size = a0;
	return rh.vm_ret;
}

u64 private_vfree(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	rh.vfree_n++;
	rh.vfree_arg = a0 - T32O_PTR(zone_r);
	return 0;
}

static int mine_wait, mine_initc, mine_vm, mine_vfree;

/* our hooks */
static void o_init_completion(u8 *c) { oh.initc_n++; oh.initc_arg = (u32)(c - ctxo_mem); }
static void *o_vmalloc(u32 size) { oh.vm_n++; oh.vm_size = size; return oh.vm_on ? (void *)new_o : (void *)0; }
static void o_vfree(void *p) { oh.vfree_n++; oh.vfree_arg = (u32)((u8 *)p - zone_o); }
static u32 o_wait(u8 *c, u32 ms) { oh.wait_n++; oh.wait_arg = (u32)(c - ctxo_mem); oh.wait_ms = ms; return oh.wait_ret; }
/* the vendor tisp_awb_set_lum_th_freq rewrites the state words (source not available):
 * check the state getinfo hands to it, then apply the vendor's final words */
static u32 hook_sr[4], hook_exp[3];
static void o_lum(u32 ch, struct t32_awb_algo_state *st)
{
	oh.lum_n++;
	oh.lum_ch = ch;
	if (st->lum_a != hook_exp[0] || st->lum_b != hook_exp[1] || st->lum_c != hook_exp[2])
		oh.pre_bad = 1;
	st->lum_a = hook_sr[1];
	st->lum_b = hook_sr[2];
	st->lum_c = hook_sr[3];
}
static void o_oom(void) { oh.oom_n++; }

static const struct t32_awb_algo_ops ops = { o_init_completion, o_vmalloc, o_vfree, o_wait, o_lum, o_oom };

static u32 cur_ch;
static u32 rd_reg(u32 reg) { return (u32)tisp_reg_read(cur_ch, reg, 0, 0); }
static u32 sys_rd(u32 reg) { return (u32)system_reg_read(reg, 0, 0, 0); }

enum { W_WAIT, W_GETINFO, W_INIT, W_SLEEP, W_N };

struct cs {
	int which;
	u32 ch, mode, la, lb, lc, zone_on, null_ctx, wait_ret, vm_on, out6, out7;
};

static unsigned int compared;

#define BAD(w, ...) do { printf("MISMATCH %s: ", w); printf(__VA_ARGS__); printf("\n"); exit(1); } while (0)

static void run_case(const char *what, const struct cs *k)
{
	u32 ch = k->ch, *sr = &t32_data_e16c4[ch * 4];
	u32 out_init[9], out_o[9], ref_ret = 0, ref_sr[4], ref_z, ref_printf, ref_logn, ref_sl[6] = { 0 };
	u8 info_init[20], info_o[20];
	u32 i, w, ours_ret = 0;
	u8 *ours_zone = 0;
	struct t32_awb_algo_ctx co;
	struct t32_awb_algo_state so;

	for (w = 0; w < TB; w += 4)
		put32(tbuf + w, rnd());
	for (w = 0; w < ZB; w += 4)
		put32(secbuf + T32_AWB_ALG_ZONE_SRC + w, rnd());
	for (w = 0; w < 0x40; w += 4)
		put32(secbuf + 0x1a440 + w, rnd());
	for (w = 0; w < ZB; w += 4)
		put32(zinit + w, rnd());
	for (w = 0; w < 20; w++)
		info_init[w] = (u8)rnd();
	for (i = 0; i < 9; i++)
		out_init[i] = rnd();
	out_init[1] = ch;
	out_init[6] = k->out6;
	out_init[7] = k->out7;

	/* vendor side */
	t32o_reset();
	rsalt = rnd();
	t32o_rd_hook = fake_read;
	memset(t32_data_e1670, 0, 32);
	memset(t32_data_e1630, 0, 64);
	t32_data_dd0a4[ch] = T32O_PTR(tbuf);
	memset(ctxr, 0, sizeof(ctxr));
	ctxr[0] = T32O_PTR(tbuf + 0xd40);
	ctxr[1] = T32O_PTR(secbuf);
	ctxr[7] = k->zone_on ? T32O_PTR(zone_r) : 0;
	t32_data_e16b4[ch] = k->null_ctx ? 0 : T32O_PTR(ctxr);
	sr[0] = k->mode; sr[1] = k->la; sr[2] = k->lb; sr[3] = k->lc;
	memcpy(zone_r, zinit, ZB);
	memcpy(info_r, info_init, 20);
	memcpy(out_r, out_init, 36);
	memset(&rh, 0, sizeof(rh));
	rh.wait_ret = k->wait_ret;
	rh.vm_ret = k->vm_on ? T32O_PTR(new_r) : 0;

	switch (k->which) {
	case W_WAIT:
		ref_ret = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_awb_algo_waitdone, ch, 0, 0, 0, 0);
		break;
	case W_GETINFO:
		ref_ret = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_awb_algo_getinfo, T32O_PTR(out_r), 0, 0, 0, 0);
		break;
	case W_INIT:
		t32o_call((t32o_fn)(void (*)(void))tisp_awb_algo_init, ch, k->mode, T32O_PTR(info_r), 0, 0);
		break;
	case W_SLEEP:
		t32o_call((t32o_fn)(void (*)(void))tisp_awb_sleep, ch, 0, 0, 0, 0);
		for (i = 0; i < 2; i++)
			ref_sl[i] = t32_data_e1670[ch * 2 + i];
		for (i = 0; i < 4; i++)
			ref_sl[2 + i] = t32_data_e1630[ch * 4 + i];
		break;
	}
	memcpy(ref_sr, sr, 16);
	ref_z = ctxr[7];
	ref_printf = t32o_log.printfs;
	ref_logn = t32o_log.n;

	/* our side */
	memset(&oh, 0, sizeof(oh));
	/* the same glue results the vendor's stubs gave (not derived from its outputs) */
	oh.wait_ret = rh.wait_ret;
	oh.vm_on = k->vm_on;
	memcpy(hook_sr, ref_sr, 16);
	hook_exp[0] = k->out6;
	hook_exp[1] = k->lb;
	hook_exp[2] = k->out7;
	co.tune = tbuf + 0xd40;
	co.sec = secbuf;
	co.completion = ctxo_mem + 16;
	co.zone = k->zone_on ? zone_o : 0;
	so.mode = k->mode; so.lum_a = k->la; so.lum_b = k->lb; so.lum_c = k->lc;
	memcpy(zone_o, zinit, ZB);
	memcpy(info_o, info_init, 20);
	memcpy(out_o, out_init, 36);
	cur_ch = ch;

	compared++;
	switch (k->which) {
	case W_WAIT:
		ours_ret = (u32)t32_awb_algo_waitdone(&so, &co, &ops);
		if (ours_ret != ref_ret)
			BAD(what, "waitdone ref %d ours %d", (int)ref_ret, (int)ours_ret);
		if (mine_wait && (rh.wait_n != oh.wait_n || (oh.wait_n &&
		    (rh.wait_arg != oh.wait_arg || rh.wait_ms != oh.wait_ms))))
			BAD(what, "wait call ref %u/%u/%u ours %u/%u/%u", rh.wait_n, rh.wait_arg, rh.wait_ms,
			    oh.wait_n, oh.wait_arg, oh.wait_ms);
		break;
	case W_GETINFO:
		ours_zone = t32_awb_algo_getinfo(k->null_ctx ? 0 : &co, &so, out_o, &ops);
		if ((ref_ret != 0) != (ours_zone != 0))
			BAD(what, "getinfo ret ref %08x ours %p", ref_ret, (void *)ours_zone);
		if (ours_zone && (ref_ret - T32O_PTR(zone_r)) != (u32)(ours_zone - zone_o))
			BAD(what, "getinfo zone ptr");
		for (i = 0; i < 9; i++)
			if (out_r[i] != out_o[i])
				BAD(what, "out[%u] ref %08x ours %08x", i, out_r[i], out_o[i]);
		if (ref_sr[0] != so.mode || ref_sr[1] != so.lum_a || ref_sr[2] != so.lum_b || ref_sr[3] != so.lum_c)
			BAD(what, "state ref %08x %08x %08x %08x ours %08x %08x %08x %08x", ref_sr[0], ref_sr[1],
			    ref_sr[2], ref_sr[3], so.mode, so.lum_a, so.lum_b, so.lum_c);
		if (memcmp(zone_r, zone_o, ZB))
			BAD(what, "zone buffer differs");
		if ((ref_logn > 0) != (oh.lum_n > 0))
			BAD(what, "lum_th_freq: vendor writes %u ours calls %u", ref_logn, oh.lum_n);
		if (oh.pre_bad)
			BAD(what, "state handed to lum_th_freq is wrong");
		if (oh.lum_n && oh.lum_ch != ch)
			BAD(what, "lum_th_freq channel %u != %u", oh.lum_ch, ch);
		break;
	case W_INIT: {
		u32 cr = !ref_z ? 0 : (ref_z == T32O_PTR(zone_r) ? 1 : 2);
		u32 cu = !co.zone ? 0 : (co.zone == zone_o ? 1 : 2);

		t32_awb_algo_init(&co, &so, k->mode, info_o, &ops);
		cu = !co.zone ? 0 : (co.zone == zone_o ? 1 : 2);
		if (ref_sr[0] != so.mode || ref_sr[1] != so.lum_a || ref_sr[2] != so.lum_b || ref_sr[3] != so.lum_c)
			BAD(what, "init state ref %08x ours %08x", ref_sr[0], so.mode);
		if (cr != cu)
			BAD(what, "init zone class ref %u ours %u", cr, cu);
		if (memcmp(info_r, info_o, 20))
			BAD(what, "init info differs");
		(void)ref_printf;
		if ((k->mode == 1 && ref_z == 0) != (oh.oom_n != 0))
			BAD(what, "init no-memory message ref %d ours %u", k->mode == 1 && ref_z == 0, oh.oom_n);
		if (mine_initc && (rh.initc_n != oh.initc_n || (oh.initc_n && rh.initc_arg != oh.initc_arg)))
			BAD(what, "init_completion ref %u/%u ours %u/%u", rh.initc_n, rh.initc_arg, oh.initc_n, oh.initc_arg);
		if (mine_vfree && (rh.vfree_n != oh.vfree_n || (oh.vfree_n && rh.vfree_arg != oh.vfree_arg)))
			BAD(what, "vfree ref %u/%u ours %u/%u", rh.vfree_n, rh.vfree_arg, oh.vfree_n, oh.vfree_arg);
		if (mine_vm && (rh.vm_n != oh.vm_n || rh.vm_size != oh.vm_size))
			BAD(what, "vmalloc ref %u/%u ours %u/%u", rh.vm_n, rh.vm_size, oh.vm_n, oh.vm_size);
		break;
	}
	case W_SLEEP: {
		struct t32_awb_sleep_state s;
		u32 ov[6];

		t32_awb_sleep(&s, rd_reg, sys_rd);
		ov[0] = s.e1670[0]; ov[1] = s.e1670[1];
		for (i = 0; i < 4; i++)
			ov[2 + i] = s.e1630[i];
		for (i = 0; i < 6; i++)
			if (ov[i] != ref_sl[i])
				BAD(what, "sleep[%u] ref %08x ours %08x", i, ref_sl[i], ov[i]);
		break;
	}
	}
}

static void gen(struct cs *k, int which)
{
	u32 pick = rnd() % 5;

	k->which = which;
	k->ch = rnd() & 3;
	k->mode = pick < 2 ? 1 : (pick == 2 ? 0 : (pick == 3 ? 2 : rnd()));
	k->la = rnd(); k->lb = rnd(); k->lc = rnd();
	k->zone_on = rnd() & 1;
	k->null_ctx = (which == W_GETINFO && rnd() % 8 == 0);
	k->wait_ret = (rnd() & 1) ? 0 : 1 + rnd() % 7;
	k->vm_on = rnd() & 1;
	k->out6 = (rnd() & 1) ? k->la : rnd();
	k->out7 = (rnd() & 1) ? k->lc : rnd();
}

int main(void)
{
	const unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 3000;
	unsigned int c, n0;
	u32 b;
	int which;

	/* which glue functions are ours? */
	memset(&rh, 0, sizeof(rh));
	private_wait_for_completion_timeout(0, 0, 0, 0); mine_wait = rh.wait_n != 0;
	private_init_completion(0, 0, 0, 0); mine_initc = rh.initc_n != 0;
	private_vmalloc(0, 0, 0, 0); mine_vm = rh.vm_n != 0;
	private_vfree(0, 0, 0, 0); mine_vfree = rh.vfree_n != 0;
	if (!mine_wait || !mine_initc || !mine_vm || !mine_vfree) {
		fprintf(stderr, "t32_awbin_oracle: env glue linked instead of the oracle's\n");
		return 2;
	}

	for (c = 0; c < n; c++) {
		struct cs k;

		for (which = 0; which < W_N; which++) {
			gen(&k, which);
			run_case("random", &k);
		}
	}
	n0 = compared;

	/* boundaries: every mode class, zone / wait / vmalloc outcome, compare words equal or not */
	for (which = 0; which < W_N; which++)
		for (b = 0; b < 96; b++) {
			struct cs k;
			static const u32 modes[3] = { 0, 1, 2 };

			gen(&k, which);
			k.mode = modes[b % 3];
			k.zone_on = (b >> 2) & 1;
			k.wait_ret = (b >> 3) & 1;
			k.vm_on = (b >> 4) & 1;
			k.null_ctx = which == W_GETINFO && b >= 90;
			k.out6 = (b & 32) ? k.la : k.la + 1;
			k.out7 = (b & 64) ? k.lc : k.lc + 1;
			run_case("boundary", &k);
		}

	printf("t32_awbin_oracle: random %u compared, boundaries %u, glue ours wait/initc/vm/vfree %d%d%d%d, 0 mismatches\n",
	       n0, compared - n0, mine_wait, mine_initc, mine_vm, mine_vfree);
	return 0;
}
