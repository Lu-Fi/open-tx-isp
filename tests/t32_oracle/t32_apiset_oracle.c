// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our readable thin setters (driver/t32/tx_isp_t32_api_set.h)
 * against the vendor tisp_s_* / tisp_set_* entry points from Matt Davis'
 * mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc, built for the host
 * with env.c).
 *
 * The block functions they forward to are recording stubs here (vendor side:
 * the OEM symbol names; our side: the ops table calls the same stubs), so the
 * comparison covers: which callee, in which order, every argument the call
 * defines (the value byte behind the pointer for the BCSH setters, whose
 * pointer is a stack address), the return value, and every byte of the user
 * buffer, the mask attribute table f3258 (all channels) and the AE attribute
 * blocks (*f0904, all channels).
 *
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_api_set.h"

extern u8 t32_data_f3258[512];
extern u32 t32_data_f0904[4];

static u32 rs = 0x51e7a3u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

/* ---- call log ---- */
#define NREC 4
struct rec { const char *fn; unsigned int n; u32 a[7]; };
static struct rec lg[NREC];
static unsigned int nlg;
static u32 stub_ret;

static u64 rec(const char *fn, unsigned int n, const u32 *a)
{
	unsigned int i;

	if (nlg < NREC) {
		lg[nlg].fn = fn;
		lg[nlg].n = n;
		for (i = 0; i < 7; i++)
			lg[nlg].a[i] = i < n ? a[i] : 0;
	}
	nlg++;
	return stub_ret + nlg;	/* a different result per call: the return value tells which call it came from */
}

/* vendor callee stubs: n = arguments the call defines; D = a1 points to the value byte */
#define STUB(name, n) \
	u64 name(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6) \
	{ u32 a[7] = { a0, a1, a2, a3, a4, a5, a6 }; return rec(#name, n, a); }
#define STUBD(name) \
	u64 name(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6) \
	{ u32 a[7] = { a0, *(u8 *)T32O_HOST(a1) }; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6; \
	  return rec(#name, 2, a); }
u64 tisp_gib_api_set_blc(u32, u32, u32, u32, u32, u32, u32);
u64 tisp_bcsh_api_set_offset_rgb(u32, u32, u32, u32, u32, u32, u32);
u64 tisp_bcsh_api_set_brightness(u32, u32, u32, u32, u32, u32, u32);
u64 tisp_bcsh_api_set_contrast(u32, u32, u32, u32, u32, u32, u32);
u64 tisp_bcsh_api_set_saturation(u32, u32, u32, u32, u32, u32, u32);
u64 tisp_bcsh_api_set_hue(u32, u32, u32, u32, u32, u32, u32);
u64 tisp_ae_api_set_face_ae_tune_params(u32, u32, u32, u32, u32, u32, u32);
u64 tisp_csccr_api_set(u32, u32, u32, u32, u32, u32, u32);
u64 tisp_msca_api_set_osd_out_cfg(u32, u32, u32, u32, u32, u32, u32);
u64 tisp_ae_set_at_list(u32, u32, u32, u32, u32, u32, u32);
u64 tisp_ae_set_ev_list(u32, u32, u32, u32, u32, u32, u32);
u64 tisp_ae_api_set_exp_list(u32, u32, u32, u32, u32, u32, u32);
u64 api_ae_set_weight(u32, u32, u32, u32, u32, u32, u32);
u64 tisp_gamma_api_set(u32, u32, u32, u32, u32, u32, u32);
u64 tisp_awb_set_weight(u32, u32, u32, u32, u32, u32, u32);
u64 tisp_msca_set_mirr_flip(u32, u32, u32, u32, u32, u32, u32);
u64 tisp_af_api_set_weight(u32, u32, u32, u32, u32, u32, u32);
u64 tisp_ae_api_set_antiflick(u32, u32, u32, u32, u32, u32, u32);
u64 tisp_ae_api_set_scene_par(u32, u32, u32, u32, u32, u32, u32);
u64 tisp_msca_api_set_autozoom(u32, u32, u32, u32, u32, u32, u32);
u64 tisp_msca_api_set_line_out_cfg(u32, u32, u32, u32, u32, u32, u32);
u64 tisp_awb_api_set_ct_trend_offset(u32, u32, u32, u32, u32, u32, u32);
u64 tisp_msca_api_set_osd_out_attribute_cfg(u32, u32, u32, u32, u32, u32, u32);
u64 tisp_tmo_api_set_curve(u32, u32, u32, u32, u32, u32, u32);
u64 tisp_awb_api_set_ct(u32, u32, u32, u32, u32, u32, u32);
u64 tisp_dmsc_sharpness_set(u32, u32, u32, u32, u32, u32, u32);
u64 tisp_ysp_sharpness_set(u32, u32, u32, u32, u32, u32, u32);
u64 tisp_msca_api_set_fcrop(u32, u32, u32, u32, u32, u32, u32);
u64 api_ae_set_exprinfo_attr(u32, u32, u32, u32, u32, u32, u32);
u64 tisp_tmo_faceae_refresh(u32, u32, u32, u32, u32, u32, u32);
u64 tisp_set_scaler_level_control_set(u32, u32, u32, u32, u32, u32, u32);
STUB(tisp_gib_api_set_blc, 2)
STUB(tisp_bcsh_api_set_offset_rgb, 2)
STUBD(tisp_bcsh_api_set_brightness)
STUBD(tisp_bcsh_api_set_contrast)
STUBD(tisp_bcsh_api_set_saturation)
STUBD(tisp_bcsh_api_set_hue)
STUB(tisp_ae_api_set_face_ae_tune_params, 4)
STUB(tisp_csccr_api_set, 1)
STUB(tisp_msca_api_set_osd_out_cfg, 1)
STUB(tisp_ae_set_at_list, 3)
STUB(tisp_ae_set_ev_list, 3)
STUB(tisp_ae_api_set_exp_list, 3)
STUB(api_ae_set_weight, 2)
STUB(tisp_gamma_api_set, 3)
STUB(tisp_awb_set_weight, 2)
STUB(tisp_msca_set_mirr_flip, 2)
STUB(tisp_af_api_set_weight, 2)
STUB(tisp_ae_api_set_antiflick, 2)
STUB(tisp_ae_api_set_scene_par, 2)
STUB(tisp_msca_api_set_autozoom, 2)
STUB(tisp_msca_api_set_line_out_cfg, 2)
STUB(tisp_awb_api_set_ct_trend_offset, 2)
STUB(tisp_msca_api_set_osd_out_attribute_cfg, 2)
STUB(tisp_tmo_api_set_curve, 3)
STUB(tisp_awb_api_set_ct, 2)
STUB(tisp_dmsc_sharpness_set, 2)
STUB(tisp_ysp_sharpness_set, 2)
STUB(tisp_msca_api_set_fcrop, 4)
STUB(api_ae_set_exprinfo_attr, 2)
STUB(tisp_tmo_faceae_refresh, 7)
STUB(tisp_set_scaler_level_control_set, 2)

/* ---- our ops: the same stubs, pointer arguments as u32, value bytes by value ---- */
#define P(p)	T32O_PTR(p)
#define C2(f, a, b)	((int)f(a, b, 0, 0, 0, 0, 0))
static int o_gib(u32 c, u32 a) { return C2(tisp_gib_api_set_blc, c, a); }
static int o_off(u32 c, u32 a) { return C2(tisp_bcsh_api_set_offset_rgb, c, a); }
static u64 recb(const char *fn, u32 c, u8 v) { u32 a[7] = { c, v }; return rec(fn, 2, a); }
static int o_bri(u32 c, const u8 *v) { return (int)recb("tisp_bcsh_api_set_brightness", c, *v); }
static int o_con(u32 c, const u8 *v) { return (int)recb("tisp_bcsh_api_set_contrast", c, *v); }
static int o_sat(u32 c, const u8 *v) { return (int)recb("tisp_bcsh_api_set_saturation", c, *v); }
static int o_hue(u32 c, const u8 *v) { return (int)recb("tisp_bcsh_api_set_hue", c, *v); }
static int o_face(u32 c, const void *p0, const void *p1, const void *p2)
{ return (int)tisp_ae_api_set_face_ae_tune_params(c, P(p0), P(p1), P(p2), 0, 0, 0); }
static int o_csccr(u32 c) { return (int)tisp_csccr_api_set(c, 0, 0, 0, 0, 0, 0); }
static int o_osdo(u32 c) { return (int)tisp_msca_api_set_osd_out_cfg(c, 0, 0, 0, 0, 0, 0); }
static int o_at(u32 c, u32 n, const void *l) { return (int)tisp_ae_set_at_list(c, n, P(l), 0, 0, 0, 0); }
static int o_ev(u32 c, u32 n, const void *l) { return (int)tisp_ae_set_ev_list(c, n, P(l), 0, 0, 0, 0); }
static int o_exp(u32 c, u32 n, const void *l) { return (int)tisp_ae_api_set_exp_list(c, n, P(l), 0, 0, 0, 0); }
static int o_aew(u32 c, const void *b) { return C2(api_ae_set_weight, c, P(b)); }
static int o_gam(u32 c, const void *b, u32 f) { return (int)tisp_gamma_api_set(c, P(b), f, 0, 0, 0, 0); }
static int o_awbw(u32 c, const void *b) { return C2(tisp_awb_set_weight, c, P(b)); }
static int o_mf(u32 c, const void *b) { return C2(tisp_msca_set_mirr_flip, c, P(b)); }
static int o_afw(u32 c, const void *b) { return C2(tisp_af_api_set_weight, c, P(b)); }
static int o_afl(u32 c, const void *b) { return C2(tisp_ae_api_set_antiflick, c, P(b)); }
static int o_scn(u32 c, const void *b) { return C2(tisp_ae_api_set_scene_par, c, P(b)); }
static int o_az(u32 c, const void *b) { return C2(tisp_msca_api_set_autozoom, c, P(b)); }
static int o_line(u32 c, const void *b) { return C2(tisp_msca_api_set_line_out_cfg, c, P(b)); }
static int o_ctt(u32 c, const void *b) { return C2(tisp_awb_api_set_ct_trend_offset, c, P(b)); }
static int o_osda(u32 c, const void *b) { return C2(tisp_msca_api_set_osd_out_attribute_cfg, c, P(b)); }
static int o_tmoc(u32 c, u32 m, const void *b) { return (int)tisp_tmo_api_set_curve(c, m, P(b), 0, 0, 0, 0); }
static int o_ct(u32 c, u32 ct) { return C2(tisp_awb_api_set_ct, c, ct); }
static int o_dsh(u32 c, u32 l) { return C2(tisp_dmsc_sharpness_set, c, l); }
static int o_ysh(u32 c, u32 l) { return C2(tisp_ysp_sharpness_set, c, l); }
static int o_fcrop(u32 c, u32 w1, u32 w2, u32 w3) { return (int)tisp_msca_api_set_fcrop(c, w1, w2, w3, 0, 0, 0); }
static int o_expr(u32 c, const void *b) { return C2(api_ae_set_exprinfo_attr, c, P(b)); }
static int o_tmof(u32 c, u32 e, const void *p0, const void *p1, const void *p2, const void *p3, const void *p4)
{ return (int)tisp_tmo_faceae_refresh(c, e, P(p0), P(p1), P(p2), P(p3), P(p4)); }
static int o_scl(u32 c, const void *b) { return C2(tisp_set_scaler_level_control_set, c, P(b)); }

static const struct t32_api_set_ops ops = {
	.gib_api_set_blc = o_gib, .bcsh_api_set_offset_rgb = o_off,
	.bcsh_api_set_brightness = o_bri, .bcsh_api_set_contrast = o_con,
	.bcsh_api_set_saturation = o_sat, .bcsh_api_set_hue = o_hue,
	.ae_api_set_face_ae_tune_params = o_face, .csccr_api_set = o_csccr,
	.msca_api_set_osd_out_cfg = o_osdo, .ae_set_at_list = o_at, .ae_set_ev_list = o_ev,
	.ae_api_set_exp_list = o_exp, .ae_set_weight = o_aew, .gamma_api_set = o_gam,
	.awb_set_weight = o_awbw, .msca_set_mirr_flip = o_mf, .af_api_set_weight = o_afw,
	.ae_api_set_antiflick = o_afl, .ae_api_set_scene_par = o_scn,
	.msca_api_set_autozoom = o_az, .msca_api_set_line_out_cfg = o_line,
	.awb_api_set_ct_trend_offset = o_ctt, .msca_api_set_osd_out_attribute_cfg = o_osda,
	.tmo_api_set_curve = o_tmoc, .awb_api_set_ct = o_ct, .dmsc_sharpness_set = o_dsh,
	.ysp_sharpness_set = o_ysh, .msca_api_set_fcrop = o_fcrop, .ae_set_exprinfo_attr = o_expr,
	.tmo_faceae_refresh = o_tmof, .set_scaler_level_control_set = o_scl,
};

/* ---- vendor entry points (ref/, o32 shape) ---- */
#define REFS(X) \
	X(tisp_s_gib_blc) X(tisp_s_coefft_wb) X(tisp_set_brightness) X(tisp_set_contrast) \
	X(tisp_set_saturation) X(tisp_set_hue) X(tisp_s_face_ae_weiget) X(tisp_s_raw_rw_handler) \
	X(tisp_s_mdns_riscvEn) X(tisp_set_scaler_level_control) X(tisp_s_csccr_mode) \
	X(tisp_s_osd_block_attr) X(tisp_s_at_list) X(tisp_s_ev_list) X(tisp_s_ae_explist) \
	X(tisp_s_ae_weight_attr) X(tisp_s_Gamma) X(tisp_s_awb_weight_attr) X(tisp_s_hv_flip) \
	X(tisp_s_af_weight_attr) X(tisp_s_antiflick) X(tisp_s_ae_scence_attr) \
	X(tisp_s_autozoom_control) X(tisp_s_draw_block_attr) X(tisp_s_awb_ct_trend_offset) \
	X(tisp_s_osd_attr) X(tisp_s_tmo_curve) X(tisp_s_awb_ct) X(tisp_set_sharpness) \
	X(tisp_s_fcrop_control) X(tisp_s_ae_exprinfo_attr) X(tisp_s_tmo_faceae) X(tisp_set_scaler_level)
#define DECL(f) u64 f(u32, u32, u32, u32);
REFS(DECL)
#define ENT(f) { #f, (t32o_fn)(void (*)(void))f },
static const struct { const char *name; t32o_fn fn; } ent[] = { REFS(ENT) };
#define NENT (sizeof(ent) / sizeof(ent[0]))

#define BUFB	128
#define AEB	(4 * 220)
static u8 *buf, *ae;
static u8 buf0[BUFB], ae0[AEB], f0[512];

static int ours(unsigned int k, u32 a0, u32 a1, u32 a2, u32 a3)
{
	const u8 *b = buf;

	switch (k) {
	case 0: return t32_s_gib_blc(&ops, a0, a1);
	case 1: return t32_s_coefft_wb(&ops, a0, a1);
	case 2: return t32_set_brightness(&ops, a0, a1);
	case 3: return t32_set_contrast(&ops, a0, a1);
	case 4: return t32_set_saturation(&ops, a0, a1);
	case 5: return t32_set_hue(&ops, a0, a1);
	case 6: return t32_s_face_ae_weiget(&ops, a0, (const u32 *)b);
	case 7: return t32_s_raw_rw_handler();
	case 8: return t32_s_mdns_riscv_en(a0);
	case 9: return t32_set_scaler_level_control();
	case 10: return t32_s_csccr_mode(&ops, a0);
	case 11: return t32_s_osd_block_attr(&ops, a0);
	case 12: return t32_s_at_list(&ops, a0, b);
	case 13: return t32_s_ev_list(&ops, a0, b);
	case 14: return t32_s_ae_explist(&ops, a0, b);
	case 15: return t32_s_ae_weight_attr(&ops, a0, b);
	case 16: return t32_s_gamma(&ops, a0, b);
	case 17: return t32_s_awb_weight_attr(&ops, a0, b);
	case 18: return t32_s_hv_flip(&ops, a0, b);
	case 19: return t32_s_af_weight_attr(&ops, a0, b);
	case 20: return t32_s_antiflick(&ops, a0, b);
	case 21: return t32_s_ae_scence_attr(&ops, a0, b);
	case 22: return t32_s_autozoom_control(&ops, a0, b);
	case 23: return t32_s_draw_block_attr(&ops, a0, b);
	case 24: return t32_s_awb_ct_trend_offset(&ops, a0, b);
	case 25: return t32_s_osd_attr(&ops, a0, b);
	case 26: return t32_s_tmo_curve(&ops, a0, b);
	case 27: return t32_s_awb_ct(&ops, a0, a1);
	case 28: return t32_set_sharpness(&ops, a0, a1);
	case 29: return t32_s_fcrop_control(&ops, t32_data_f3258, a0, a1, a2, a3);
	case 30: return t32_s_ae_exprinfo_attr(&ops, ae, a0, b);
	case 31: return t32_s_tmo_faceae(&ops, a0, b);
	case 32: return t32_set_scaler_level(&ops, a0, b);
	}
	return -1;
}

/* the entry takes a buffer pointer in a1 (else a value) */
static int takes_buf(unsigned int k)
{
	return k == 6 || (k >= 12 && k <= 26) || k >= 30;
}

static unsigned int failed, compared;

static void run(unsigned int k, u32 a0, u32 a1, u32 a2, u32 a3)
{
	struct rec vl[NREC];
	unsigned int vn, i, j;
	u32 vr;
	int or_;
	u8 vbuf[BUFB], vae[AEB], vf[512];

	t32o_reset();
	buf = t32o_alloc(BUFB);
	ae = t32o_alloc(AEB);
	for (i = 0; i < BUFB; i++)
		buf0[i] = (u8)rnd();
	for (i = 0; i < AEB; i++)
		ae0[i] = (u8)rnd();
	for (i = 0; i < 512; i++)
		f0[i] = (u8)rnd();
	if (takes_buf(k))
		a1 = T32O_PTR(buf);
	stub_ret = rnd();

	memcpy(buf, buf0, BUFB); memcpy(ae, ae0, AEB); memcpy(t32_data_f3258, f0, 512);
	t32_data_f0904[0] = T32O_PTR(ae);
	nlg = 0;
	vr = (u32)t32o_call(ent[k].fn, a0, a1, a2, a3, 0);
	vn = nlg;
	memcpy(vl, lg, sizeof(vl));
	memcpy(vbuf, buf, BUFB); memcpy(vae, ae, AEB); memcpy(vf, t32_data_f3258, 512);

	memcpy(buf, buf0, BUFB); memcpy(ae, ae0, AEB); memcpy(t32_data_f3258, f0, 512);
	nlg = 0;
	or_ = ours(k, a0, a1, a2, a3);
	compared++;
	if (vn != nlg || vr != (u32)or_ || memcmp(vbuf, buf, BUFB) || memcmp(vae, ae, AEB) ||
	    memcmp(vf, t32_data_f3258, 512))
		goto bad;
	for (i = 0; i < vn && i < NREC; i++) {
		if (strcmp(vl[i].fn, lg[i].fn) || vl[i].n != lg[i].n)
			goto bad;
		for (j = 0; j < 7; j++)
			if (vl[i].a[j] != lg[i].a[j])
				goto bad;
	}
	return;
bad:
	if (failed++ < 8) {
		printf("MISMATCH %s a0=%08x a1=%08x a2=%08x a3=%08x: calls %u/%u ret %08x/%08x mem %d%d%d\n",
		       ent[k].name, a0, a1, a2, a3, vn, nlg, vr, (u32)or_, !!memcmp(vbuf, buf, BUFB),
		       !!memcmp(vae, ae, AEB), !!memcmp(vf, t32_data_f3258, 512));
		for (i = 0; i < vn && i < NREC; i++)
			printf("  vendor %s(%08x %08x %08x %08x %08x %08x %08x)\n", vl[i].fn, vl[i].a[0], vl[i].a[1],
			       vl[i].a[2], vl[i].a[3], vl[i].a[4], vl[i].a[5], vl[i].a[6]);
		for (i = 0; i < nlg && i < NREC; i++)
			printf("  ours   %s(%08x %08x %08x %08x %08x %08x %08x)\n", lg[i].fn, lg[i].a[0], lg[i].a[1],
			       lg[i].a[2], lg[i].a[3], lg[i].a[4], lg[i].a[5], lg[i].a[6]);
	}
}

int main(void)
{
	unsigned int c, k, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 10000;
	static const u32 edge[] = { 0, 1, 2, 3, 0xff, 0x100, 0x1ff, 0x80000000u, 0xffffffffu };

	for (k = 0; k < NENT; k++)
		for (c = 0; c < n / 10 + 9; c++) {
			u32 a0 = c < 9 ? edge[c] : (c & 1 ? rnd() : rnd() & 3);
			u32 a1 = c < 9 ? edge[8 - c] : rnd();

			if (k == 29)	/* fcrop: the vendor writes f3258 + a0 * 32 + 19 for any a0 (table has 16 channels) */
				a0 &= 0xf;	/* a0 >= 16 writes past the table: vendor UB, not compared */
			if (k == 30)	/* exprinfo: *f0904 + a0 * 220 (4 channels here) */
				a0 &= 3;
			run(k, a0, a1, rnd(), rnd());
		}
	printf("t32_apiset_oracle: %u entry points, %u cases compared, %u mismatches\n", (unsigned)NENT, compared,
	       failed);
	return failed ? 1 : 0;
}
