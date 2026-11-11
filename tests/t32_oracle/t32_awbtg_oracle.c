// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: driver/t32/tx_isp_t32_awbtg.h (t32_awb_tgain_apply =
 * tisp_awb_tgain_update with tisp_awb_set_regional_threshold and
 * tisp_awb_set_lum_th_freq; t32_awb_ev_update = tisp_awb_ev_update) against
 * the vendor functions (Matt Davis' recovered C, PR #15 @ 2e2e06bc).
 *
 * Per case: random par section (6740 B) and run state (0x1d48c B) of the
 * channel, a decoy channel with its own blocks, a random gain (index 0 .. 11:
 * the last knot and past it, fraction random or 0 / 0xffff), freeze byte
 * random, the e16c4 words of the channel (override switch on / off, limits
 * below / above / equal the middle one, zero) and of the decoy.  Compared:
 * every byte of par / st / hw of both channels, the e16c4 words of all
 * channels, every register write (addr / value / order), d03a0 = 0 (direct
 * mode: the AWB ports do not port the shadow variant).  A sequence of
 * updates on one context (gain / ev / gain) is checked too.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_awbtg.h"

#define PARB	6740u
#define STB	0x1d48cu
#define HWB	4292u

extern u32 t32_data_e16b4[4], t32_data_e16c4[16];
u64 tisp_awb_tgain_update(u32, u32, u32, u32), tisp_awb_ev_update(u32, u32, u32, u32);

static u32 rs = 0x2b9e0f71u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static u8 d_par[2][PARB], d_st[2][STB], d_hw[2][HWB];
static u32 d_ov[16];
static u8 *vpar[2], *vst[2], *vhw[2];
static unsigned int failed, compared, nwrites, nfrz, nov;

static void setup_vendor(u32 ch, u32 dec)
{
	u32 k, slot[2] = { ch, dec };

	for (k = 0; k < 2; k++) {
		u8 *ctx = t32o_alloc(32);

		vpar[k] = t32o_alloc(PARB);
		vst[k] = t32o_alloc(STB);
		vhw[k] = t32o_alloc(HWB);
		memcpy(vpar[k], d_par[k], PARB);
		memcpy(vst[k], d_st[k], STB);
		memcpy(vhw[k], d_hw[k], HWB);
		memset(ctx, 0, 32);
		t32_wr32(ctx, 0, T32O_PTR(vpar[k]));
		t32_wr32(ctx, 4, T32O_PTR(vst[k]));
		t32_wr32(ctx, 12, T32O_PTR(vhw[k]));
		t32_data_e16b4[slot[k]] = T32O_PTR(ctx);
	}
	memcpy(t32_data_e16c4, d_ov, sizeof(d_ov));
}

static int cmpb(const char *what, const u8 *v, const u8 *o, u32 n, unsigned int c)
{
	u32 i;

	for (i = 0; i < n; i++)
		if (v[i] != o[i]) {
			if (failed++ < 8)
				printf("t32_awbtg_oracle: FAIL case %u: %s byte %u vendor %02x ours %02x\n", c, what, i, v[i], o[i]);
			return 1;
		}
	return 0;
}

/* steps: 0 = tgain update, 1 = ev update */
static int run_case(unsigned int c, u32 ch, u32 dec, const u32 *op, const u32 *val, unsigned int nop)
{
	static u8 opar[2][PARB], ost[2][STB], ohw[2][HWB];
	struct t32_stats_write w[T32_AWBTG_MAX_WRITES * 4];
	struct t32_awb a;
	struct t32_awb_lumov ov;
	unsigned int n = 0, s, i;
	int bad = 0;

	t32o_reset();
	t32_data_d03a0[0] = 0;
	setup_vendor(ch, dec);
	for (s = 0; s < nop; s++)
		t32o_call((t32o_fn)(void (*)(void))(op[s] ? tisp_awb_ev_update : tisp_awb_tgain_update),
			  ch, (u32)rnd(), val[s], 0, 0);

	memcpy(opar, d_par, sizeof(opar));
	memcpy(ost, d_st, sizeof(ost));
	memcpy(ohw, d_hw, sizeof(ohw));
	memset(&a, 0, sizeof(a));
	a.par = (struct t32_awb_par *)opar[0];
	a.st = (struct t32_awb_state *)ost[0];
	a.hw = (struct t32_awb_hw *)ohw[0];
	memcpy(ov.w, d_ov + 4 * ch, sizeof(ov.w));
	for (s = 0; s < nop; s++) {
		if (op[s])
			t32_awb_ev_update(&a, val[s]);
		else
			n += t32_awb_tgain_apply(&a, &ov, val[s], w + n);
	}
	compared++;
	for (i = 0; i < 2; i++) {
		bad |= cmpb(i ? "decoy par" : "par", vpar[i], opar[i], PARB, c);
		bad |= cmpb(i ? "decoy st" : "st", vst[i], ost[i], STB, c);
		bad |= cmpb(i ? "decoy hw" : "hw", vhw[i], ohw[i], HWB, c);
	}
	for (i = 0; i < 16; i++) {
		u32 exp = i / 4 == ch ? ov.w[i & 3] : d_ov[i];

		if (t32_data_e16c4[i] != exp) {
			if (failed++ < 8)
				printf("t32_awbtg_oracle: FAIL case %u: e16c4 word %u vendor %x ours %x\n", c, i,
				       t32_data_e16c4[i], exp);
			bad = 1;
		}
	}
	if (t32o_log.n != n) {
		if (failed++ < 8)
			printf("t32_awbtg_oracle: FAIL case %u: %u vendor writes, %u ours\n", c, t32o_log.n, n);
		return 1;
	}
	for (i = 0; i < n; i++)
		if (!t32o_reg_eq(t32o_log.w[i].reg, w[i].reg) || t32o_log.w[i].val != w[i].val) {
			if (failed++ < 8)
				printf("t32_awbtg_oracle: FAIL case %u: write %u vendor %x=%x ours %x=%x\n", c, i,
				       t32o_log.w[i].reg, t32o_log.w[i].val, w[i].reg, w[i].val);
			return 1;
		}
	nwrites += n;
	return bad;
}

static u32 rnd_gain(void)
{
	u32 i = rnd() % 12, f = rnd() & 0xffff, m = rnd() % 8;

	if (m == 0)
		f = 0;
	else if (m == 1)
		f = 0xffff;
	return i << 16 | f;
}

static u32 rnd_limit(u32 mid)
{
	switch (rnd() % 5) {
	case 0: return 0;
	case 1: return mid;
	case 2: return mid ? rnd() % mid : 0;
	case 3: return mid + 1 + rnd() % 1000;
	}
	return rnd();
}

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 3000, c, k, i;

	for (c = 0; c < n; c++) {
		u32 ch = rnd() & 3, dec = (ch + 1 + rnd() % 3) & 3, op[4], val[4], nop, mid;

		for (k = 0; k < 2; k++) {
			for (i = 0; i < PARB; i++)	/* small rows half of the time: equal knots, a < b and a > b */
				d_par[k][i] = (c & 1) ? (u8)rnd() : (u8)(rnd() & 3);
			for (i = 0; i < STB; i += 4)
				t32_wr32(d_st[k], i, rnd());
			for (i = 0; i < HWB; i++)
				d_hw[k][i] = (u8)rnd();
			d_st[k][107597] = (rnd() % 3) ? 0 : (u8)(1 + rnd() % 255);
		}
		nfrz += d_st[0][107597] != 0;
		for (i = 0; i < 16; i++)
			d_ov[i] = rnd();
		mid = t32_intp_u16(0, 0, d_par[0] + 4786);	/* about the middle limit (exact at gain 0) */
		d_ov[4 * ch] = (rnd() % 3) ? 0 : 1 + rnd() % 3;
		d_ov[4 * ch + 1] = rnd_limit(mid);
		d_ov[4 * ch + 3] = rnd_limit(mid);
		nov += d_ov[4 * ch] != 0;
		nop = c % 4 == 3 ? 3 : 1;
		for (k = 0; k < nop; k++) {
			op[k] = nop == 3 ? k == 1 : (rnd() % 4 == 0);
			val[k] = op[k] ? rnd() : rnd_gain();
		}
		if (run_case(c, ch, dec, op, val, nop) && failed > 20)
			break;
	}
	printf("t32_awbtg_oracle: %u cases (%u frozen, %u with the luma override), %u writes, %u mismatches\n",
	       compared, nfrz, nov, nwrites, failed);
	return failed ? 1 : 0;
}
