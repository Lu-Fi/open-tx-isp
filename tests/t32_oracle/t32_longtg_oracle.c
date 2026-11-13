// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: tisp_long_tgain_update (tx_isp_t32_blkrest.h) against the
 * vendor function (Matt Davis' recovered C, PR #15 @ 2e2e06bc).  Per case
 * random channel 0 / 1, EV (equal to the last one in half the cases), the
 * two extra words, force (1 / 0 / random) and keep (0 / 1 / random), bypass
 * register value (each tested bit random), d03a0, pipe WDR byte.  Compared:
 * return value, the register read (channel, register), every block update in
 * order with channel, EV and the 5th argument (6th for defog / BCSH on the
 * forced path; elsewhere the vendor's is a stale stack word), the last-EV
 * table.  Exit 1 on the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_blkrest.h"

u32 t32_data_dc064[4];
extern u64 tisp_long_tgain_update(u32, u32, u32, u32);

static u32 rs = 0x3243f6a8u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

#define LOGN 20
struct hlog { unsigned int n; u32 e[LOGN][6]; };
static struct hlog vl, ol, *cur;
static u32 byp, forced;
static void hrec(u32 k, u32 a, u32 b, u32 c, u32 d, u32 e)
{
	if (cur->n < LOGN) { u32 *p = cur->e[cur->n]; p[0] = k; p[1] = a; p[2] = b; p[3] = c; p[4] = d; p[5] = e; }
	cur->n++;
}
u64 tisp_reg_read(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; hrec(99, a0, a1, 0, 0, 0); return byp; }
#define G4(name, k) u64 name(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; hrec(k, a0, a2, a3, 0, 0); return 0; }
#define R4(name, k) u64 name(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; hrec(50 + k, a0, a1, 0, 0, 0); return 0; }
G4(tisp_dpc_l_gain_update, T32_LTG_DPC)
G4(tisp_lsc_tgain_update, T32_LTG_LSC)
G4(tisp_tmo_tgain_update, T32_LTG_TMO)
G4(tisp_awb_tgain_update, T32_LTG_AWB)
u64 tisp_lce_tgain_update(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5);
u64 tisp_lce_tgain_update(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5) { (void)a1; hrec(T32_LTG_LCE, a0, a2, a3, a4, forced ? a5 : 0); return 0; }
R4(tisp_gib_again_update, T32_LTG_GIB)
R4(tisp_dmsc_refresh, T32_LTG_DMSC)
R4(tisp_mdns_refresh, T32_LTG_MDNS)
R4(tisp_ysp_refresh, T32_LTG_YSP)
R4(tisp_cdns_refresh, T32_LTG_CDNS)
R4(tisp_ydns_refresh, T32_LTG_YDNS)
R4(tisp_sdns_refresh, T32_LTG_SDNS)
R4(tisp_rdns_refresh, T32_LTG_RDNS)
R4(tisp_af_tgain_update, T32_LTG_AF)
static u32 o_rd(u32 ch, u32 reg) { return (u32)tisp_reg_read(ch, reg, 0, 0); }
static void o_g(enum t32_ltg_gain b, u32 ch, u64 g, u32 x, u32 y) { hrec(b, ch, (u32)g, (u32)(g >> 32), x, y); }
static void o_r(enum t32_ltg_ref b, u32 ch, u32 g) { hrec(50 + b, ch, g, 0, 0, 0); }
static const struct t32_ltg_ops ops = { o_rd, o_g, o_r };

int main(void)
{
	u32 last[4], i;
	unsigned long n, N = 30000, frc = 0, same = 0;

	for (n = 0; n < N; n++) {
		u32 ch = rnd() & 1, x = rnd(), y = rnd(), r = rnd() % 3, force = r == 0 ? 1 : (r == 1 ? 0 : rnd());
		u32 keep = (rnd() & 1) ? 0 : ((rnd() & 1) ? 1 : rnd()), rv, ro, hi;
		u8 sh = (rnd() % 3 == 0) ? 0 : ((rnd() & 1) ? 1 : rnd() | 2);
		u64 g;

		for (i = 0; i < 4; i++) last[i] = rnd();
		r = rnd() % 3;
		hi = r == 0 ? 0 : (r == 1 ? 1u << (rnd() & 31) : rnd());
		g = (u64)hi << 32 | ((rnd() % 3) ? last[ch] : rnd());
		byp = rnd() & 0x000f9c37u;
		if (rnd() & 1) byp |= rnd() & 0xfff00000u;
		forced = force == 1 && !keep;

		memset(&ol, 0, sizeof(ol)); cur = &ol;
		memcpy(t32_data_dc064, last, 16);
		ro = (u32)t32_long_tgain_update(&ops, &sh, last, ch, g, x, y, force, keep);

		t32o_reset();
		memset(&vl, 0, sizeof(vl)); cur = &vl;
		t32_data_d03a0[0] = sh;
		t32o_arg5 = y; t32o_arg6 = force; t32o_arg7 = keep;
		rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_long_tgain_update, ch, rnd(), (u32)g, (u32)(g >> 32), x);
		t32o_arg5 = t32o_arg6 = t32o_arg7 = 0;
		t32_data_d03a0[0] = 0;

		if (rv != ro || vl.n != ol.n || memcmp(vl.e, ol.e, sizeof(vl.e)) || memcmp(t32_data_dc064, last, 16)) {
			printf("t32_longtg_oracle: FAIL case %lu ch %u force %#x keep %#x byp %#x: ret %#x/%#x calls %u/%u\n", n, ch, force, keep,
			       byp, rv, ro, vl.n, ol.n);
			for (i = 0; i < vl.n && i < LOGN; i++)
				printf("  %u: %u %#x %#x %#x %#x / %u %#x %#x %#x %#x\n", i, vl.e[i][0], vl.e[i][1], vl.e[i][2], vl.e[i][3],
				       vl.e[i][5], ol.e[i][0], ol.e[i][1], ol.e[i][2], ol.e[i][3], ol.e[i][5]);
			return 1;
		}
		frc += forced;
		same += ol.n == 0;
	}
	printf("t32_longtg_oracle: PASS %lu cases (%lu forced, %lu unchanged)\n", N, frc, same);
	return 0;
}
