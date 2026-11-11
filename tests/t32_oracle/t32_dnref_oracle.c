// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: tisp_day_or_night_par_refresh (tx_isp_t32_blkrest.h)
 * against the vendor function (Matt Davis' recovered C, PR #15 @ 2e2e06bc).
 * Per case random channel 0 / 1 (with high bits), day / night value (with
 * high bits), channel state words, wake-up result.  Compared: the 22 block
 * refreshes in order with their arguments (channel, dn, defog's two words;
 * AE / SDNS take only the channel), the wake-up call and its queue, the
 * return value, every byte of e2764, f0908 and dad90 (canary filled).
 * Exit 1 on the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_blkrest.h"

u8 t32_data_e2764[4], t32_data_dad90[4];
u32 t32_data_f0908[4], t32_data_dad94[4];
extern u64 tisp_day_or_night_par_refresh(u32, u32, u32, u32);

static u32 rs = 0xe6546b64u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

#define LOGN 32
struct hlog { unsigned int n; u32 e[LOGN][5]; };
static struct hlog vl, ol, *cur;
static u32 wret;
static void hrec(u32 k, u32 a, u32 b, u32 c, u32 d)
{
	if (cur->n < LOGN) { u32 *e = cur->e[cur->n]; e[0] = k; e[1] = a; e[2] = b; e[3] = c; e[4] = d; }
	cur->n++;
}
u64 tisp_top_api_dn_par_refresh(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; hrec(0, a0, a1, 0, 0); return 0; }
u64 tisp_dpc_dn_params_refresh(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; hrec(1, a0, a1, 0, 0); return 0; }
u64 tisp_gib_dn_params_refresh(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; hrec(2, a0, a1, 0, 0); return 0; }
u64 api_ae_dn_par_refresh(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; hrec(3, a0, 0, 0, 0); return 0; }
u64 tisp_awb_dn_params_refresh(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; hrec(4, a0, a1, 0, 0); return 0; }
u64 tisp_lsc_dn_params_refresh(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; hrec(5, a0, a1, 0, 0); return 0; }
u64 tisp_tmo_dn_params_refresh(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; hrec(6, a0, a1, 0, 0); return 0; }
u64 tisp_dmsc_dn_params_refresh(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; hrec(7, a0, a1, 0, 0); return 0; }
u64 tisp_gamma_dn_params_refresh(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; hrec(8, a0, a1, 0, 0); return 0; }
u64 tisp_ccm_dn_params_refresh(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; hrec(9, a0, a1, 0, 0); return 0; }
u64 tisp_clm_dn_params_refresh(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; hrec(10, a0, a1, 0, 0); return 0; }
u64 tisp_bcsh_dn_params_refresh(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; hrec(11, a0, a1, 0, 0); return 0; }
u64 tisp_ysp_dn_params_refresh(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; hrec(12, a0, a1, 0, 0); return 0; }
u64 tisp_mdns_dn_params_refresh(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; hrec(13, a0, a1, 0, 0); return 0; }
u64 tisp_ydns_dn_params_refresh(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; hrec(14, a0, a1, 0, 0); return 0; }
u64 tisp_sdns_dn_params_refresh(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; hrec(15, a0, 0, 0, 0); return 0; }
u64 tisp_cdns_dn_params_refresh(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; hrec(16, a0, a1, 0, 0); return 0; }
u64 tisp_defog_dn_params_refresh(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; hrec(17, a0, a1, a2, a3); return 0; }
u64 tisp_lce_dn_params_refresh(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; hrec(18, a0, a1, 0, 0); return 0; }
u64 tisp_rdns_dn_params_refresh(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; hrec(19, a0, a1, 0, 0); return 0; }
u64 tisp_decmp_dn_params_refresh(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; hrec(20, a0, a1, 0, 0); return 0; }
u64 tisp_csccr_dn_params_refresh(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; hrec(21, a0, a1, 0, 0); return 0; }
u64 __wake_up(u32 a0, u32 a1, u32 a2, u32 a3) { hrec(99, a0 == T32O_PTR(t32_data_dad94), a1, a2, a3); return wret; }
static void o_ref(enum t32_dn_blk b, u32 ch, u32 dn, u32 w0, u32 w1) { hrec(b, ch, dn, w0, w1); }
static u32 o_wk(void *wq) { return (u32)__wake_up(T32O_PTR(wq), 1, 1, 0); }
static const struct t32_dnr_ops ops = { o_ref, o_wk };

int main(void)
{
	static u8 pipe[1808];
	u8 done[4], ev[4];
	u32 cnt[4], i;
	unsigned long n, N = 20000;

	for (n = 0; n < N; n++) {
		struct t32_dnr d;
		u32 ch = rnd() & 1, a0 = ch | (rnd() & 0xffffff00u), dn = (rnd() & 1) ? rnd() & 1 : rnd(), rv, ro;

		for (i = 0; i < sizeof(pipe); i++) pipe[i] = rnd();
		for (i = 0; i < 4; i++) { done[i] = rnd(); ev[i] = rnd(); cnt[i] = rnd(); }
		wret = rnd();

		memset(&ol, 0, sizeof(ol)); cur = &ol;
		memcpy(t32_data_e2764, done, 4); memcpy(t32_data_dad90, ev, 4); memcpy(t32_data_f0908, cnt, 16);
		d.ops = &ops; d.pipe = pipe; d.done = done; d.count = cnt; d.ev = ev; d.wq = t32_data_dad94;
		ro = t32_day_or_night_par_refresh(&d, a0, dn);
		{
			u8 vd[4], ve[4];
			u32 vc[4];

			memcpy(vd, t32_data_e2764, 4); memcpy(ve, t32_data_dad90, 4); memcpy(vc, t32_data_f0908, 16);
			t32o_reset();
			memset(&vl, 0, sizeof(vl)); cur = &vl;
			memcpy(t32_data_e2764, vd, 4); memcpy(t32_data_dad90, ve, 4); memcpy(t32_data_f0908, vc, 16);
			memcpy(t32_data_dc284, pipe, sizeof(pipe));
		}
		rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_day_or_night_par_refresh, a0, dn, rnd(), rnd(), 0);

		if (rv != ro || vl.n != ol.n || memcmp(vl.e, ol.e, sizeof(vl.e)) || memcmp(t32_data_e2764, done, 4) ||
		    memcmp(t32_data_dad90, ev, 4) || memcmp(t32_data_f0908, cnt, 16)) {
			printf("t32_dnref_oracle: FAIL case %lu ch %#x dn %#x: ret %#x/%#x calls %u/%u\n", n, a0, dn, rv, ro, vl.n, ol.n);
			for (i = 0; i < vl.n && i < LOGN; i++)
				printf("  %u: %u %#x %#x %#x / %u %#x %#x %#x\n", i, vl.e[i][0], vl.e[i][1], vl.e[i][2], vl.e[i][3],
				       ol.e[i][0], ol.e[i][1], ol.e[i][2], ol.e[i][3]);
			return 1;
		}
	}
	printf("t32_dnref_oracle: PASS %lu cases\n", N);
	return 0;
}
