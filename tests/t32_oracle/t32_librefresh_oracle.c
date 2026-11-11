// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our parameter refresh (driver/t32/tx_isp_t32_librefresh.h: lib_tisp_refresh_para) against Matt
 * Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc).
 *
 * Compared bit-exact: every callee call in order (block, channel, mode where the vendor passes it, the defog state
 * words, the IQ bin parse arguments, the wake-up arguments), the tuning blocks of both channels, the refresh state
 * bytes dad90 (random start values), the return value.
 * Inputs: channel 0 / 1, mode byte 0 / 1 / random with random upper bits, running byte 1 / random, random request.
 *
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_librefresh.h"

u64 lib_tisp_refresh_para(u32, u32, u32, u32);

#define F(fn)	((t32o_fn)(void (*)(void))(fn))

u8 t32_data_dc060[4], t32_data_dad90[4], t32_data_dad94[16];

static u32 rs = 0x6b1e3d27u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

enum { C_PARSE = 100, C_WAKE };
struct call { u32 id, a[4]; };
#define NC 32
static struct call vc[NC], oc[NC];
static unsigned int vn, on;

static void rec(struct call *t, unsigned int *n, u32 id, u32 a0, u32 a1, u32 a2, u32 a3)
{
	if (*n < NC)
		t[*n] = (struct call){ id, { a0, a1, a2, a3 } };
	(*n)++;
}

/* vendor side callees */
u64 tisp_bin_data_parse(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; rec(vc, &vn, C_PARSE, a0, a1, a2, 0); return rnd(); }
u64 __wake_up(u32 a0, u32 a1, u32 a2, u32 a3) { rec(vc, &vn, C_WAKE, a0 == T32O_PTR(t32_data_dad94), a1, a2, a3); return rnd(); }
#define DN1(name, blk) u64 name(u32 a0, u32 a1, u32 a2, u32 a3) \
	{ (void)a1; (void)a2; (void)a3; rec(vc, &vn, blk, a0, 0, 0, 0); return rnd(); }
#define DN2(name, blk) u64 name(u32 a0, u32 a1, u32 a2, u32 a3) \
	{ (void)a2; (void)a3; rec(vc, &vn, blk, a0, a1, 0, 0); return rnd(); }
DN1(tisp_top_api_dn_par_refresh, T32_RF_TOP)
DN2(tisp_dpc_dn_params_refresh, T32_RF_DPC)
DN2(tisp_gib_dn_params_refresh, T32_RF_GIB)
DN1(api_ae_dn_par_refresh, T32_RF_AE)
DN2(tisp_awb_dn_params_refresh, T32_RF_AWB)
DN2(tisp_lsc_dn_params_refresh, T32_RF_LSC)
DN2(tisp_tmo_dn_params_refresh, T32_RF_TMO)
DN2(tisp_dmsc_dn_params_refresh, T32_RF_DMSC)
DN2(tisp_gamma_dn_params_refresh, T32_RF_GAMMA)
DN2(tisp_ccm_dn_params_refresh, T32_RF_CCM)
DN2(tisp_clm_dn_params_refresh, T32_RF_CLM)
DN2(tisp_bcsh_dn_params_refresh, T32_RF_BCSH)
DN2(tisp_ysp_dn_params_refresh, T32_RF_YSP)
DN2(tisp_mdns_dn_params_refresh, T32_RF_MDNS)
DN2(tisp_ydns_dn_params_refresh, T32_RF_YDNS)
DN1(tisp_sdns_dn_params_refresh, T32_RF_SDNS)
DN2(tisp_cdns_dn_params_refresh, T32_RF_CDNS)
u64 tisp_defog_dn_params_refresh(u32 a0, u32 a1, u32 a2, u32 a3) { rec(vc, &vn, T32_RF_DEFOG, a0, a1, a2, a3); return rnd(); }
DN2(tisp_lce_dn_params_refresh, T32_RF_LCE)
DN2(tisp_rdns_dn_params_refresh, T32_RF_RDNS)
DN2(tisp_decmp_dn_params_refresh, T32_RF_DECMP)
DN2(tisp_csccr_dn_params_refresh, T32_RF_CSCCR)

/* our hooks */
static u32 o_parse(void *c, u32 ch, u32 size, u32 addr) { (void)c; rec(oc, &on, C_PARSE, ch, size, addr, 0); return 0; }
static void o_dn(void *c, enum t32_refresh_blk blk, u32 ch, u32 mode, u32 w0, u32 w1)
{
	(void)c;
	if (blk == T32_RF_TOP || blk == T32_RF_AE || blk == T32_RF_SDNS)
		rec(oc, &on, blk, ch, 0, 0, 0);
	else
		rec(oc, &on, blk, ch, mode, w0, w1);
}
static void o_wake(void *c, void *wq, u32 mode, u32 nr, void *key)
{
	(void)c;
	rec(oc, &on, C_WAKE, wq == (void *)t32_data_dad94, mode, nr, (u32)(uintptr_t)key);
}

int main(void)
{
	unsigned int c, i, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 20000;
	unsigned int compared = 0, failed = 0;
	struct t32_refresh_hooks h = { NULL, o_parse, o_dn, o_wake };
	static u8 tun_o[2][64], st_o[4];
	u8 *tun_po[2] = { tun_o[0], tun_o[1] };
	const u32 *cs_o[2];
	struct t32_refresh_env e = { tun_po, NULL, cs_o, st_o, t32_data_dad94 };

	for (c = 0; c < n; c++) {
		u32 r, o, bad, ch = c & 1, k;
		u8 *tun_v[2], *req;
		struct t32_refresh_req q;

		t32o_reset();
		vn = on = 0;
		memset(vc, 0, sizeof(vc));
		memset(oc, 0, sizeof(oc));
		for (k = 0; k < 2; k++) {
			tun_v[k] = t32o_alloc(64);
			for (i = 0; i < 64; i++)
				tun_v[k][i] = tun_o[k][i] = (u8)rnd();
			t32_data_dd0a4[k] = T32O_PTR(tun_v[k]);
			t32_data_dc060[k] = c % 5 == 0 ? (u8)rnd() : 1;
		}
		for (i = 0; i < sizeof(t32_data_dc284); i++)
			t32_data_dc284[i] = (u8)rnd();
		cs_o[0] = (const u32 *)t32_data_dc284;
		cs_o[1] = (const u32 *)(t32_data_dc284 + 904);
		e.running = t32_data_dc060;
		for (i = 0; i < 4; i++)
			t32_data_dad90[i] = st_o[i] = (u8)rnd();
		req = t32o_alloc(32);
		for (i = 0; i < 32; i++)
			req[i] = (u8)rnd();
		req[17] = (u8)ch;
		k = c % 3 == 0 ? 1 : (c % 3 == 1 ? 0 : rnd() & 0xff);
		memcpy(req + 12, (u32[]){ (rnd() & ~0xffu) | k }, 4);
		memcpy(&q, req, sizeof(q));

		r = (u32)t32o_call(F(lib_tisp_refresh_para), T32O_PTR(req), rnd(), rnd(), rnd(), 0);
		o = t32_lib_refresh_para(&h, &e, &q);
		bad = r != o || vn != on || vn > NC || memcmp(vc, oc, sizeof(vc)) || memcmp(tun_v[0], tun_o[0], 64) ||
		      memcmp(tun_v[1], tun_o[1], 64) || memcmp(t32_data_dad90, st_o, 4);
		compared++;
		if (bad && failed++ < 10)
			printf("MISMATCH case %u ch %u mode %x: ret %x/%x calls %u/%u\n", c, ch, k, r, o, vn, on);
	}
	printf("t32_librefresh_oracle: %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
