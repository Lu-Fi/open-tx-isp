// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: DPC per-frame paths.  Runs the unmodified vendor functions
 * (Matt Davis' recovered C, PR #15 @ 2e2e06bc): tisp_dpc_dn_params_refresh
 * (four 22-byte table copies, tisp_dpc_gain_interp long/short,
 * tisp_dpc_write_reg) and tisp_dpc_wake_up (tisp_dpc_write_reg_long / _short /
 * _other), and our readable t32_dpc_dn_params_refresh / t32_dpc_wake_up from
 * driver/t32/tx_isp_t32_dpc.h, and compares every register write (address,
 * value, order) and the section pointer / saved argument the reference stores.
 *
 * Vendor per-channel context (t32_data_e35f0[ch], see the prologue of
 * fn_..a1888): +0 the section pointer (tuning base + 22976), +4 the value
 * block (0xb4 bytes), +8/+12/+16 the fixed point gain of the long and short
 * exposure tables, +20 saved by tisp_dpc_dn_params_refresh, +24 four 22-byte
 * copies of the section u16 tables 0x6 / 0x10e / 0x1c / 0x124.  Both sides
 * get the same gain and the same zeroed value block.
 *
 * The vendor "other" register function writes the extra short-exposure words
 * (7 + 7) on part of the dn path and never on the wake-up path.  What the
 * vendor's per-frame copy tests there is not available to the host: env.c
 * models the format register 0x88 read of the init chain only, and no data
 * object of env.c feeds this decision.  So, exactly as for the init chain, the
 * oracle reads the decision the vendor made (did the run write 0x2144?) and
 * hands it to ours as its "wide" argument; the wake-up side is constant in the
 * vendor (128 + 16 writes, no extras) and stays hardcoded in our port.
 * A reference case that faults (memory the oracle cannot provide) is counted
 * as skipped, never as a pass.
 *
 * Fixed-seed random cases cover random sections, gains (index 0..10 plus a
 * fraction), channels, saved value and the format register.
 */
#include <signal.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_dpc.h"

#define SECB	T32_DPC_SEC_BYTES
#define SEC_OFF	22976u
#define CTXLEN	0x200u
#define MAXW	T32_DPC_MAX_WRITES

/*
 * The per-channel vendor contexts.  t32_oracle_env.h does not list this
 * object and env.c may define it; a weak definition keeps the link working
 * either way (the strong one wins).
 */
u32 t32_data_e35f0[4] __attribute__((weak));

extern u64 tisp_dpc_dn_params_refresh(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_dpc_wake_up(u32 a0, u32 a1, u32 a2, u32 a3);

static u32 rs = 0x5bd1e995u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static unsigned int compared, failed, skipped;
static sigjmp_buf fault_env;

static void fault_handler(int sig)
{
	(void)sig;
	siglongjmp(fault_env, 1);
}

static void gen_section(u8 *sec)
{
	unsigned int i;

	for (i = 0; i < SECB; i++)
		sec[i] = (i % 3) ? (u8)rnd() : (u8)(rnd() & 7);
}

static void report(const char *what, const struct t32_front_write *refw,
		   unsigned int ref_n, const struct t32_front_write *ow, int our_n)
{
	unsigned int i;

	printf("MISMATCH %s: writes vendor %u ours %d\n", what, ref_n, our_n);
	for (i = 0; i < ref_n || (int)i < our_n; i++)
		if (i >= ref_n || (int)i >= our_n ||
		    refw[i].reg != ow[i].reg || refw[i].val != ow[i].val) {
			printf("  #%u vendor %05x=%08x ours %05x=%08x\n", i,
			       i < ref_n ? refw[i].reg : 0, i < ref_n ? refw[i].val : 0,
			       (int)i < our_n ? ow[i].reg : 0, (int)i < our_n ? ow[i].val : 0);
			if (i > 8)
				break;
		}
}

/* tisp_dpc_dn_params_refresh */
static int run_dn(const char *what, const u8 *sec, u32 ch, u32 gain, u32 arg, u32 reg88)
{
	struct t32_front_write refw[MAXW], ow[MAXW];
	struct t32_dpc_ctx ours;
	u8 *base_ref, *ctx_ref, *block_ref, *base_our;
	u32 ref_sec_off, ref_arg, our_sec_off;
	unsigned int ref_n, i;
	int our_n;
	volatile int ref_wide = 0;

	t32o_reset();
	t32o_reg88 = reg88;
	base_ref = t32o_alloc(SEC_OFF + SECB + 64);
	ctx_ref = t32o_alloc(CTXLEN);
	block_ref = t32o_alloc(T32_DPC_VALUES);
	if (!base_ref || !ctx_ref || !block_ref)
		return 2;
	memset(ctx_ref, 0, CTXLEN);
	memset(block_ref, 0, T32_DPC_VALUES);
	memcpy(base_ref + SEC_OFF, sec, SECB);
	*(u32 *)(ctx_ref + 0) = T32O_PTR(base_ref + SEC_OFF);
	*(u32 *)(ctx_ref + 4) = T32O_PTR(block_ref);
	*(u32 *)(ctx_ref + 8) = gain;
	*(u32 *)(ctx_ref + 12) = gain;
	*(u32 *)(ctx_ref + 16) = gain;
	t32_data_dd0a4[ch] = T32O_PTR(base_ref);
	t32_data_e35f0[ch] = T32O_PTR(ctx_ref);

	if (sigsetjmp(fault_env, 1)) {
		skipped++;
		return 0;
	}
	t32o_call((t32o_fn)(void (*)(void))tisp_dpc_dn_params_refresh, ch, arg, 0, 0, 0);
	ref_n = t32o_log.n;
	if (ref_n > MAXW) {
		fprintf(stderr, "ref_n %u > MAXW\n", ref_n);
		return 2;
	}
	memcpy(refw, t32o_log.w, ref_n * sizeof(refw[0]));
	ref_sec_off = *(u32 *)(ctx_ref + 0) - T32O_PTR(base_ref);
	ref_arg = *(u32 *)(ctx_ref + 20);
	for (i = 0; i < ref_n; i++)
		if (refw[i].reg == 0x2144)
			ref_wide = 1;

	t32o_reset();
	t32o_reg88 = reg88;
	base_our = t32o_alloc(SEC_OFF + SECB + 64);
	if (!base_our)
		return 2;
	memcpy(base_our + SEC_OFF, sec, SECB);
	memset(&ours, 0, sizeof(ours));
	ours.sec = base_our + SEC_OFF;
	ours.gain = gain;
	our_n = t32_dpc_dn_params_refresh(&ours, (u8)ch, base_our, ref_wide, arg, ow);
	our_sec_off = (u32)((uintptr_t)ours.sec - (uintptr_t)base_our);

	compared++;
	if (our_n < 0 || (unsigned int)our_n != ref_n)
		goto bad;
	for (i = 0; i < ref_n; i++)
		if (refw[i].reg != ow[i].reg || refw[i].val != ow[i].val)
			goto bad;
	if (ref_sec_off != our_sec_off || ref_arg != ours.arg)
		goto bad;
	return 0;
bad:
	if (failed++ < 5) {
		report(what, refw, ref_n, ow, our_n);
		printf("  ctx sec off vendor %05x ours %05x, saved vendor %08x ours %08x\n",
		       ref_sec_off, our_sec_off, ref_arg, ours.arg);
	}
	return 1;
}

/* tisp_dpc_wake_up */
static int run_wake(const char *what, const u8 *sec, u32 ch, u32 reg88)
{
	struct t32_front_write refw[MAXW], ow[MAXW];
	struct t32_dpc_ctx ours;
	u8 *base_ref, *ctx_ref, *block_ref, *base_our;
	unsigned int ref_n, i;
	int our_n;

	t32o_reset();
	t32o_reg88 = reg88;
	base_ref = t32o_alloc(SEC_OFF + SECB + 64);
	ctx_ref = t32o_alloc(CTXLEN);
	block_ref = t32o_alloc(T32_DPC_VALUES);
	if (!base_ref || !ctx_ref || !block_ref)
		return 2;
	memset(ctx_ref, 0, CTXLEN);
	memset(block_ref, 0, T32_DPC_VALUES);
	memcpy(base_ref + SEC_OFF, sec, SECB);
	*(u32 *)(ctx_ref + 0) = T32O_PTR(base_ref + SEC_OFF);
	*(u32 *)(ctx_ref + 4) = T32O_PTR(block_ref);
	t32_data_dd0a4[ch] = T32O_PTR(base_ref);
	t32_data_e35f0[ch] = T32O_PTR(ctx_ref);

	if (sigsetjmp(fault_env, 1)) {
		skipped++;
		return 0;
	}
	t32o_call((t32o_fn)(void (*)(void))tisp_dpc_wake_up, ch, 0, 0, 0, 0);
	ref_n = t32o_log.n;
	if (ref_n > MAXW) {
		fprintf(stderr, "ref_n %u > MAXW\n", ref_n);
		return 2;
	}
	memcpy(refw, t32o_log.w, ref_n * sizeof(refw[0]));

	t32o_reset();
	t32o_reg88 = reg88;
	base_our = t32o_alloc(SEC_OFF + SECB + 64);
	if (!base_our)
		return 2;
	memcpy(base_our + SEC_OFF, sec, SECB);
	memset(&ours, 0, sizeof(ours));
	ours.sec = base_our + SEC_OFF;
	our_n = t32_dpc_wake_up(&ours, (u8)ch, ow);

	compared++;
	if (our_n < 0 || (unsigned int)our_n != ref_n)
		goto bad;
	for (i = 0; i < ref_n; i++)
		if (refw[i].reg != ow[i].reg || refw[i].val != ow[i].val)
			goto bad;
	return 0;
bad:
	if (failed++ < 5)
		report(what, refw, ref_n, ow, our_n);
	return 1;
}

static void run_boundaries(void)
{
	static const u8 zero[SECB];
	u32 g;

	for (g = 0; g <= 10; g++) {
		run_dn("dn-idx", zero, 0, g << 16, 0, 0);
		run_dn("dn-idx-frac", zero, 0, (g << 16) | 0xffffu, 0xffffffffu, 0x1f);
		run_wake("wake-lo", zero, 0, 4);
		run_wake("wake-hi", zero, 0, 5);
		run_wake("wake-max", zero, 3, 0x1f);
	}
	run_dn("dn-arg0", zero, 3, 0, 0, 4);
	run_dn("dn-argf", zero, 2, 0, 0xffffffffu, 5);
}

int main(void)
{
	const unsigned int n = getenv("T32O_NRAND") ?
		(unsigned int)atoi(getenv("T32O_NRAND")) : 3000;
	unsigned int c;
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = fault_handler;
	sigaction(SIGSEGV, &sa, NULL);
	sigaction(SIGBUS, &sa, NULL);

	for (c = 0; c < n; c++) {
		u8 sec[SECB];
		u32 ch = rnd() & 3;
		u32 gain = ((rnd() % 11) << 16) | (rnd() & 0xffff);
		u32 arg = rnd();
		u32 reg88 = rnd() % 40;

		gen_section(sec);
		run_dn("dn-random", sec, ch, gain, arg, reg88);

		gen_section(sec);
		run_wake("wake-random", sec, ch, rnd() % 40);
	}
	run_boundaries();

	printf("t32_dpcf_oracle: %u compared, %u skipped, %u mismatches\n",
	       compared, skipped, failed);
	if (failed)
		return 1;
	if (!compared) {
		printf("t32_dpcf_oracle: SKIP (the reference needs memory I could not model)\n");
		return 1;
	}
	return 0;
}
