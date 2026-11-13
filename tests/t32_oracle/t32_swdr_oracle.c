// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: tisp_s_wdr_en (tx_isp_t32_librest.h) against the vendor
 * function (Matt Davis' recovered C, PR #15 @ 2e2e06bc; its callees renamed
 * to recorders by fix_op20_swdr.sh).  Per case: d03a0 0 / 1 / 2, channel
 * 0 / 1, bank 0 .. 5 (random high bits above the & 0xff), en 0 / 1 / other,
 * bank table entries NULL or one of two random tuning bins, random tuning
 * blocks (bytes 0 / 1 / random in the three bit fields), channel state
 * (config mode 0 .. 31, random bits), top block, DE-IR flags, WDR flags,
 * scripted register reads (the reset done bit after 0 .. 4 polls).
 * Both sides run on the same objects (restored in between).  Compared: every
 * call in order with its arguments (system / tisp register reads and writes,
 * memcpy, lut_params_copy, the nine block wdr_en, isp_printf with the values
 * its message prints), all bytes of da850, dc284, e1474, f0918, the bank
 * table and both channels' tuning blocks, and the return value.
 * Channels >= 2 and banks >= 6 are not run (vendor UB: past dc284 / dd0b4).
 * Exit 1 on the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_librest.h"

extern u64 tisp_s_wdr_en(u32, u32, u32, u32);
extern u32 t32_data_dd0b4[24];
u32 t32_data_f0918[4];
/* isp_printf formats (data.c cb780 / cb7c0 / cb724 / cb7f4 / cb81c) and the function name (c2fc0): address only */
u8 t32_data_cb780[4], t32_data_cb7c0[4], t32_data_cb724[4], t32_data_cb7f4[4], t32_data_cb81c[4], t32_data_c2fc0[4];

static u32 rs = 0x3c6ef372u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

enum { C_SRD = 1, C_SWR, C_RRD, C_RWR, C_CPY, C_LUT, C_LOG, C_LSC, C_GIB, C_CCM, C_BCSH, C_GAMMA, C_MDNS, C_LCE,
       C_DEFOG, C_DPC };
#define LOGN 64
struct hlog { unsigned int n; u32 e[LOGN][6]; };
static struct hlog vl, ol, *cur;
static u32 rdseed, done_after, n204, bad_fmt;

static void rec(u32 k, u32 a, u32 b, u32 c, u32 d, u32 e)
{
	unsigned int i = cur->n;

	if (i < LOGN) { cur->e[i][0] = k; cur->e[i][1] = a; cur->e[i][2] = b; cur->e[i][3] = c; cur->e[i][4] = d;
			cur->e[i][5] = e; }
	cur->n++;
}
static u32 rdval(u32 k, u32 reg)
{
	u32 v = (rdseed ^ reg * 0x9e3779b9u ^ cur->n * 0x85ebca6bu) * 0xc2b2ae35u;

	if (k == C_SRD && reg == T32_SWDR_TOP_RST_DONE)
		v = (v & ~1u) | (n204++ >= done_after);
	if (cur->n > 4000)	/* a mutation that no longer ends the poll */
		v |= 1;
	return v;
}
static u32 srd(u32 reg) { u32 v = rdval(C_SRD, reg); rec(C_SRD, reg, v, 0, 0, 0); return v; }
static u32 rrd(u32 ch, u32 reg) { u32 v = rdval(C_RRD, reg); rec(C_RRD, ch, reg, v, 0, 0); return v; }

/* vendor recorders */
u64 op20_w_system_reg_read(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; return srd(a0); }
u64 op20_w_system_reg_write(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; rec(C_SWR, a0, a1, 0, 0, 0); return 0; }
u64 op20_w_tisp_reg_read(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return rrd(a0, a1); }
u64 op20_w_tisp_reg_write(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; rec(C_RWR, a0, a1, a2, 0, 0); return 0; }
u64 op20_w_memcpy(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a3;
	rec(C_CPY, a0, a1, a2, 0, 0);
	memmove(T32O_HOST(a0), T32O_HOST(a1), a2);
	return a0;
}
u64 op20_w_tisp_lut_params_copy(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; rec(C_LUT, a0, a1, 0, 0, 0); return 0; }
#define WEN(n, k) u64 op20_w_tisp_##n##_wdr_en(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; rec(k, a0, a1, 0, 0, 0); return 0; }
WEN(lsc, C_LSC) WEN(gib, C_GIB) WEN(ccm, C_CCM) WEN(bcsh, C_BCSH) WEN(gamma, C_GAMMA) WEN(mdns, C_MDNS) WEN(lce, C_LCE)
WEN(defog, C_DEFOG) WEN(dpc, C_DPC)
u64 op20_w_isp_printf(u32 a0, u32 a1, u32 a2, u32 a3, u32 s16, u32 s20, u32 s24)
{
	const u8 *fmt = NULL;

	if (a2 != T32O_PTR(t32_data_c2fc0)) bad_fmt = 1;
	switch (a3) {
	case 640: fmt = t32_data_cb780; s20 = s24 = 0; break;
	case 656: fmt = t32_data_cb7c0; break;
	case 671: fmt = t32_data_cb724; s16 = s20 = s24 = 0; break;
	case 706: fmt = t32_data_cb7f4; s16 = s20 = s24 = 0; break;
	case 748: fmt = t32_data_cb81c; s24 = 0; break;
	}
	if (a1 != T32O_PTR(fmt)) bad_fmt = 1;
	rec(C_LOG, a0, a3, s16, s20, s24);
	return 0;
}

/* our hooks */
static u32 o_srd(void *c, u32 reg) { (void)c; return srd(reg); }
static void o_swr(void *c, u32 reg, u32 val) { (void)c; rec(C_SWR, reg, val, 0, 0, 0); }
static u32 o_rrd(void *c, u32 ch, u32 reg) { (void)c; return rrd(ch, reg); }
static void o_rwr(void *c, u32 ch, u32 reg, u32 val) { (void)c; rec(C_RWR, ch, reg, val, 0, 0); }
static void o_cpy(void *c, void *d, const void *s, u32 n) { (void)c; rec(C_CPY, T32O_PTR(d), T32O_PTR(s), n, 0, 0); memmove(d, s, n); }
static void o_lut(void *c, u32 ch, u32 b) { (void)c; rec(C_LUT, ch, b, 0, 0, 0); }
#define OEN(n, k) static void o_##n(void *c, u32 ch, u32 en) { (void)c; rec(k, ch, en, 0, 0, 0); }
OEN(lsc, C_LSC) OEN(gib, C_GIB) OEN(ccm, C_CCM) OEN(bcsh, C_BCSH) OEN(gamma, C_GAMMA) OEN(mdns, C_MDNS) OEN(lce, C_LCE)
OEN(defog, C_DEFOG) OEN(dpc, C_DPC)
static void o_log(void *c, u32 lv, u32 line, u32 a, u32 b, u32 d) { (void)c; rec(C_LOG, lv, line, a, b, d); }

#define TS T32_SWDR_TUNE_SIZE
static void fill_bits(u8 *p, u32 mode)
{
	u32 i;

	for (i = 0; i < 32; i++)
		p[i] = mode == 0 ? (u8)(rnd() & 1) : mode == 1 ? 0 : mode == 2 ? 1 : (u8)rnd();
}

int main(void)
{
	static const struct t32_swdr_hooks h = { NULL, o_srd, o_swr, o_rrd, o_rwr, o_cpy, o_lut, o_lsc, o_gib, o_ccm,
						 o_bcsh, o_gamma, o_mdns, o_lce, o_defog, o_dpc, o_log };
	static u8 i_da850[704], i_dc284[1808], s_da850[704], s_dc284[1808];
	static u8 *tun[2], *bin[2], *ini[2], *res[2];
	u32 i_e1474[4], s_e1474[4], i_f0918[4], s_f0918[4], i_bank[24], s_bank[24], i_tune[4], s_tune[4], i;
	unsigned long n, N = 3000, off = 0, on = 0, other = 0, nobank = 0, shadow = 0;

	for (i = 0; i < 2; i++) {
		tun[i] = t32o_alloc(TS); bin[i] = t32o_alloc(TS);
		ini[i] = malloc(TS); res[i] = malloc(TS);
	}
	for (n = 0; n < N; n++) {
		struct t32_swdr_data d = { t32_data_d03a0, t32_data_dd0b4, t32_data_dd0a4, t32_data_da850, t32_data_dc284,
					   t32_data_e1474, t32_data_f0918 };
		u32 ch = rnd() & 1, bank = rnd() % 6, en, sel, rv, ro, sh, seed, dn;

		bank |= (rnd() & 1) ? rnd() << 8 : 0;
		sel = rnd() % 5;
		en = sel < 2 ? 0 : sel < 4 ? 1 : ((rnd() & 1) ? 2 : rnd());
		sh = rnd() % 3;
		for (i = 0; i < 2; i++) {
			u32 k;

			for (k = 0; k < TS; k++) ini[i][k] = (u8)rnd();
			fill_bits(ini[i] + T32_SWDR_TUNE_BYPASS, rnd() & 3);
			fill_bits(ini[i] + T32_SWDR_TUNE_MASK2, rnd() & 3);
			fill_bits(ini[i] + T32_SWDR_TUNE_MASK3, rnd() & 3);
			if (n == 0 || !(rnd() & 7)) {
				for (k = 0; k < TS; k++) bin[i][k] = (u8)rnd();
				fill_bits(bin[i] + T32_SWDR_TUNE_BYPASS, rnd() & 3);
				fill_bits(bin[i] + T32_SWDR_TUNE_MASK2, rnd() & 3);
				fill_bits(bin[i] + T32_SWDR_TUNE_MASK3, rnd() & 3);
			}
		}
		for (i = 0; i < sizeof(i_da850); i++) i_da850[i] = (u8)rnd();
		for (i = 0; i < sizeof(i_dc284); i++) i_dc284[i] = (u8)rnd();
		for (i = 0; i < 2; i++) {	/* config mode 0 .. 31 */
			u32 cfg = rnd();

			if (rnd() & 1) cfg = (cfg & ~0x1fu) | (rnd() & 3 ? rnd() % 24 : 19 + (rnd() & 1));
			memcpy(i_dc284 + T32_SWDR_ST_SIZE * i + T32_SWDR_ST_CFG, &cfg, 4);
		}
		for (i = 0; i < 4; i++) {
			i_e1474[i] = (rnd() & 1) ? rnd() & 1 : rnd();
			i_f0918[i] = rnd();
			i_tune[i] = i < 2 ? T32O_PTR(tun[i]) : rnd();
		}
		for (i = 0; i < 24; i++) i_bank[i] = (rnd() % 5) ? T32O_PTR(bin[rnd() & 1]) : 0;
		seed = rnd();
		dn = rnd() % 5;

#define SETUP() do { \
		memcpy(t32_data_da850, i_da850, sizeof(i_da850)); memcpy(t32_data_dc284, i_dc284, sizeof(i_dc284)); \
		memcpy(t32_data_e1474, i_e1474, 16); memcpy(t32_data_f0918, i_f0918, 16); \
		memcpy(t32_data_dd0b4, i_bank, sizeof(i_bank)); memcpy(t32_data_dd0a4, i_tune, 16); \
		memcpy(tun[0], ini[0], TS); memcpy(tun[1], ini[1], TS); \
		t32_data_d03a0[0] = (u8)sh; rdseed = seed; done_after = dn; n204 = 0; } while (0)

		/* ours */
		t32o_reset();
		SETUP();
		memset(&ol, 0, sizeof(ol)); cur = &ol;
		ro = t32_s_wdr_en(&h, &d, ch, en, bank);
		memcpy(s_da850, t32_data_da850, sizeof(s_da850)); memcpy(s_dc284, t32_data_dc284, sizeof(s_dc284));
		memcpy(s_e1474, t32_data_e1474, 16); memcpy(s_f0918, t32_data_f0918, 16);
		memcpy(s_bank, t32_data_dd0b4, sizeof(s_bank)); memcpy(s_tune, t32_data_dd0a4, 16);
		memcpy(res[0], tun[0], TS); memcpy(res[1], tun[1], TS);

		/* vendor */
		t32o_reset();
		SETUP();
		memset(&vl, 0, sizeof(vl)); cur = &vl; bad_fmt = 0;
		rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_s_wdr_en, ch, en, bank, rnd(), 0);

		if (!i_bank[ch * 6 + (bank & 0xff)]) nobank++;
		else if (en == 0) off++;
		else if (en == 1) on++;
		else other++;
		if (sh) shadow++;
		if (bad_fmt || rv != ro || vl.n != ol.n || memcmp(vl.e, ol.e, sizeof(vl.e)) ||
		    memcmp(s_da850, t32_data_da850, sizeof(s_da850)) || memcmp(s_dc284, t32_data_dc284, sizeof(s_dc284)) ||
		    memcmp(s_e1474, t32_data_e1474, 16) || memcmp(s_f0918, t32_data_f0918, 16) ||
		    memcmp(s_bank, t32_data_dd0b4, sizeof(s_bank)) || memcmp(s_tune, t32_data_dd0a4, 16) ||
		    memcmp(res[0], tun[0], TS) || memcmp(res[1], tun[1], TS)) {
			printf("t32_swdr_oracle: MISMATCH case %lu ch %u en %u bank %x d03a0 %u: ret %u/%u calls %u/%u fmt %u\n",
			       n, ch, en, bank, sh, rv, ro, vl.n, ol.n, bad_fmt);
			for (i = 0; i < vl.n && i < LOGN; i++)
				printf("  v %2u %08x %08x %08x %08x %08x | o %2u %08x %08x %08x %08x %08x\n",
				       vl.e[i][0], vl.e[i][1], vl.e[i][2], vl.e[i][3], vl.e[i][4], vl.e[i][5],
				       ol.e[i][0], ol.e[i][1], ol.e[i][2], ol.e[i][3], ol.e[i][4], ol.e[i][5]);
			return 1;
		}
	}
	printf("t32_swdr_oracle: %lu cases (%lu no bank, %lu off, %lu on, %lu other en; %lu d03a0 != 0), 0 mismatches\n",
	       N, nobank, off, on, other, shadow);
	return 0;
}
