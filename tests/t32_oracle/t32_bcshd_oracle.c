// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: BCSH per-frame paths.  Runs the unmodified vendor
 * functions (Matt Davis' recovered C, PR #15 @ 2e2e06bc) and our readable
 * t32_bcsh_{dn_params_refresh,wdr_en,wake_up} from
 * driver/t32/tx_isp_t32_bcsh.h and compares every register write and the
 * changed context fields.
 *
 * The vendor keeps its state in a 24-byte block (t32_data_e2450[ch] -> ctx,
 * ctx[0] = the block state, ctx[1] = the parameter set, ctx[2] = the user/csc
 * set, ctx[3] = the exposure bands).  Our struct t32_bcsh mirrors it.  The
 * oracle builds both from the same initial state, runs the vendor function
 * and ours, and compares the write log, the parameter set, the user/csc set,
 * the exposure bands, the matrix block and the caches/counters.
 *
 * Fixed-seed random cases cover random gain/ev/day-night inputs and random
 * tuning bytes.  Boundary cases exercise the exposure clamp and the
 * hysteresis of the two interpolation paths.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_bcsh.h"

#define SECB		T32_BCSH_SEC_BYTES
#define SEC_OFF		0xd154u
#define MAXW		(T32_BCSH_MAX_WRITES + 16)

/* env.c globals the reference functions touch */
extern u32 t32_data_e2450[4];

/* unmodified vendor functions from ref/ (Matt Davis' recovered C) */
extern u64 tisp_bcsh_dn_params_refresh(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_bcsh_wdr_en(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_bcsh_wake_up(u32 a0, u32 a1, u32 a2, u32 a3);

static u32 rs = 0x5b3c91u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static void put32(u8 *p, u32 v)
{
	p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
}

static unsigned int compared, failed;

enum {
	CASE_DN,
	CASE_WDR,
	CASE_WAKE
};

static int run_case(const char *what, int which, u32 ch, const u8 *sec, const u8 *lut,
		    u32 lutbytes, const u8 *csc, u32 ev_lo, u32 ev_hi, u32 ct, u32 ct_code,
		    u32 ctr_ev, u32 ctr_ct, u32 pend, u32 b19)
{
	static struct t32_front_write refw[MAXW], ow[MAXW];
	u8 *base_ref, *base_our, *outer_ref, *caches_ref, *lut_ref, *info_ref;
	u8 *param_ref, *ap_ref, *p5_ref, *m112_ref;
	struct t32_bcsh *our;
	u8 cp_ref[300], ap_ref_b[0x90], p5_ref_b[48], m112_ref_b[112];
	u8 cp_ini[300], ap_ini[0x90], p5_ini[48], m112_ini[112];
	u8 sec_after[SECB];
	u32 ev_lo_ref, ev_hi_ref, ct_ref, ct_code_ref;
	u8 ctr_ev_ref, ctr_ct_ref, pend_ref, b19_ref;
	unsigned int ref_n, i;
	int our_n = 0;

	(void)what;
	(void)csc;

	/* initial state for both sides */
	memset(cp_ini, 0, sizeof(cp_ini));
	memset(ap_ini, 0, sizeof(ap_ini));
	memset(p5_ini, 0, sizeof(p5_ini));
	memset(m112_ini, 0, sizeof(m112_ini));

	/* ---- reference side ---- */
	t32o_reset();
	base_ref = t32o_alloc(0x10000);
	outer_ref = t32o_alloc(0x80);
	caches_ref = t32o_alloc(0x40);
	memset(outer_ref, 0, 0x80);
	memset(caches_ref, 0, 0x40);
	param_ref = t32o_alloc(0x400);
	ap_ref = t32o_alloc(0x400);
	p5_ref = t32o_alloc(0x400);
	m112_ref = t32o_alloc(0x400);
	lut_ref = t32o_alloc(lutbytes + 8);
	info_ref = t32o_alloc(64);
	if (!base_ref || !outer_ref || !caches_ref || !param_ref || !ap_ref || !p5_ref ||
	    !m112_ref || !lut_ref || !info_ref)
		return 2;
	memset(info_ref, 0, 64);
	put32(info_ref + 28, T32O_PTR(lut_ref));

	memcpy(base_ref + SEC_OFF, sec, SECB);
	memcpy(lut_ref, lut, lutbytes);
	memcpy(param_ref, cp_ini, 300);
	memcpy(ap_ref, ap_ini, 0x90);
	memcpy(p5_ref, p5_ini, 48);
	memcpy(m112_ref, m112_ini, 112);

	/*
	 * The vendor's 24-byte block (t32_data_e2450[ch]) points to an outer
	 * struct: +0 sec, +4 caches/counters, +8 param set, +12 ap,
	 * +16 matrix block, +20 exposure bands, +24 spinlock.
	 */
	put32(outer_ref + 0, T32O_PTR(base_ref + SEC_OFF));
	put32(outer_ref + 4, T32O_PTR(caches_ref));
	put32(outer_ref + 8, T32O_PTR(param_ref));
	put32(outer_ref + 12, T32O_PTR(m112_ref));
	put32(outer_ref + 16, T32O_PTR(ap_ref));
	put32(outer_ref + 20, T32O_PTR(p5_ref));

	/* caches block: ev lo/hi, ct, ct_code, ctr_ev, ctr_ct, pend, b19 */
	put32(caches_ref + 0, ev_lo);
	put32(caches_ref + 4, ev_hi);
	put32(caches_ref + 8, ct);
	put32(caches_ref + 12, ct_code);
	caches_ref[16] = (u8)ctr_ev;
	caches_ref[17] = (u8)ctr_ct;
	caches_ref[18] = (u8)pend;
	caches_ref[19] = (u8)b19;

	t32_data_e2450[ch] = T32O_PTR(outer_ref);
	t32_data_dd0a4[ch] = T32O_PTR(base_ref);
	t32_data_dc274[ch] = T32O_PTR(info_ref);

	switch (which) {
	case CASE_DN:
		t32o_call((t32o_fn)(void (*)(void))tisp_bcsh_dn_params_refresh, ch, 0, 0, 0, 0);
		break;
	case CASE_WDR:
		t32o_call((t32o_fn)(void (*)(void))tisp_bcsh_wdr_en, ch, 0, 0, 0, 0);
		break;
	case CASE_WAKE:
		t32o_call((t32o_fn)(void (*)(void))tisp_bcsh_wake_up, ch, 0, 0, 0, 0);
		break;
	}

	ref_n = t32o_log.n;
	if (ref_n > MAXW) {
		fprintf(stderr, "ref_n %u > MAXW\n", ref_n);
		return 2;
	}
	memcpy(refw, t32o_log.w, ref_n * sizeof(refw[0]));
	memcpy(sec_after, base_ref + SEC_OFF, SECB);
	memcpy(cp_ref, param_ref, 300);
	memcpy(ap_ref_b, ap_ref, 0x90);
	memcpy(p5_ref_b, p5_ref, 48);
	memcpy(m112_ref_b, m112_ref, 112);
	ev_lo_ref = *(u32 *)(caches_ref + 0);
	ev_hi_ref = *(u32 *)(caches_ref + 4);
	ct_ref = *(u32 *)(caches_ref + 8);
	ct_code_ref = *(u32 *)(caches_ref + 12);
	ctr_ev_ref = caches_ref[16];
	ctr_ct_ref = caches_ref[17];
	pend_ref = caches_ref[18];
	b19_ref = caches_ref[19];

	/* ---- our side ---- */
	t32o_reset();
	base_our = t32o_alloc(0x10000);
	our = t32o_alloc(sizeof(*our));
	if (!base_our || !our)
		return 2;
	memset(our, 0, sizeof(*our));
	memcpy(base_our + SEC_OFF, sec, SECB);
	our->sec = base_our + SEC_OFF;
	our->lut = lut;		/* original input lut remains valid */
	our->lutbytes = lutbytes;
	memcpy(our->cp, cp_ini, 300);
	memcpy(our->ap, ap_ini, 0x90);
	memcpy(our->p5, p5_ini, 48);
	memcpy(our->m112, m112_ini, 112);
	our->ev_lo = ev_lo;
	our->ev_hi = ev_hi;
	our->ct = ct;
	our->ct_code = ct_code;
	our->ctr_ev = (u8)ctr_ev;
	our->ctr_ct = (u8)ctr_ct;
	our->pend = (u8)pend;
	our->b19 = (u8)b19;

	switch (which) {
	case CASE_DN:
		t32_bcsh_dn_params_refresh(our, (u8)ch, base_our);
		break;
	case CASE_WDR:
		t32_bcsh_wdr_en(our, (u8)ch);
		break;
	case CASE_WAKE:
		our_n = t32_bcsh_wake_up(our, ow);
		break;
	}

	compared++;
	if (which == CASE_WAKE) {
		if (our_n < 0 || (unsigned int)our_n != ref_n)
			goto bad;
		for (i = 0; i < ref_n; i++)
			if (refw[i].reg != ow[i].reg || refw[i].val != ow[i].val)
				goto bad;
	} else {
		if (ref_n != 0)
			goto bad;
	}
	if (memcmp(sec_after, base_our + SEC_OFF, SECB)) {
		if (failed < 3)
			printf("  field: sec\n");
		goto bad;
	}
	if (memcmp(cp_ref, our->cp, 300)) {
		if (failed < 3) {
			for (i = 0; i < 300; i++)
				if (cp_ref[i] != our->cp[i]) {
					printf("  field: cp+0x%x ref %02x ours %02x\n", i, cp_ref[i], our->cp[i]);
					break;
				}
		}
		goto bad;
	}
	if (memcmp(ap_ref_b, our->ap, 0x90)) {
		if (failed < 3) {
			for (i = 0; i < 0x90; i++)
				if (ap_ref_b[i] != our->ap[i]) {
					printf("  field: ap+0x%x ref %02x ours %02x (0x496=%u)\n", i, ap_ref_b[i], our->ap[i], sec[0x496]);
					break;
				}
		}
		goto bad;
	}
	if (memcmp(p5_ref_b, our->p5, 48)) {
		if (failed < 3)
			printf("  field: p5\n");
		goto bad;
	}
	if (memcmp(m112_ref_b, our->m112, 112)) {
		if (failed < 3)
			printf("  field: m112\n");
		goto bad;
	}
	if (ev_lo_ref != our->ev_lo || ev_hi_ref != our->ev_hi)
		goto bad;
	if (ct_ref != our->ct || ct_code_ref != our->ct_code)
		goto bad;
	if (ctr_ev_ref != our->ctr_ev || ctr_ct_ref != our->ctr_ct)
		goto bad;
	if (pend_ref != our->pend || b19_ref != our->b19)
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
		printf("  pend ref %u ours %u, ct ref %08x ours %08x\n",
		       pend_ref, our->pend, ct_ref, our->ct);
	}
	return 1;
}

static void gen_random_section(u8 *sec)
{
	unsigned int i;

	for (i = 0; i < SECB; i++)
		sec[i] = (i % 3) ? (u8)rnd() : (u8)(rnd() & 7);
}

static void run_boundaries(void)
{
	static u8 sec[SECB], lut[10 * 40], csc[104];
	unsigned int i;

	memset(sec, 0, sizeof(sec));
	memset(lut, 0, sizeof(lut));
	memset(csc, 0, sizeof(csc));
	sec[0x4af] = 1;
	sec[0x4b1] = 0;
	sec[0x492] = 1;
	sec[0x493] = 1;
	sec[0x496] = 0;
	sec[0x498] = 1;
	sec[0x499] = 0;
	sec[0x49e] = 1;
	sec[0x49f] = 0;
	sec[0x461] = 1;
	sec[0x467] = 1;
	sec[0x45d] = 1;
	sec[0x466] = 1;
	sec[0x49d] = 1;
	sec[0x497] = 1;
	sec[0x49c] = 0x80;
	sec[0x45a] = 1;
	sec[0x45b] = 1;
	sec[0x45c] = 1;
	sec[0x45e] = 0x80;
	sec[0x45f] = 1;
	sec[0x460] = 1;
	sec[0x465] = 1;
	sec[0x4b0] = 0;
	for (i = 0; i < 10; i++)
		put32(lut + 40 * i, 2000 + 500 * i);
	for (i = 0; i < 9; i++)
		put32(lut + 4 + 4 * i, 0x1000);
	csc[0x5b] = 0;

	run_case("dn-zero", CASE_DN, 0, sec, lut, 40, csc, 0, 0, 5000, 255, 0, 0, 0, 0);
	run_case("wdr-zero", CASE_WDR, 0, sec, lut, 40, csc, 0, 0, 5000, 255, 0, 0, 0, 0);
	run_case("wake-zero", CASE_WAKE, 0, sec, lut, 40, csc, 0, 0, 5000, 255, 0, 0, 0, 0);
	run_case("dn-pend", CASE_DN, 0, sec, lut, 40, csc, 0, 0, 5000, 255, 0, 0, 5, 0);
	run_case("wdr-pend", CASE_WDR, 0, sec, lut, 40, csc, 0, 0, 5000, 255, 0, 0, 5, 0);
	run_case("dn-b19", CASE_DN, 0, sec, lut, 40, csc, 0, 0, 5000, 255, 0, 0, 0, 1);
	run_case("wdr-b19", CASE_WDR, 0, sec, lut, 40, csc, 0, 0, 5000, 255, 0, 0, 0, 1);
}

int main(void)
{
	const unsigned int n = getenv("T32O_NRAND") ?
		(unsigned int)atoi(getenv("T32O_NRAND")) : 3000;
	unsigned int c;

	for (c = 0; c < n; c++) {
		u8 sec[SECB], lut[10 * 40], csc[104];
		u32 ch = rnd() & 3;
		u32 ev_lo = rnd();
		u32 ev_hi = rnd() % 300;
		u32 ct = 1500 + rnd() % 4000;
		u32 ct_code = rnd() % 20;
		u32 ctr_ev = rnd() & 0xff;
		u32 ctr_ct = rnd() & 0xff;
		u32 pend = rnd() % 20;
		u32 b19 = rnd() & 1;
		unsigned int knots = 1 + rnd() % 10, i, k;

		gen_random_section(sec);
		sec[0x4af] = (u8)knots;
		sec[0x4b0] = (u8)(rnd() % 4 ? rnd() % 255 : rnd() % 6);
		sec[0x4b1] = (u8)(rnd() % 16 ? 0 : 1 + rnd() % 3);
		sec[0x492] = (u8)(rnd() % 3);
		sec[0x493] = (u8)(rnd() % 2);
		sec[0x494] = (u8)(rnd() % 3);
		sec[0x496] = (u8)(rnd() % 2);
		sec[0x498] = (u8)(rnd() % 2);
		sec[0x499] = (u8)(rnd() % 3);
		sec[0x49e] = (u8)(rnd() % 2);
		sec[0x49f] = (u8)(rnd() % 3);
		sec[0x461] = (u8)(rnd() % 2);
		sec[0x462] = (u8)(rnd() % 3);
		sec[0x467] = (u8)(rnd() % 2);
		sec[0x468] = (u8)(rnd() % 3);
		sec[0x45d] = (u8)(rnd() % 3);
		sec[0x466] = (u8)(rnd() % 3);
		sec[0x49d] = (u8)(rnd() % 3);
		sec[0x497] = (u8)(rnd() % 2);
		sec[0x49c] = (u8)rnd();
		sec[0x45a] = (u8)(rnd() % 2);
		sec[0x45b] = (u8)(rnd() % 2);
		sec[0x45c] = (u8)(rnd() % 3);
		sec[0x45e] = (u8)rnd();
		sec[0x45f] = (u8)(rnd() % 2);
		sec[0x460] = (u8)(rnd() % 2);
		sec[0x465] = (u8)(rnd() % 2);
		sec[0x4a2] = (u8)rnd();
		sec[0x46b] = (u8)rnd();
		for (i = 0; i < 12; i++) {
			put32(sec + 4 + 4 * i, rnd() % 0x100000);
			put32(sec + 0x34 + 4 * i, rnd() % 0x100000);
		}
		for (k = 0; k < knots; k++) {
			put32(lut + 40 * k, 1500 + rnd() % 4000);
			for (i = 0; i < 9; i++)
				put32(lut + 40 * k + 4 + 4 * i, (u32)((int)(rnd() % 4096) - 2048));
		}
		for (i = 0; i < 104; i++)
			csc[i] = (u8)rnd();
		csc[0x5b] = (u8)(rnd() % 6);

		run_case("dn-random", CASE_DN, ch, sec, lut, knots * 40, csc, ev_lo, ev_hi, ct, ct_code,
			 ctr_ev, ctr_ct, pend, b19);
		run_case("wdr-random", CASE_WDR, ch, sec, lut, knots * 40, csc, ev_lo, ev_hi, ct, ct_code,
			 ctr_ev, ctr_ct, pend, b19);
		run_case("wake-random", CASE_WAKE, ch, sec, lut, knots * 40, csc, ev_lo, ev_hi, ct, ct_code,
			 ctr_ev, ctr_ct, pend, b19);
	}
	run_boundaries();

	printf("t32_bcshd_oracle: random %u compared, %u mismatches\n", compared, failed);
	if (failed)
		return 1;
	return 0;
}
