// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our channel init (driver/t32/tx_isp_t32_libinit.h: lib_tisp_init) against Matt Davis'
 * mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc), with the reference fix_point_mult2_32 and
 * tisp_log2_fixed_to_fixed linked in.
 *
 * Compared bit-exact: every callee call in order with its arguments (block inits with channel and state pointer,
 * sensor_init pointer, event callback ids and functions, the startup updates with their u64 value and the
 * stack words after it, the level-2 error prints with their line), the 2 x 904 B channel state (dc284), dc074,
 * f0918, f0900, dc060 (random start values), e4018 / e4100 / dd2d4 unchanged, the return value.
 * Inputs: channel 0..1 with random upper bits, random attributes (WDR byte 0, 1 or random), tuning block 0 or
 * set, event_init 0 or failing, pending flag 1 or random, random AE state.
 *
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_libinit.h"

u64 lib_tisp_init(u32, u32, u32, u32);

#define F(fn)	((t32o_fn)(void (*)(void))(fn))
enum { ID_ERR = 100, ID_RESET, ID_RCFG, ID_SENSOR, ID_EVINIT, ID_IPC, ID_SETCB, ID_CORE, ID_POLL, ID_CSCCR,
       ID_UPD = 200 };

u32 t32_data_f0918[4];
extern u8 t32_data_e4100[0x10000], t32_data_f0900[0x10000], t32_data_dd2d4[0x400];
extern u32 t32_data_dc074[8];
u8 t32_data_e4018[4], t32_data_dc060[4];
char t32_data_bf284[4] = "f", t32_data_c7e08[4] = "f", t32_data_c7e3c[4] = "f", t32_data_c7e48[4] = "f",
     t32_data_c7e54[4] = "f", t32_data_c7e88[4] = "f", t32_data_c7eb0[4] = "f";

static u32 rs = 0x6c1d93e5u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

struct call { u32 id, a[3], s[8]; };
#define NC 64
static struct call vc[NC], oc[NC];
static unsigned int vn, on;
static u32 ev_ret;

static void rec(struct call *t, unsigned int *n, u32 id, u32 a0, u32 a1, u32 a2, const u32 *s)
{
	if (*n < NC) {
		t[*n] = (struct call){ id, { a0, a1, a2 }, { 0 } };
		if (s)
			memcpy(t[*n].s, s, sizeof(t[*n].s));
	}
	(*n)++;
}

#define BI(name, blk) u64 name(u32 a0, u32 a1, u32 a2, u32 a3) \
	{ (void)a2; (void)a3; rec(vc, &vn, blk, a0, a1, 0, NULL); return rnd(); }
BI(tisp_csc_init, T32_IB_CSC) BI(tisp_csccr_init, T32_IB_CSCCR) BI(tisp_top_init, T32_IB_TOP)
BI(tisp_ae_init, T32_IB_AE) BI(tisp_awb_init, T32_IB_AWB) BI(tisp_af_init, T32_IB_AF) BI(tisp_tmo_init, T32_IB_TMO)
BI(tisp_dmsc_init, T32_IB_DMSC) BI(tisp_gamma_init, T32_IB_GAMMA) BI(tisp_cdns_init, T32_IB_CDNS)
BI(tisp_gib_init, T32_IB_GIB) BI(tisp_dpc_init, T32_IB_DPC) BI(tisp_lsc_init, T32_IB_LSC)
BI(tisp_defog_init, T32_IB_DEFOG) BI(tisp_ydns_init, T32_IB_YDNS) BI(tisp_ccm_init, T32_IB_CCM)
BI(tisp_bcsh_init, T32_IB_BCSH) BI(tisp_lce_init, T32_IB_LCE) BI(tisp_sdns_init, T32_IB_SDNS)
BI(tisp_ysp_init, T32_IB_YSP) BI(tisp_mdns_init, T32_IB_MDNS) BI(tisp_clm_init, T32_IB_CLM)
BI(tisp_gsm_init, T32_IB_GSM) BI(tisp_rdns_init, T32_IB_RDNS) BI(tisp_dma_init, T32_IB_DMA)
BI(tisp_decmp_init, T32_IB_DECMP) BI(tisp_ldc_init, T32_IB_LDC) BI(tisp_wdr_init, T32_IB_WDR)
BI(tisp_msca_init, T32_IB_MSCA) BI(tisp_rcfg_init, ID_RCFG) BI(sensor_init, ID_SENSOR)
BI(lib_tisp_set_csccr, ID_CSCCR)

#define V0(name, id) u64 name(u32 a0, u32 a1, u32 a2, u32 a3) \
	{ (void)a0; (void)a1; (void)a2; (void)a3; rec(vc, &vn, id, 0, 0, 0, NULL); return rnd(); }
V0(lib_tisp_all_reset, ID_RESET) V0(tisp_core_tuning_init, ID_CORE) V0(tisp_code_tuning_poll_init, ID_POLL)

u64 tisp_top_ipc_init(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	rec(vc, &vn, ID_IPC, a0, 0, 0, NULL);
	return rnd();
}
u64 tisp_event_init(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	rec(vc, &vn, ID_EVINIT, a0, 0, 0, NULL);
	return ev_ret;
}
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2;
	if (a0 != 1)	/* level-1 banners are not ported */
		rec(vc, &vn, ID_ERR, a0, a3, 0, NULL);
	return 0;
}

/* the update callbacks: (ch, pad, u64 value, 4 x u64 on the stack) */
#define UP(name, cb) u64 name(u32 a0, u32 a1, u32 a2, u32 a3, u32 s0, u32 s1, u32 s2, u32 s3, u32 s4, u32 s5, \
		u32 s6, u32 s7) \
	{ u32 s[8] = { s0, s1, s2, s3, s4, s5, s6, s7 }; (void)a1; rec(vc, &vn, ID_UPD + cb, a0, a2, a3, s); \
	  return rnd(); }
UP(tisp_long_tgain_update, T32_ICB_LONG_TGAIN) UP(tisp_short_tgain_update, T32_ICB_SHORT_TGAIN)
UP(tisp_long_again_update, T32_ICB_LONG_AGAIN) UP(tisp_long_ev_update, T32_ICB_LONG_EV)
UP(tisp_short_ev_update, T32_ICB_SHORT_EV) UP(tisp_ct_update, T32_ICB_CT) UP(tisp_ae_ir_update, T32_ICB_AE_IR)
UP(tisp_wdr_short_ev_update, T32_ICB_WDR_SHORT_EV)

static u32 cb_id(u32 fn)
{
	static const struct { void *fn; u32 id; } t[] = {
		{ (void *)tisp_long_tgain_update, T32_ICB_LONG_TGAIN }, { (void *)tisp_short_tgain_update, T32_ICB_SHORT_TGAIN },
		{ (void *)tisp_long_again_update, T32_ICB_LONG_AGAIN }, { (void *)tisp_long_ev_update, T32_ICB_LONG_EV },
		{ (void *)tisp_short_ev_update, T32_ICB_SHORT_EV }, { (void *)tisp_ct_update, T32_ICB_CT },
		{ (void *)tisp_ae_ir_update, T32_ICB_AE_IR },
	};
	unsigned int i;

	for (i = 0; i < sizeof(t) / sizeof(t[0]); i++)
		if (fn == T32O_PTR(t[i].fn))
			return t[i].id;
	return 0xdead;
}
u64 tisp_event_set_cb(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a3;
	rec(vc, &vn, ID_SETCB, a0, a1, cb_id(a2), NULL);
	return rnd();
}

/* our side: own copies of the written data; state pointers are mapped back to the vendor addresses */
static u8 par_o[1808];
static u32 o_par(const u8 *p) { return T32O_PTR(t32_data_dc284) + (u32)(p - par_o); }
static void o_err(void *c, u32 line) { (void)c; rec(oc, &on, ID_ERR, 2, line, 0, NULL); }
static void o_reset(void *c) { (void)c; rec(oc, &on, ID_RESET, 0, 0, 0, NULL); }
static void o_rcfg(void *c, u32 ch, u8 *p) { (void)c; rec(oc, &on, ID_RCFG, ch, o_par(p), 0, NULL); }
static void o_sensor(void *c, u8 *s, u32 ch) { (void)c; rec(oc, &on, ID_SENSOR, T32O_PTR(s), ch, 0, NULL); }
static void o_blk(void *c, enum t32_init_blk b, u32 ch, u8 *p) { (void)c; rec(oc, &on, b, ch, o_par(p), 0, NULL); }
static u32 o_evinit(void *c, u32 ch) { (void)c; rec(oc, &on, ID_EVINIT, ch, 0, 0, NULL); return ev_ret; }
static void o_ipc(void *c, u8 *p) { (void)c; rec(oc, &on, ID_IPC, o_par(p), 0, 0, NULL); }
static void o_setcb(void *c, u32 ch, u32 ev, enum t32_init_cb cb) { (void)c; rec(oc, &on, ID_SETCB, ch, ev, cb, NULL); }
static void o_upd(void *c, enum t32_init_cb cb, u32 ch, uint64_t v)
{
	static const u32 s[8] = { 0, 0, 1, 0, 0, 0, 1, 0 };	/* the vendor's constant stack words */

	(void)c;
	rec(oc, &on, ID_UPD + cb, ch, (u32)v, (u32)(v >> 32), s);
}
static void o_core(void *c) { (void)c; rec(oc, &on, ID_CORE, 0, 0, 0, NULL); }
static void o_poll(void *c) { (void)c; rec(oc, &on, ID_POLL, 0, 0, 0, NULL); }
static void o_csccr(void *c, u32 ch, u32 m) { (void)c; rec(oc, &on, ID_CSCCR, ch, m, 0, NULL); }

static void put32(u8 *p, u32 o, u32 v) { memcpy(p + o, &v, 4); }

int main(void)
{
	unsigned int c, i, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 20000;
	unsigned int compared = 0, failed = 0;
	struct t32_init_hooks h = { NULL, o_err, o_reset, o_rcfg, o_sensor, o_blk, o_evinit, o_ipc, o_setcb, o_upd,
				    o_core, o_poll, o_csccr };
	static u32 max_o[8], rcfg_o[4];
	static u8 pend_o[16], run_o[4], e4018_s[4], ae_s[0x10000], sens_s[0x400];
	static void *tun_o[4];
	struct t32_init_env e = { par_o, max_o, tun_o, t32_data_dd2d4, rcfg_o, pend_o, t32_data_e4100, run_o,
				  t32_data_e4018 };

	for (c = 0; c < n; c++) {
		u32 ch = (rnd() & ~0xffu) | (c & 1), bad, r, ro, k;
		u8 *attr;

		t32o_reset();
		vn = on = 0;
		memset(vc, 0, sizeof(vc));
		memset(oc, 0, sizeof(oc));
		attr = t32o_alloc(904);
		for (i = 0; i < 904; i++)
			attr[i] = (u8)rnd();
		k = rnd() % 4;
		attr[36] = k == 0 ? 0 : k == 1 ? 1 : (u8)rnd();
		for (i = 0; i < 1808; i++)
			par_o[i] = t32_data_dc284[i] = (u8)rnd();
		for (i = 0; i < 8; i++)
			max_o[i] = t32_data_dc074[i] = rnd() % 3 ? rnd() % 4096 : rnd();
		for (i = 0; i < 4; i++) {
			t32_data_dd0a4[i] = rnd() % 4 ? rnd() | 1 : 0;
			tun_o[i] = (void *)(uintptr_t)t32_data_dd0a4[i];
			rcfg_o[i] = t32_data_f0918[i] = rnd();
			run_o[i] = t32_data_dc060[i] = (u8)rnd();
			e4018_s[i] = t32_data_e4018[i] = (u8)rnd();
		}
		for (i = 0; i < 16; i++)
			pend_o[i] = t32_data_f0900[i] = rnd() % 3 ? 1 : (u8)rnd();
		for (i = 0; i < 2; i++) {
			u8 *ae = t32_data_e4100 + 12800 * i;
			u32 sh = rnd() % 32;

			put32(ae, 188, rnd());
			put32(ae, 12528, rnd() >> sh);
			put32(ae, 12532, rnd() >> (rnd() % 32));
			put32(ae, 12552, rnd() % 4 ? rnd() >> (rnd() % 32) : rnd() % 2048);
		}
		for (i = 0; i < 0x400; i++)
			t32_data_dd2d4[i] = (u8)rnd();
		memcpy(ae_s, t32_data_e4100, sizeof(ae_s));
		memcpy(sens_s, t32_data_dd2d4, sizeof(sens_s));
		ev_ret = rnd() % 3 ? 0 : rnd() | 1;

		r = (u32)t32o_call(F(lib_tisp_init), ch, T32O_PTR(attr), rnd(), rnd(), 0);
		ro = t32_lib_init(&h, &e, ch, attr);
		bad = r != ro || vn != on || vn > NC || memcmp(vc, oc, sizeof(vc)) || memcmp(par_o, t32_data_dc284, 1808) ||
		      memcmp(max_o, t32_data_dc074, 32) || memcmp(rcfg_o, t32_data_f0918, 16) ||
		      memcmp(pend_o, t32_data_f0900, 16) || memcmp(run_o, t32_data_dc060, 4) ||
		      memcmp(e4018_s, t32_data_e4018, 4) || memcmp(ae_s, t32_data_e4100, sizeof(ae_s)) ||
		      memcmp(sens_s, t32_data_dd2d4, sizeof(sens_s));
		for (i = 0; !bad && i < 4; i++)
			bad = t32_data_dd0a4[i] != (u32)(uintptr_t)tun_o[i];
		compared++;
		if (bad && failed++ < 10)
			printf("MISMATCH case %u ch %x: calls %u/%u ret %x/%x\n", c, ch, vn, on, r, ro);
	}
	printf("t32_libinit_oracle: %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
