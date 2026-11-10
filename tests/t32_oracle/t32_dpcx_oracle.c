// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: DPC per-frame paths.  Runs the unmodified vendor functions
 * (Matt Davis' recovered C, PR #15 @ 2e2e06bc) tisp_dpc_l_gain_update,
 * tisp_dpc_s_gain_update and tisp_dpc_wdr_en and our readable
 * t32_dpc_{l_gain_update,s_gain_update,wdr_en} from
 * driver/t32/tx_isp_t32_dpc.h, and compares every register write and the
 * changed context fields.
 *
 * The vendor context (t32_data_e35f0[ch]) is 0x80 bytes: +0 section pointer,
 * +4 value block pointer, +8 WDR enable byte, +12 long gain, +16 short gain,
 * +20 argument, +24 four 11 point u16 tables.  Fixed-seed random cases cover
 * random gains, WDR enables, channels and tuning bytes.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_dpc.h"

#define SECB		T32_DPC_SEC_BYTES
#define SEC_OFF		0x59c0u
#define MAXW		T32_DPC_MAX_WRITES

/* env.c globals the reference functions touch */
extern u32 t32_data_e35f0[4];

/* unmodified vendor functions from ref/ (Matt Davis' recovered C) */
extern u64 tisp_dpc_l_gain_update(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_dpc_s_gain_update(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_dpc_wdr_en(u32 a0, u32 a1, u32 a2, u32 a3);

static u32 rs = 0x7a19c3u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static unsigned int compared, failed;

enum {
	CASE_L,
	CASE_S,
	CASE_WDR
};

static void gen_random_section(u8 *sec)
{
	unsigned int i;

	for (i = 0; i < SECB; i++)
		sec[i] = (i % 3) ? (u8)rnd() : (u8)(rnd() & 7);
}

static int run_case(const char *what, int which, u32 ch, u32 gain, u32 en,
		    const u8 *section)
{
	u8 *base_ref, *o_ref, *ctx_ref;
	u8 *base_our, *o_our;
	struct t32_dpc_ctx *our_ctx;
	u8 initial_o[T32_DPC_VALUES], after_ref_o[T32_DPC_VALUES], after_our_o[T32_DPC_VALUES];
	u8 initial_tbl[88], after_ref_tbl[88], after_our_tbl[88];
	u32 ref_gain = 0, our_gain = 0, ref_gain1 = 0, our_gain1 = 0;
	u32 ref_wdr = 0, our_wdr = 0;
	struct t32_front_write refw[MAXW], ow[MAXW];
	unsigned int ref_n = 0, i;
	int our_n = 0;

	(void)what;

	/* Reference side: vendor context is a 0x80-byte struct. */
	t32o_reset();
	base_ref = t32o_alloc(SEC_OFF + SECB + 64);
	if (!base_ref)
		return 2;
	memcpy(base_ref + SEC_OFF, section, SECB);
	o_ref = t32o_alloc(T32_DPC_VALUES + 16);
	ctx_ref = t32o_alloc(0x80);
	if (!o_ref || !ctx_ref)
		return 2;
	for (i = 0; i < T32_DPC_VALUES; i++)
		o_ref[i] = (u8)rnd();
	memcpy(initial_o, o_ref, T32_DPC_VALUES);
	for (i = 0; i < 88; i++)
		ctx_ref[24 + i] = (u8)rnd();
	memcpy(initial_tbl, ctx_ref + 24, 88);

	*(u32 *)(ctx_ref + 0)  = T32O_PTR(base_ref + SEC_OFF);
	*(u32 *)(ctx_ref + 4)  = T32O_PTR(o_ref);
	ctx_ref[8] = (u8)en;
	*(u32 *)(ctx_ref + 12) = gain;
	*(u32 *)(ctx_ref + 16) = gain;
	*(u32 *)(ctx_ref + 20) = 0;

	t32_data_e35f0[ch] = T32O_PTR(ctx_ref);
	t32_data_dd0a4[ch] = T32O_PTR(base_ref);

	switch (which) {
	case CASE_L:
		t32o_call((t32o_fn)(void (*)(void))tisp_dpc_l_gain_update,
			  ch, 0, gain, 0, 0);
		break;
	case CASE_S:
		t32o_call((t32o_fn)(void (*)(void))tisp_dpc_s_gain_update,
			  ch, 0, gain, 0, 0);
		break;
	case CASE_WDR:
		t32o_call((t32o_fn)(void (*)(void))tisp_dpc_wdr_en,
			  ch, en, 0, 0, 0);
		break;
	}
	ref_n = t32o_log.n;
	if (ref_n > MAXW) {
		fprintf(stderr, "ref_n %u > MAXW\n", ref_n);
		return 2;
	}
	memcpy(refw, t32o_log.w, ref_n * sizeof(refw[0]));
	memcpy(after_ref_o, o_ref, T32_DPC_VALUES);
	memcpy(after_ref_tbl, ctx_ref + 24, 88);
	ref_gain = *(u32 *)(ctx_ref + 12);
	ref_gain1 = *(u32 *)(ctx_ref + 16);
	ref_wdr = ctx_ref[8];

	/* Our side: native struct context. */
	t32o_reset();
	base_our = t32o_alloc(SEC_OFF + SECB + 64);
	if (!base_our)
		return 2;
	memcpy(base_our + SEC_OFF, section, SECB);
	o_our = t32o_alloc(T32_DPC_VALUES + 16);
	our_ctx = t32o_alloc(sizeof(*our_ctx));
	if (!o_our || !our_ctx)
		return 2;
	our_ctx->sec = base_our + SEC_OFF;
	our_ctx->o = o_our;
	our_ctx->rsvd0 = 0;
	our_ctx->gain = gain;
	our_ctx->gain1 = gain;
	our_ctx->arg = 0;
	our_ctx->wdr = (u8)en;
	memcpy(o_our, initial_o, T32_DPC_VALUES);
	memcpy(our_ctx->tbl, initial_tbl, 88);

	switch (which) {
	case CASE_L:
		our_n = t32_dpc_l_gain_update(our_ctx, (u8)ch, gain, ow);
		break;
	case CASE_S:
		our_n = t32_dpc_s_gain_update(our_ctx, (u8)ch, gain, ow);
		break;
	case CASE_WDR:
		our_n = t32_dpc_wdr_en(our_ctx, (u8)ch, (u8)en, ow);
		break;
	}
	memcpy(after_our_o, o_our, T32_DPC_VALUES);
	memcpy(after_our_tbl, our_ctx->tbl, 88);
	our_gain = our_ctx->gain;
	our_gain1 = our_ctx->gain1;
	our_wdr = our_ctx->wdr;

	compared++;
	if (our_n < 0 || (unsigned int)our_n != ref_n)
		goto bad;
	for (i = 0; i < ref_n; i++)
		if (refw[i].reg != ow[i].reg || refw[i].val != ow[i].val)
			goto bad;
	if (ref_gain != our_gain || ref_gain1 != our_gain1 || ref_wdr != our_wdr)
		goto bad;
	if (memcmp(after_ref_o, after_our_o, T32_DPC_VALUES))
		goto bad;
	if (memcmp(after_ref_tbl, after_our_tbl, 88))
		goto bad;
	return 0;
bad:
	if (failed++ < 5) {
		printf("MISMATCH %s: writes ref %u ours %d\n", what, ref_n, our_n);
		for (i = 0; i < ref_n || (int)i < our_n; i++)
			if (i >= ref_n || (int)i >= our_n ||
			    refw[i].reg != ow[i].reg || refw[i].val != ow[i].val) {
				printf("  #%u vendor %05x=%08x ours %05x=%08x\n", i,
				       i < ref_n ? refw[i].reg : 0, i < ref_n ? refw[i].val : 0,
				       (int)i < our_n ? ow[i].reg : 0, (int)i < our_n ? ow[i].val : 0);
				if (i > 8)
					break;
			}
		printf("  ctx gain ref %08x ours %08x, gain1 ref %08x ours %08x, wdr ref %02x ours %02x\n",
		       ref_gain, our_gain, ref_gain1, our_gain1, ref_wdr, our_wdr);
	}
	return 1;
}

static void run_boundaries(void)
{
	static const u8 zero[SECB];

	run_case("l-zero", CASE_L, 0, 0, 0, zero);
	run_case("l-one", CASE_L, 0, 0x10000, 0, zero);
	run_case("s-zero", CASE_S, 0, 0, 0, zero);
	run_case("s-one", CASE_S, 0, 0x10000, 0, zero);
	run_case("wdr-0", CASE_WDR, 0, 0, 0, zero);
	run_case("wdr-1", CASE_WDR, 0, 0, 1, zero);
	run_case("wdr-2", CASE_WDR, 0, 0, 2, zero);
	run_case("wdr-ff", CASE_WDR, 0, 0, 0xff, zero);
}

int main(void)
{
	const unsigned int n = getenv("T32O_NRAND") ?
		(unsigned int)atoi(getenv("T32O_NRAND")) : 3000;
	unsigned int c;

	for (c = 0; c < n; c++) {
		u8 sec[SECB];
		u32 ch = rnd() & 3;
		u32 gain = rnd();
		u32 en = rnd() & 3;

		gen_random_section(sec);
		run_case("l-random", CASE_L, ch, gain, 0, sec);

		gen_random_section(sec);
		run_case("s-random", CASE_S, ch, gain, 0, sec);

		gen_random_section(sec);
		run_case("wdr-random", CASE_WDR, ch, gain, en, sec);
	}
	run_boundaries();

	printf("t32_dpcx_oracle: random %u compared, %u mismatches\n", compared, failed);
	if (failed)
		return 1;
	return 0;
}
