// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our lib_tisp_aov_sleep / lib_tisp_aov_wake_up (driver/t32/tx_isp_t32_libaov.h) against Matt
 * Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc).
 *
 * Compared bit-exact: every block call in order (function, channel, state pointer; ordered against the printk
 * traces and register writes), register writes, return value (where the vendor has one).  Inputs: channel
 * 0 / 1 with random upper bits, the channel's sensor word 1 / 0 / 2 / random, random callee results.
 * Not compared: the leftover argument of tisp_func_reset and of the argument-less calls.
 *
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_libaov.h"

u64 lib_tisp_aov_sleep(u32, u32, u32, u32);
u64 lib_tisp_aov_wake_up(u32, u32, u32, u32);

#define F(fn)	((t32o_fn)(void (*)(void))(fn))

char t32_data_bf19c[4] = "f", t32_data_bf184[4] = "f", t32_data_c8350[4] = "f";

static u32 rs = 0x452821e6u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

struct ev { u32 id, ch, st, nw, pr; };
#define NE 48
static struct ev ve[NE], oe[NE];
static unsigned int vn, on;
static u32 ret_of[64], o_nw, o_pr;
static struct { u32 reg, val; } ow[4];

static void vadd(u32 id, u32 ch, u32 st) { if (vn < NE) ve[vn] = (struct ev){ id, ch, st, t32o_log.n, t32o_log.printfs }; vn++; }
static u32 voff(u32 p) { return p - T32O_PTR(t32_data_dc284); }

/* kind 0: no argument, 1: (ch), 2: (state), 3: (ch, state) */
#define K0(name, id) u64 name(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; vadd(id, 0, 0); return ret_of[id]; }
#define K1(name, id) u64 name(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; vadd(id, a0, 0); return ret_of[id]; }
#define K2(name, id) u64 name(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; vadd(id, 0, voff(a0)); return ret_of[id]; }
#define K3(name, id) u64 name(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; vadd(id, a0, voff(a1)); return ret_of[id]; }
K1(tisp_ae_sleep, T32_AOV_AE_SLEEP) K1(tisp_awb_sleep, T32_AOV_AWB_SLEEP) K1(tsip_defog_sleep, T32_AOV_DEFOG_SLEEP)
K1(tisp_mdns_sleep, T32_AOV_MDNS_SLEEP) K1(tisp_msca_sleep, T32_AOV_MSCA_SLEEP) K1(tisp_ipc_sleep, T32_AOV_IPC_SLEEP)
K1(tisp_top_sleep, T32_AOV_TOP_SLEEP) K1(tisp_wdr_sleep, T32_AOV_WDR_SLEEP) K1(tisp_aov_rcfg_sleep, T32_AOV_RCFG_SLEEP)
K0(tisp_func_reset, T32_AOV_FUNC_RESET) K0(lib_tisp_intp_clear, T32_AOV_INTP_CLEAR)
K0(lib_tisp_intp_enable, T32_AOV_INTP_ENABLE) K3(tisp_top_init, T32_AOV_TOP_INIT) K2(tisp_top_ipc_init, T32_AOV_TOP_IPC_INIT)
K0(lib_tisp_all_reset, T32_AOV_ALL_RESET) K0(tisp_aov_rcfg_wake_up, T32_AOV_RCFG_WAKE)
K1(tisp_ipc_wake_up, T32_AOV_IPC_WAKE) K1(tisp_top_wake_up, T32_AOV_TOP_WAKE) K1(tisp_ae_wake_up, T32_AOV_AE_WAKE)
K1(tisp_af_wake_up, T32_AOV_AF_WAKE) K1(tisp_awb_wake_up, T32_AOV_AWB_WAKE) K1(tisp_bcsh_wake_up, T32_AOV_BCSH_WAKE)
K1(tisp_ccm_wake_up, T32_AOV_CCM_WAKE) K1(tisp_cdns_wake_up, T32_AOV_CDNS_WAKE) K1(tisp_clm_wake_up, T32_AOV_CLM_WAKE)
K1(tisp_csc_wake_up, T32_AOV_CSC_WAKE) K1(tisp_csccr_wake_up, T32_AOV_CSCCR_WAKE) K1(tisp_decmp_wake_up, T32_AOV_DECMP_WAKE)
K1(tisp_defog_wake_up, T32_AOV_DEFOG_WAKE) K1(tisp_dmsc_wake_up, T32_AOV_DMSC_WAKE) K1(tisp_dpc_wake_up, T32_AOV_DPC_WAKE)
K1(tisp_gamma_wake_up, T32_AOV_GAMMA_WAKE) K1(tisp_gib_wake_up, T32_AOV_GIB_WAKE) K1(tisp_gsm_wake_up, T32_AOV_GSM_WAKE)
K1(tisp_lce_wake_up, T32_AOV_LCE_WAKE) K1(tisp_ldc_wake_up, T32_AOV_LDC_WAKE) K1(tisp_lsc_wake_up, T32_AOV_LSC_WAKE)
K1(tisp_mdns_wake_up, T32_AOV_MDNS_WAKE) K1(tisp_msca_wake_up, T32_AOV_MSCA_WAKE) K1(tisp_rdns_wake_up, T32_AOV_RDNS_WAKE)
K1(tisp_sdns_wake_up, T32_AOV_SDNS_WAKE) K1(tisp_tmo_wake_up, T32_AOV_TMO_WAKE) K1(tisp_tstp_wake_up, T32_AOV_TSTP_WAKE)
K1(tisp_wdr_wake_up, T32_AOV_WDR_WAKE) K1(tisp_ydns_wake_up, T32_AOV_YDNS_WAKE) K1(tisp_ysp_wake_up, T32_AOV_YSP_WAKE)

static u8 st[1808];
static u32 o_fn(void *c, enum t32_aov_fn fn, u32 ch, u8 *s)
{
	(void)c;
	if (on < NE)
		oe[on] = (struct ev){ fn, ch, s ? (u32)(s - st) : 0, o_nw, o_pr };
	on++;
	return ret_of[fn];
}
static void o_trace(void *c, u32 line) { (void)c; (void)line; o_pr++; }
static u32 o_wr(void *c, u32 reg, u32 val)
{
	(void)c;
	if (o_nw < 4) {
		ow[o_nw].reg = reg;
		ow[o_nw].val = val;
	}
	o_nw++;
	return 0;
}

int main(void)
{
	unsigned int c, i, compared = 0, failed = 0, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 20000;
	struct t32_aov_hooks h = { NULL, o_fn, o_trace, o_wr };

	for (c = 0; c < n; c++) {
		u32 ch = (rnd() & ~0xffu) | (rnd() & 1), k = rnd() % 4, sn, r, o, bad;
		int sleep = c & 1, has_ret;

		t32o_reset();
		vn = on = o_nw = o_pr = 0;
		memset(ve, 0, sizeof(ve));
		memset(oe, 0, sizeof(oe));
		for (i = 0; i < sizeof(st); i++)
			t32_data_dc284[i] = st[i] = (u8)rnd();
		for (i = 0; i < 64; i++)
			ret_of[i] = rnd();
		sn = k < 2 ? 1 : k == 2 ? (rnd() % 2) * 2 : rnd();
		memcpy(t32_data_dc284 + 904 * (ch & 1) + 900, &sn, 4);
		memcpy(st + 904 * (ch & 1) + 900, &sn, 4);
		if (sleep) {
			r = (u32)t32o_call(F(lib_tisp_aov_sleep), ch, rnd(), rnd(), rnd(), 0);
			o = t32_lib_aov_sleep(&h, st, ch);
			has_ret = sn == 1 || (ch & 0xff) == 0;
		} else {
			r = (u32)t32o_call(F(lib_tisp_aov_wake_up), ch, rnd(), rnd(), rnd(), 0);
			o = t32_lib_aov_wake_up(&h, st, ch);
			has_ret = 1;
		}
		bad = (has_ret && r != o) || vn != on || memcmp(ve, oe, sizeof(ve)) || t32o_log.n != o_nw ||
		      t32o_log.printfs != o_pr || memcmp(t32_data_dc284, st, sizeof(st)) || vn > NE;
		for (i = 0; !bad && i < o_nw && i < 4; i++)
			bad = t32o_log.w[i].reg != ow[i].reg || t32o_log.w[i].val != ow[i].val;
		compared++;
		if (bad && failed++ < 10)
			printf("MISMATCH %s case %u ch %x sn %x: ret %x/%x calls %u/%u\n", sleep ? "sleep" : "wake", c, ch, sn,
			       r, o, vn, on);
	}
	printf("t32_libaov_oracle: %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
