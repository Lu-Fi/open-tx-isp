// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: DMSC per-frame paths.  Runs the unmodified vendor
 * functions (Matt Davis' recovered C, PR #15 @ 2e2e06bc) and our
 * t32_dmsc_{par_refresh,refresh,dn_params_refresh,wake_up} from
 * driver/t32/tx_isp_t32_dmsc.h; compares every register write, the section
 * bytes, the value block and the context fields.
 * Vendor context (t32_data_e1810[ch]): +0 section, +4 value block, +8 gain.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_dmsc.h"

#define SECB		T32_DMSC_SEC_BYTES
#define VALB		T32_DMSC_VALUES
#define SEC_OFF		27612u
#define MAXW		256

extern u32 t32_data_e1810[4];

extern u64 tisp_dmsc_par_refresh(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_dmsc_refresh(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_dmsc_dn_params_refresh(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_dmsc_wake_up(u32 a0, u32 a1, u32 a2, u32 a3);

static u32 rs = 0x51d3b7u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static unsigned int compared, failed;

enum { CASE_PAR, CASE_REFRESH, CASE_DN, CASE_WAKE };

static void gen_section(u8 *sec)
{
	unsigned int i;

	for (i = 0; i < SECB; i++)
		sec[i] = (i % 3) ? (u8)rnd() : (u8)(rnd() & 7);
}

static int run_case(const char *what, int which, u32 ch, u32 init_gain,
		    u32 gain, u32 thr, const u8 *section)
{
	u32 ref_sharp;
	u8 *base_ref, *par_ref, *ctx_ref, *base_our, *par_our;
	struct t32_dmsc_ctx *oc;
	u8 init_par[VALB];
	static u8 aft_ref_sec[SECB], aft_our_sec[SECB];
	u8 aft_ref_par[VALB], aft_our_par[VALB];
	u32 ref_gain, our_gain, ref_so, our_so;
	struct t32_front_write refw[MAXW], ow[MAXW];
	unsigned int ref_n, i;
	int our_n = 0;

	t32o_reset();
	base_ref = t32o_alloc(SEC_OFF + SECB + 64);
	if (!base_ref)
		return 2;
	/*
	 * Let the unmodified vendor tisp_dmsc_init set up the context and the
	 * block state (e.g. the sharpness ratio) the per-frame functions
	 * read, then restore the case section and gain.
	 */
	memcpy(base_ref + SEC_OFF, section, SECB);
	memcpy(base_ref + SEC_OFF, "\x11\xda\x34\x01", 4);
	t32_data_dd0a4[ch] = T32O_PTR(base_ref);
	t32o_call((t32o_fn)(void (*)(void))tisp_dmsc_init, ch, 0, 0, 0, 0);
	t32o_log.n = 0;
	if (t32_data_e1810[ch]) {
		ctx_ref = T32O_HOST(t32_data_e1810[ch]);
		par_ref = T32O_HOST(*(u32 *)(ctx_ref + 4));
	} else {
		par_ref = t32o_alloc(VALB + 16);
		ctx_ref = t32o_alloc(32);
		if (!par_ref || !ctx_ref)
			return 2;
		*(u32 *)(ctx_ref + 4) = T32O_PTR(par_ref);
		t32_data_e1810[ch] = T32O_PTR(ctx_ref);
	}
	memcpy(base_ref + SEC_OFF, section, SECB);
	memcpy(init_par, par_ref, VALB);
	*(u32 *)(ctx_ref + 0) = T32O_PTR(base_ref + SEC_OFF);
	*(u32 *)(ctx_ref + 8) = init_gain;
	t32_data_dd0a4[ch] = T32O_PTR(base_ref);

	switch (which) {
	case CASE_PAR:
		t32o_call((t32o_fn)(void (*)(void))tisp_dmsc_par_refresh, ch, gain, thr, 0, 0);
		break;
	case CASE_REFRESH:
		t32o_call((t32o_fn)(void (*)(void))tisp_dmsc_refresh, ch, gain, 0, 0, 0);
		break;
	case CASE_DN:
		t32o_call((t32o_fn)(void (*)(void))tisp_dmsc_dn_params_refresh, ch, 0, 0, 0, 0);
		break;
	default:
		t32o_call((t32o_fn)(void (*)(void))tisp_dmsc_wake_up, ch, 0, 0, 0, 0);
		break;
	}
	ref_n = t32o_log.n;
	if (ref_n > MAXW) {
		fprintf(stderr, "ref_n %u > MAXW\n", ref_n);
		return 2;
	}
	memcpy(refw, t32o_log.w, ref_n * sizeof(refw[0]));
	memcpy(aft_ref_par, par_ref, VALB);
	ref_gain = *(u32 *)(ctx_ref + 8);
	ref_sharp = *(u32 *)(ctx_ref + 16);	/* vendor ctx +16: 0x80 from tisp_dmsc_init, read by tisp_dmsc_intp */
	ref_so = *(u32 *)(ctx_ref + 0) - T32O_PTR(base_ref);
	memcpy(aft_ref_sec, base_ref + SEC_OFF, SECB);

	t32o_reset();
	base_our = t32o_alloc(SEC_OFF + SECB + 64);
	par_our = t32o_alloc(VALB + 16);
	oc = t32o_alloc(sizeof(*oc));
	if (!base_our || !par_our || !oc)
		return 2;
	memcpy(base_our + SEC_OFF, section, SECB);
	memcpy(par_our, init_par, VALB);
	oc->sec = base_our + SEC_OFF;
	oc->param = par_our;
	oc->gain = init_gain;
	oc->sharpness = ref_sharp;

	switch (which) {
	case CASE_PAR:
		our_n = t32_dmsc_par_refresh(oc, gain, thr, ow);
		break;
	case CASE_REFRESH:
		our_n = t32_dmsc_refresh(oc, gain, ow);
		break;
	case CASE_DN:
		our_n = t32_dmsc_dn_params_refresh(oc, base_our, ow);
		break;
	default:
		our_n = t32_dmsc_wake_up(oc, ow);
		break;
	}
	memcpy(aft_our_par, par_our, VALB);
	our_gain = oc->gain;
	our_so = (u32)((uintptr_t)oc->sec - (uintptr_t)base_our);
	memcpy(aft_our_sec, base_our + SEC_OFF, SECB);

	compared++;
	if (our_n < 0 || (unsigned int)our_n != ref_n)
		goto bad;
	for (i = 0; i < ref_n; i++)
		if (refw[i].reg != ow[i].reg || refw[i].val != ow[i].val)
			goto bad;
	if (ref_gain != our_gain || ref_so != our_so)
		goto bad;
	if (memcmp(aft_ref_par, aft_our_par, VALB) || memcmp(aft_ref_sec, aft_our_sec, SECB))
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
		printf("  gain ref %08x ours %08x, sec off ref %x ours %x\n",
		       ref_gain, our_gain, ref_so, our_so);
	}
	return 1;
}

static void run_boundaries(void)
{
	static u8 sec[SECB];

	gen_section(sec);
	run_case("par-n", CASE_PAR, 0, 0x20000, 0x20000 + 9, 10, sec);
	run_case("par-e", CASE_PAR, 0, 0x20000, 0x20000 + 10, 10, sec);
	run_case("par-p", CASE_PAR, 0, 0x20000, 0x20000 + 11, 10, sec);
	run_case("par-below", CASE_PAR, 1, 0x20000, 0x20000 - 10, 10, sec);
	run_case("par-neg", CASE_PAR, 0, 0xffffffffu, 0x30000, 10, sec);
	run_case("refresh-n", CASE_REFRESH, 0, 0x10000, 0x10000 + 255, 0, sec);
	run_case("refresh-e", CASE_REFRESH, 0, 0x10000, 0x10000 + 256, 0, sec);
	run_case("refresh-p", CASE_REFRESH, 0, 0x10000, 0x10000 + 257, 0, sec);
	run_case("refresh-neg", CASE_REFRESH, 0, 0xffffffffu, 0x10000, 0, sec);
	run_case("dn", CASE_DN, 0, 0x10000, 0, 0, sec);
	run_case("wake", CASE_WAKE, 0, 0x10000, 0, 0, sec);
}

int main(void)
{
	const unsigned int n = getenv("T32O_NRAND") ?
		(unsigned int)atoi(getenv("T32O_NRAND")) : 3000;
	unsigned int c;
	static u8 sec[SECB];

	for (c = 0; c < n; c++) {
		u32 ch = rnd() & 3;
		u32 init_gain = (c % 5 == 0) ? 0xffffffffu : (rnd() & 0xfffff);
		u32 gain = (c % 2) ? (rnd() & 0xfffff) : init_gain + (rnd() & 0x3ff);
		u32 thr = (c % 3) ? (rnd() & 0x3ff) : (rnd() & 0xfffff);

		gen_section(sec);
		run_case("par-random", CASE_PAR, ch, init_gain, gain, thr, sec);
		gen_section(sec);
		run_case("refresh-random", CASE_REFRESH, ch, init_gain, gain, 0, sec);
		gen_section(sec);
		run_case("dn-random", CASE_DN, ch, init_gain, 0, 0, sec);
		gen_section(sec);
		run_case("wake-random", CASE_WAKE, ch, init_gain, 0, 0, sec);
	}
	run_boundaries();

	printf("t32_dmscf_oracle: random %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
