// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: CCM per-frame paths.  Runs the unmodified vendor
 * functions (Matt Davis' recovered C, PR #15 @ 2e2e06bc) and our readable
 * t32_ccm_{dn_params_refresh,wdr_en,wake_up} from driver/t32/tx_isp_t32_ccm.h.
 *
 * Each case: random section + knots, vendor init, then the vendor state
 * (exposure, colour temperature) and for dn the bytes +0x360/+0x361 are
 * randomised on both sides, and the per-frame function is run on both.
 * Compared: every register write (address, value, order), the section bytes,
 * the parameter block and the state (exposure, colour temperature).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_ccm.h"

#define SECB	T32_CCM_SEC_BYTES
#define SEC_OFF	0x7e40u
#define MAXW	T32_CCM_MAX_WRITES

extern u64 tisp_ccm_dn_params_refresh(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_ccm_wdr_en(u32 a0, u32 a1, u32 a2, u32 a3);
/*
 * ref/fn_..8d688_tisp_ccm_wake_up.c does not build with the host gcc (its
 * "extern int tisp_ccm_write_reg();" is a no-argument prototype in C23), so
 * it is not linked: its whole body is tisp_ccm_write_reg(ch, 0), called here.
 */

static u32 rs = 0x51c0deu;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static void put32(u8 *p, u32 v)
{
	p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
}

static unsigned int compared, refused, failed;

enum { CASE_DN, CASE_WDR, CASE_WAKE };

static int run_case_mode(int which, const u8 *sec, const u8 *lut, u32 lutbytes, u8 dsm)
{
	struct t32_front_write ow[MAXW];
	struct t32_ccm st;
	u8 our_sec[SECB], *our_base = NULL, *vbase, *vlut, *vinfo, *vcp;
	u32 *vst, *vev;
	unsigned int i, ref_n;
	int our_n;
	u32 vsec_off, our_sec_off, ct, elo, ehi;
	u8 b360 = (u8)rnd(), b361 = (u8)rnd();

	/* inputs of this case */
	ct = rnd() % 3 ? 1000 + rnd() % 8000 : 5000 + rnd() % 5 - 2;
	elo = rnd();
	switch (rnd() % 4) {
	case 0: ehi = 0; break;
	case 1: ehi = rnd() & 0x3ff; break;
	case 2: ehi = rnd() & 0xff; break;
	default: {
		u32 thr = t32_front_le32(sec + 4 + 4 * (rnd() % 12)) + rnd() % 3 - 1;

		ehi = thr >> 22;
		elo = thr << 10 | (rnd() & 0x3ff);
	}
	}

	/* ours first, to skip refused sections */
	memcpy(our_sec, sec, SECB);
	our_n = t32_ccm_start(&st, our_sec, lut, lutbytes, ow);
	if (our_n < 0) {
		refused += !dsm;
		return 0;
	}

	/* ---- vendor side ---- */
	t32o_reset();
	vbase = t32o_alloc(SEC_OFF + SECB);
	vlut = t32o_alloc(lutbytes ? lutbytes : 4);
	vinfo = t32o_alloc(64);
	memcpy(vbase + SEC_OFF, sec, SECB);
	memcpy(vlut, lut, lutbytes);
	put32(vinfo + 24, T32O_PTR(vlut));
	t32_data_dd0a4[0] = T32O_PTR(vbase);
	t32_data_dc274[0] = T32O_PTR(vinfo);
	t32_data_d03a0[0] = dsm;	/* 1: tisp_reg_write queues into the register shadow (logged tagged, same values) */
	t32o_call((t32o_fn)(void (*)(void))tisp_ccm_init, 0, 0, 0, 0, 0);

	vst = T32O_HOST(t32_data_e28f0[0]);
	vev = T32O_HOST(vst[1]);
	vev[0] = elo;
	vev[1] = ehi;
	vev[2] = ct;
	st.ev_lo = elo;
	st.ev_hi = ehi;
	st.ct = ct;
	if (which == CASE_DN) {
		vbase[SEC_OFF + 0x360] = b360;
		vbase[SEC_OFF + 0x361] = b361;
		our_sec[0x360] = b360;
		our_sec[0x361] = b361;
		our_base = t32o_alloc(SEC_OFF + SECB);
		memcpy(our_base + SEC_OFF, our_sec, SECB);
	}

	t32o_log.n = 0;
	if (which == CASE_DN)
		t32o_call((t32o_fn)(void (*)(void))tisp_ccm_dn_params_refresh, 0, 0, 0, 0, 0);
	else if (which == CASE_WDR)
		t32o_call((t32o_fn)(void (*)(void))tisp_ccm_wdr_en, 0, 0, 0, 0, 0);
	else
		t32o_call((t32o_fn)(void (*)(void))tisp_ccm_write_reg, 0, 0, 0, 0, 0);
	ref_n = t32o_log.n;
	if (ref_n > MAXW)
		return 2;
	vsec_off = vst[0] - T32O_PTR(vbase);
	vcp = T32O_HOST(vst[2]);

	/* ---- our side ---- */
	if (which == CASE_DN) {
		our_n = t32_ccm_dn_params_refresh(&st, our_base, ow);
		our_sec_off = (u32)((uintptr_t)st.sec - (uintptr_t)our_base);
	} else if (which == CASE_WDR) {
		our_n = t32_ccm_wdr_en(&st, ow);
		our_sec_off = SEC_OFF;	/* section pointer unchanged */
		vsec_off = SEC_OFF;
	} else {
		our_n = t32_ccm_wake_up(&st, ow);
		our_sec_off = SEC_OFF;
		vsec_off = SEC_OFF;
	}

	compared += !dsm;
	if (our_n < 0 || (unsigned int)our_n != ref_n)
		goto bad;
	for (i = 0; i < ref_n; i++)
		if (!t32o_reg_eq(t32o_log.w[i].reg, ow[i].reg) || t32o_log.w[i].val != ow[i].val)
			goto bad;
	if (our_sec_off != vsec_off)
		goto bad;
	if (memcmp(st.sec, vbase + SEC_OFF, SECB))
		goto bad;
	if (memcmp(st.cp, vcp, sizeof(st.cp)))
		goto bad;
	if (vev[0] != st.ev_lo || vev[1] != st.ev_hi || vev[2] != st.ct)
		goto bad;
	return 0;
bad:
	if (failed++ < 5) {
		printf("MISMATCH case %d: writes vendor %u ours %d, sec off vendor %08x ours %08x\n",
		       which, ref_n, our_n, vsec_off, our_sec_off);
		for (i = 0; i < ref_n || (int)i < our_n; i++)
			if (i >= ref_n || (int)i >= our_n ||
			    !t32o_reg_eq(t32o_log.w[i].reg, ow[i].reg) || t32o_log.w[i].val != ow[i].val) {
				printf("  #%u vendor %04x=%08x ours %04x=%08x\n", i,
				       i < ref_n ? t32o_log.w[i].reg : 0, i < ref_n ? t32o_log.w[i].val : 0,
				       (int)i < our_n ? ow[i].reg : 0, (int)i < our_n ? ow[i].val : 0);
				if (i > 8)
					break;
			}
		for (i = 0; i < SECB; i++)
			if (st.sec[i] != vbase[SEC_OFF + i])
				printf("  sec+0x%x vendor %02x ours %02x\n", i, vbase[SEC_OFF + i], st.sec[i]);
		for (i = 0; i < sizeof(st.cp); i++)
			if (st.cp[i] != vcp[i])
				printf("  cp+0x%x vendor %02x ours %02x\n", i, vcp[i], st.cp[i]);
		printf("  state vendor ev %08x:%08x ct %u ours %08x:%08x ct %u\n", vev[1], vev[0], vev[2],
		       st.ev_hi, st.ev_lo, st.ct);
	}
	return 1;
}

static int run_case(int which, const u8 *sec, const u8 *lut, u32 lutbytes)
{
	int bad = run_case_mode(which, sec, lut, lutbytes, 0);

	bad += run_case_mode(which, sec, lut, lutbytes, 1);	/* d03a0 = 1: only the write addresses may differ */
	t32_data_d03a0[0] = 0;
	return bad;
}

static int coef(int mode)
{
	switch (mode) {
	case 0: return (int)(rnd() % 0x4000) - 0x2000;
	case 1: return (int)(rnd() % 0x10000) - 0x8000;
	case 2: return (rnd() & 1) ? 0x1fff + (int)(rnd() % 3) - 1 : -0x2000 + (int)(rnd() % 3) - 1;
	default: return (int)(rnd() % 0x200000) - 0x100000;
	}
}

static void random_cases(unsigned int n)
{
	static u8 sec[SECB], lut[255 * 40];
	unsigned int c, i, k;

	for (c = 0; c < n; c++) {
		int mode = (int)(c % 4), knots, hyst, ct;

		for (i = 0; i < SECB; i++)
			sec[i] = (u8)rnd();
		put32(sec, T32_CCM_ID);
		sec[0x35f] = (rnd() % 16) ? (u8)(1 + rnd() % 255) : 0;
		put32(sec + 0x38, rnd() % 8 ? rnd() % 4 + 2 : rnd() % 2);
		if (rnd() % 3 == 0) {
			static const u32 first[] = { 1, 2, 3, 0x7fffffff, 0x80000000u, 0xffffffffu, 4096, 0x1000 };
			int zeros = (int)(rnd() % 13);

			for (i = 0; i < 12; i++)
				put32(sec + 4 + 4 * i, (int)i < zeros ? 0 :
				      (int)i == zeros && rnd() % 2 ? first[rnd() % 8] : rnd());
		}
		knots = c % 50 == 7 ? (int)(rnd() % 255) + 1 : (int)(rnd() % 10) + 1;
		hyst = rnd() % 4 ? (int)(rnd() % 400) & 0xff : (int)(rnd() % 8);
		sec[0x364] = (u8)knots;
		sec[0x365] = (u8)hyst;
		ct = 1500 + (int)(rnd() % 3000);
		for (k = 0; k < (unsigned int)knots; k++) {
			int t = ct;

			if (rnd() % 3 == 0) {
				static const int edge[] = { 0, 1, -1 };
				int e = edge[rnd() % 3], h = rnd() & 1 ? hyst : -hyst;

				t = 5000 + h + e;
				if (t < ct)
					t = ct;
			}
			put32(lut + 40 * k, (u32)t);
			for (i = 0; i < 9; i++)
				put32(lut + 40 * k + 4 + 4 * i, (u32)coef(mode));
			ct = t + (int)(rnd() % 2500);
		}
		run_case((int)(c % 3), sec, lut, (u32)knots * 40U);
	}
}

int main(void)
{
	const unsigned int n = getenv("T32O_NRAND") ?
		(unsigned int)atoi(getenv("T32O_NRAND")) : 9000;

	random_cases(n);
	printf("t32_ccmd_oracle: random %u compared (%u refused by ours), %u mismatches\n",
	       compared, refused, failed);
	return failed ? 1 : 0;
}
