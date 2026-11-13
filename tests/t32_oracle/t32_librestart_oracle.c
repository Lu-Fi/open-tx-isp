// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our ISP restart (driver/t32/tx_isp_t32_librestart.h: lib_tisp_restart) against Matt Davis'
 * mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc).
 *
 * Compared bit-exact: every block call in order (function, channel argument where the vendor passes one, state
 * pointer of top_init / top_ipc_init), the printk source line, the return value (random per callee).
 * REFFIX libvici_fix.sh (recording printk).
 * Inputs: pipeline count 0 / 1 / 2 / random.
 *
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_librestart.h"

u64 lib_tisp_restart(u32, u32, u32, u32);

#define F(fn)	((t32o_fn)(void (*)(void))(fn))
#define ID_PRINTK	100
#define ID_MDNS_START	101

char t32_data_bf170[4] = "f", t32_data_c8460[4] = "f";

static u32 rs = 0x2d4c8e1bu;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

struct call { u32 id, a0, st; };
#define NC 64
static struct call vc[NC], oc[NC];
static unsigned int vn, on;
static u32 ret_of[128];

static void rec(struct call *t, unsigned int *n, u32 id, u32 a0, u32 st)
{
	if (*n < NC)
		t[*n] = (struct call){ id, a0, st };
	(*n)++;
}
static u32 st_tag(u32 p) { return p == T32O_PTR(t32_data_dc284) ? 1 : (p ? 2 : 0); }

/* vendor side: K1 records the channel argument, K0 none (the vendor passes none), KS the state pointer */
#define K1(name, id) u64 name(u32 a0, u32 a1, u32 a2, u32 a3) \
	{ (void)a1; (void)a2; (void)a3; rec(vc, &vn, id, a0, 0); return ret_of[id]; }
#define K0(name, id) u64 name(u32 a0, u32 a1, u32 a2, u32 a3) \
	{ (void)a0; (void)a1; (void)a2; (void)a3; rec(vc, &vn, id, 0, 0); return ret_of[id]; }
K1(tisp_ae_sleep, T32_AOV_AE_SLEEP) K1(tisp_awb_sleep, T32_AOV_AWB_SLEEP) K0(tsip_defog_sleep, T32_AOV_DEFOG_SLEEP)
K1(tisp_mdns_sleep, T32_AOV_MDNS_SLEEP) K1(tisp_msca_sleep, T32_AOV_MSCA_SLEEP) K1(tisp_ipc_sleep, T32_AOV_IPC_SLEEP)
K1(tisp_top_sleep, T32_AOV_TOP_SLEEP) K1(tisp_wdr_sleep, T32_AOV_WDR_SLEEP) K0(lib_tisp_all_reset, T32_AOV_ALL_RESET)
K0(lib_tisp_intp_clear, T32_AOV_INTP_CLEAR) K0(lib_tisp_intp_enable, T32_AOV_INTP_ENABLE)
u64 tisp_top_init(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; rec(vc, &vn, T32_AOV_TOP_INIT, a0, st_tag(a1)); return ret_of[T32_AOV_TOP_INIT]; }
u64 tisp_top_ipc_init(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; rec(vc, &vn, T32_AOV_TOP_IPC_INIT, 0, st_tag(a0)); return ret_of[T32_AOV_TOP_IPC_INIT]; }
K1(tisp_ipc_wake_up, T32_AOV_IPC_WAKE) K1(tisp_top_wake_up, T32_AOV_TOP_WAKE) K1(tisp_ae_wake_up, T32_AOV_AE_WAKE)
K1(tisp_af_wake_up, T32_AOV_AF_WAKE) K1(tisp_awb_wake_up, T32_AOV_AWB_WAKE) K1(tisp_bcsh_wake_up, T32_AOV_BCSH_WAKE)
K1(tisp_ccm_wake_up, T32_AOV_CCM_WAKE) K1(tisp_cdns_wake_up, T32_AOV_CDNS_WAKE) K1(tisp_clm_wake_up, T32_AOV_CLM_WAKE)
K1(tisp_csc_wake_up, T32_AOV_CSC_WAKE) K1(tisp_csccr_wake_up, T32_AOV_CSCCR_WAKE) K1(tisp_decmp_wake_up, T32_AOV_DECMP_WAKE)
K1(tisp_defog_wake_up, T32_AOV_DEFOG_WAKE) K1(tisp_dmsc_wake_up, T32_AOV_DMSC_WAKE) K1(tisp_dpc_wake_up, T32_AOV_DPC_WAKE)
K1(tisp_gamma_wake_up, T32_AOV_GAMMA_WAKE) K1(tisp_gib_wake_up, T32_AOV_GIB_WAKE) K1(tisp_gsm_wake_up, T32_AOV_GSM_WAKE)
K1(tisp_lce_wake_up, T32_AOV_LCE_WAKE) K1(tisp_ldc_wake_up, T32_AOV_LDC_WAKE) K1(tisp_lsc_wake_up, T32_AOV_LSC_WAKE)
K1(tisp_mdns_wake_up, T32_AOV_MDNS_WAKE) K1(tisp_mdns_start, ID_MDNS_START) K1(tisp_msca_wake_up, T32_AOV_MSCA_WAKE)
K1(tisp_rdns_wake_up, T32_AOV_RDNS_WAKE) K1(tisp_sdns_wake_up, T32_AOV_SDNS_WAKE) K1(tisp_tmo_wake_up, T32_AOV_TMO_WAKE)
K1(tisp_tstp_wake_up, T32_AOV_TSTP_WAKE) K1(tisp_wdr_wake_up, T32_AOV_WDR_WAKE) K1(tisp_ydns_wake_up, T32_AOV_YDNS_WAKE)
K1(tisp_ysp_wake_up, T32_AOV_YSP_WAKE)
u64 t32o7_printk(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a3; rec(vc, &vn, ID_PRINTK, a2, 0); return ret_of[ID_PRINTK]; }

/* our hooks */
static u32 o_fn(void *c, enum t32_aov_fn fn, u32 ch, u8 *state)
{
	(void)c;
	if (fn == T32_AOV_DEFOG_SLEEP || fn == T32_AOV_ALL_RESET || fn == T32_AOV_INTP_CLEAR || fn == T32_AOV_INTP_ENABLE ||
	    fn == T32_AOV_TOP_IPC_INIT)
		ch = 0;
	rec(oc, &on, fn, ch, st_tag(T32O_PTR(state)));
	return ret_of[fn];
}
static void o_trace(void *c, u32 line) { (void)c; (void)line; }
static u32 o_printk(void *c, u32 line) { (void)c; rec(oc, &on, ID_PRINTK, line, 0); return ret_of[ID_PRINTK]; }
static u32 o_mdns(void *c, u32 ch) { (void)c; rec(oc, &on, ID_MDNS_START, ch, 0); return ret_of[ID_MDNS_START]; }

int main(void)
{
	unsigned int c, i, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 2000;
	unsigned int compared = 0, failed = 0;
	struct t32_restart_hooks h = { { NULL, o_fn, o_trace, NULL }, o_printk, o_mdns };

	for (c = 0; c < n; c++) {
		u32 r, o, pipes = c % 4 < 3 ? c % 4 : rnd(), bad;

		t32o_reset();
		vn = on = 0;
		memset(vc, 0, sizeof(vc));
		memset(oc, 0, sizeof(oc));
		for (i = 0; i < 128; i++)
			ret_of[i] = rnd();
		for (i = 0; i < sizeof(t32_data_dc284); i++)
			t32_data_dc284[i] = (u8)rnd();
		memcpy(t32_data_dc284 + 900, &pipes, 4);
		r = (u32)t32o_call(F(lib_tisp_restart), rnd(), rnd(), rnd(), rnd(), 0);
		o = t32_lib_restart(&h, t32_data_dc284);
		bad = r != o || vn != on || vn > NC || memcmp(vc, oc, sizeof(vc));
		compared++;
		if (bad && failed++ < 10)
			printf("MISMATCH case %u pipes %x: ret %x/%x calls %u/%u\n", c, pipes, r, o, vn, on);
	}
	printf("t32_librestart_oracle: %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
