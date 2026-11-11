// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: tisp_long_ev_update (tx_isp_t32_blkrest.h) against the
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

u32 t32_data_dd678[8] __attribute__((aligned(8)));
extern u64 tisp_long_ev_update(u32, u32, u32, u32);

static u32 rs = 0x165667b1u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

#define LOGN 12
struct hlog { unsigned int n; u32 e[LOGN][6]; };
static struct hlog vl, ol, *cur;
static u32 byp, forced;
static void hrec(u32 k, u32 a, u32 b, u32 c, u32 d, u32 e)
{
	if (cur->n < LOGN) { u32 *p = cur->e[cur->n]; p[0] = k; p[1] = a; p[2] = b; p[3] = c; p[4] = d; p[5] = e; }
	cur->n++;
}
u64 tisp_reg_read(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; hrec(9, a0, a1, 0, 0, 0); return byp; }
#define EVS(name, k, x, y) \
u64 name(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5); \
u64 name(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5) { (void)a1; (void)a4; (void)a5; hrec(k, a0, a2, a3, x, y); return 0; }
#define EV4(name, k) u64 name(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; hrec(k, a0, a2, a3, 0, 0); return 0; }
EV4(tisp_awb_ev_update, T32_LEV_AWB)
EV4(tisp_gamma_ev_update, T32_LEV_GAMMA)
EV4(tisp_ccm_ev_update, T32_LEV_CCM)
EVS(tisp_defog_ev_update, T32_LEV_DEFOG, a4, forced ? a5 : 0)
EVS(tisp_bcsh_ev_update, T32_LEV_BCSH, a4, forced ? a5 : 0)
EVS(tisp_tmo_ev_update, T32_LEV_TMO, a4, 0)
EV4(tisp_wdr_long_ev_update, T32_LEV_WDR)
static u32 o_rd(u32 ch, u32 reg) { return (u32)tisp_reg_read(ch, reg, 0, 0); }
static void o_ev(enum t32_lev_blk b, u32 ch, u64 ev, u32 x, u32 y) { hrec(b, ch, (u32)ev, (u32)(ev >> 32), x, y); }
static const struct t32_lev_ops ops = { o_rd, o_ev };

int main(void)
{
	static u8 pipe[1808];
	u64 last[4];
	unsigned long n, N = 30000, frc = 0, chg = 0;
	u32 i;

	for (n = 0; n < N; n++) {
		u32 ch = rnd() & 1, x = rnd(), y = rnd(), r = rnd() % 3, force = r == 0 ? 1 : (r == 1 ? 0 : rnd());
		u32 keep = (rnd() & 1) ? 0 : ((rnd() & 1) ? 1 : rnd()), rv, ro;
		u8 sh = (rnd() % 3 == 0) ? 0 : ((rnd() & 1) ? 1 : rnd() | 2);
		u64 ev;

		for (i = 0; i < 4; i++) last[i] = (u64)rnd() << 32 | rnd();
		ev = (rnd() & 1) ? last[ch] : ((rnd() & 1) ? last[ch] ^ ((u64)1 << (rnd() & 63)) : (u64)rnd() << 32 | rnd());
		byp = rnd() & (0x40 | 0x80 | 0x100 | 0x2000 | 0x10 | 1u << 22 | 0x0f00000f);
		for (i = 0; i < sizeof(pipe); i++) pipe[i] = rnd();
		pipe[904 * ch + 36] = (rnd() & 1) ? 1 : rnd();
		forced = force == 1 && !keep;

		memset(&ol, 0, sizeof(ol)); cur = &ol;
		memcpy(t32_data_dd678, last, 32);
		ro = (u32)t32_long_ev_update(&ops, &sh, last, pipe, ch, ev, x, y, force, keep);

		t32o_reset();
		memset(&vl, 0, sizeof(vl)); cur = &vl;
		memcpy(t32_data_dc284, pipe, sizeof(pipe));
		t32_data_d03a0[0] = sh;
		t32o_arg5 = y; t32o_arg6 = force; t32o_arg7 = keep;
		rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_long_ev_update, ch, rnd(), (u32)ev, (u32)(ev >> 32), x);
		t32o_arg5 = t32o_arg6 = t32o_arg7 = 0;
		t32_data_d03a0[0] = 0;

		if (rv != ro || vl.n != ol.n || memcmp(vl.e, ol.e, sizeof(vl.e)) || memcmp(t32_data_dd678, last, 32)) {
			printf("t32_longev_oracle: FAIL case %lu ch %u force %#x keep %#x byp %#x: ret %#x/%#x calls %u/%u\n", n, ch, force, keep,
			       byp, rv, ro, vl.n, ol.n);
			for (i = 0; i < vl.n && i < LOGN; i++)
				printf("  %u: %u %#x %#x %#x %#x / %u %#x %#x %#x %#x\n", i, vl.e[i][0], vl.e[i][1], vl.e[i][2], vl.e[i][4],
				       vl.e[i][5], ol.e[i][0], ol.e[i][1], ol.e[i][2], ol.e[i][4], ol.e[i][5]);
			return 1;
		}
		frc += forced;
		chg += ol.n > 6;
	}
	printf("t32_longev_oracle: PASS %lu cases (%lu forced, %lu long call lists)\n", N, frc, chg);
	return 0;
}
