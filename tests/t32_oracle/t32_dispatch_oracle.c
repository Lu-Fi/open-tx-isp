// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: the per-frame dispatchers of driver/t32/tx_isp_t32_dispatch.h
 * against Matt Davis' recovered vendor C (ref/, PR #15 @ 2e2e06bc):
 * tisp_ct_update, tisp_short_tgain_update, tisp_short_ev_update,
 * tisp_lcac_no_refresh.
 *
 * Compared: the sequence of block calls (channel, 64-bit argument; the o32
 * padding register a1 is not an argument), the bypass register reads
 * (d03a0 = 1: the vendor reads shadow index 2 for 0x40), every register write
 * (d03a0 = 1: shadow index <-> address one to one and the channel tag), and
 * return values.  Inputs: random channel, 64-bit values, force / keep words
 * 0 / 1 / other, bypass words with random bits, WDR byte 0 / 1 / other,
 * random LCAC parameter blocks.  Exit 0 = all equal, 1 = mismatch.
 */
#include <stdlib.h>
#include "t32o8_rec.h"
#include "tx_isp_t32_dispatch.h"

u64 tisp_ct_update(u32, u32, u32, u32, u32);
u64 tisp_short_tgain_update(u32, u32, u32, u32);
u64 tisp_short_ev_update(u32, u32, u32, u32);
u64 tisp_lcac_no_refresh(u32, u32, u32, u32);

enum { F_READ = 1, F_CCM, F_LSC, F_BCSH, F_CLM, F_GIB, F_DPC, F_LSCS, F_WDR };

u32 t32_data_e35e0[4];
static u32 byp;

u64 tisp_reg_read(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	t32o8_rec(&t32o8_v, F_READ, a0, !t32_data_d03a0[0] ? a1 : a1 == 2 ? 0x40 : a1 | 0x10000, 0, 0);
	return byp;
}
#define BLK(name, id) \
	u64 name(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; t32o8_rec(&t32o8_v, id, a0, a2, a3, 0); return 0; }
BLK(tisp_ccm_ct_update, F_CCM)
BLK(tisp_lsc_ct_update, F_LSC)
BLK(tisp_bcsh_ct_update, F_BCSH)
BLK(tisp_clm_ct_update, F_CLM)
BLK(tisp_dpc_s_gain_update, F_DPC)
BLK(tisp_lsc_tgain_update_s, F_LSCS)
BLK(tisp_wdr_short_ev_update, F_WDR)
u64 tisp_gib_again_s_update(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	t32o8_rec(&t32o8_v, F_GIB, a0, a1, 0, 0);
	return 0;
}

static struct { u32 reg, val, ch; } ow[16];
static unsigned int nw;
static u32 o_read(u32 ch, u32 reg) { t32o8_rec(&t32o8_o, F_READ, ch, reg, 0, 0); return byp; }
static void o_write(u32 ch, u32 reg, u32 val) { ow[nw].reg = reg; ow[nw].val = val; ow[nw++].ch = ch; }
#define OBLK(name, id) static void name(u32 ch, u64 v) { t32o8_rec(&t32o8_o, id, ch, (u32)v, (u32)(v >> 32), 0); }
OBLK(o_ccm, F_CCM)
OBLK(o_lsc, F_LSC)
OBLK(o_bcsh, F_BCSH)
OBLK(o_clm, F_CLM)
OBLK(o_dpc, F_DPC)
OBLK(o_lscs, F_LSCS)
OBLK(o_wdr, F_WDR)
static void o_gib(u32 ch, u32 g) { t32o8_rec(&t32o8_o, F_GIB, ch, g, 0, 0); }
static const struct t32_dispatch_ops ops = { o_read, o_write, o_ccm, o_lsc, o_bcsh, o_clm, o_gib, o_dpc, o_lscs, o_wdr };

static u32 rs = 0xd15ba7c4u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}
static u32 pick(void) { return (rnd() % 3) ? rnd() & 1 : rnd(); }

int main(void)
{
	unsigned int c, i, failed = 0, compared = 0, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 40000;

	for (c = 0; c < n; c++) {
		u32 which = c % 4, mode = rnd() & 1, ch = rnd() % 4, lo = rnd(), hi = rnd(), vr, orr = 0;
		u64 v = (u64)hi << 32 | lo;
		int bad;

		t32o_reset();
		t32o8_clear();
		nw = 0;
		t32_data_d03a0[0] = (u8)mode;
		byp = (rnd() & 1) ? rnd() : rnd() & (T32_BYP_DPC | T32_BYP_GIB | T32_BYP_LSC | T32_BYP_CCM | T32_BYP_BCSH | T32_BYP_CLM);
		switch (which) {
		case 0: {
			u32 f = pick(), kp = pick();

			t32o_arg5 = rnd();
			t32o_arg6 = f;
			t32o_arg7 = kp;
			vr = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_ct_update, ch, rnd(), lo, hi, rnd());
			t32o_arg5 = t32o_arg6 = t32o_arg7 = 0;
			orr = (u32)t32_ct_update(&ops, ch, v, f, kp);
			break;
		}
		case 1:
			vr = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_short_tgain_update, ch, rnd(), lo, hi, 0);
			orr = (u32)t32_short_tgain_update(&ops, ch, v);
			break;
		case 2: {
			u8 w = (u8)pick();

			t32_data_dc284[(ch & 1) * 904 + 36] = w;
			ch &= 1;
			vr = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_short_ev_update, ch, rnd(), lo, hi, 0);
			orr = (u32)t32_short_ev_update(&ops, ch, w, v);
			break;
		}
		default: {
			u8 *p = t32o_alloc(8320);
			u32 *ctx = t32o_alloc(16);

			for (i = 8200; i < 8300; i++)
				p[i] = (u8)rnd();
			ctx[0] = T32O_PTR(p);
			t32_data_e35e0[ch] = T32O_PTR(ctx);
			vr = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_lcac_no_refresh, ch, 0, 0, 0, 0);
			t32_lcac_no_refresh(&ops, ch, p);
			vr = orr = 0;	/* a tail call of tisp_reg_write */
			break;
		}
		}
		bad = t32o8_cmp("dispatch", failed < 8) || vr != orr || t32o_log.n != nw;
		for (i = 0; !bad && i < nw; i++) {
			u32 r = t32o_log.w[i].reg;

			bad = t32o_log.w[i].val != ow[i].val || !t32o_reg_eq(r, ow[i].reg) ||
			      (mode && (r >> 24 & 7) != (ow[i].ch & 7));
		}
		compared++;
		if (bad && failed++ < 8)
			printf("MISMATCH case %u (d03a0 %u ch %u): ret %08x / %08x writes %u / %u\n", which, mode, ch, vr, orr,
			       t32o_log.n, nw);
	}
	printf("t32_dispatch_oracle: %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
