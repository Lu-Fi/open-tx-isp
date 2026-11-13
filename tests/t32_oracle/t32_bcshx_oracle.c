// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: BCSH per-frame API paths.  Runs the unmodified vendor
 * functions (Matt Davis' recovered C, PR #15 @ 2e2e06bc) and our readable
 * t32_bcsh_api_* / refresh_by_csc / param_array_* / deinit from
 * driver/t32/tx_isp_t32_bcsh.h from the same initial state and compares
 * section + knot area (whole 64 KiB), ap, cp, p5, m112, state block, outputs
 * (with guard bytes), return values and the register writes (the vendor
 * API paths emit none: ref_n must be 0, ours has no write channel).
 *
 * Vendor context (t32_data_e2450[ch]): +0 sec, +4 state block (ev lo/hi, ct,
 * ct_code, ctr_ev, ctr_ct, pend, b19), +8 cp, +12 m112, +16 ap, +20 p5, +24
 * spin lock.  Knots: t32_data_dc274[ch] + 28, capacity t32_data_dc1a4[ch*28+16]
 * (set large on the vendor side: its realloc/vfree/vmalloc and the spin lock
 * calls are not compared, env.c behaviour unknown).  Our realloc branch of
 * param_array_set is covered with counting callbacks (more knots, up to 24).
 * refresh_by_csc reads the CSC state, so tisp_csc_init runs on random data.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_bcsh.h"

/* fallbacks, used only when env.c has no strong definition */
__attribute__((weak)) u8 t32_data_da940[4];
__attribute__((weak)) u64 private_vfree(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 0; }
__attribute__((weak)) u64 __private_spin_lock_irqsave(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 0; }
__attribute__((weak)) u64 private_spin_unlock_irqrestore(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 0; }

#define SECB		T32_BCSH_SEC_BYTES
#define BIGB		(SECB + 400)
#define MAXK		24
#define ARGB		(SECB + 40 * MAXK + 8)
#define MAXW		(T32_BCSH_MAX_WRITES + 16)
#define CSC_OFF		27488u
#define ALLB		0x10000

extern u64 tisp_bcsh_deinit(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_bcsh_param_array_get(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_bcsh_param_array_set(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_bcsh_api_set_brightness(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_bcsh_api_get_brightness(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_bcsh_api_set_contrast(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_bcsh_api_get_contrast(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_bcsh_api_set_saturation(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_bcsh_api_get_saturation(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_bcsh_api_set_hue(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_bcsh_api_get_hue(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_bcsh_refresh_by_csc(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_bcsh_api_set_ccm(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_bcsh_api_get_ccm(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_bcsh_api_set_offset_rgb(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_bcsh_api_get_offset_rgb(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_csc_api_get(u32 a0, u32 a1, u32 a2, u32 a3);

static u32 rs = 0x9e3779b9u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static void put32(u8 *p, u32 v)
{
	p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
}

static u32 get32(const u8 *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24;
}

static unsigned int compared, failed, reallocs, nullslots;

enum {
	CASE_SET_BRIGHT, CASE_GET_BRIGHT, CASE_SET_CONTRAST, CASE_GET_CONTRAST,
	CASE_SET_SAT, CASE_GET_SAT, CASE_SET_HUE, CASE_GET_HUE, CASE_REFRESH_CSC,
	CASE_SET_CCM, CASE_GET_CCM, CASE_SET_OFFSET, CASE_GET_OFFSET,
	CASE_PARAM_GET, CASE_PARAM_SET, CASE_DEINIT, CASE_DEINIT_NULL, CASE_COUNT
};

struct vctx {
	u32 sec, state, cp, m112, ap, p5, lock, pad;
};

struct input {
	const u8 *sec, *lut, *ap, *arg, *csc;
	u32 arglen, lutbytes, our_lb, ev_lo, ev_hi, ct, ct_code;
	u8 ctr_ev, ctr_ct, pend, b19;
};

static void gen_section(u8 *sec, u32 knots)
{
	unsigned int i;

	for (i = 0; i < SECB; i++)
		sec[i] = (i % 3) ? (u8)rnd() : (u8)(rnd() & 7);
	put32(sec, 0x0134dae2);
	sec[0x4af] = (u8)knots;
	sec[0x4b0] = (u8)(rnd() % 4 ? rnd() % 255 : rnd() % 6);
	sec[0x4b1] = (u8)(rnd() % 16 ? 0 : 1 + rnd() % 3);
	sec[0x492] = (u8)(rnd() % 3);
	sec[0x493] = (u8)(rnd() % 3);
	sec[0x494] = (u8)(rnd() % 3);
	sec[0x496] = (u8)(rnd() % 2);
	sec[0x497] = (u8)(rnd() % 2);
	sec[0x498] = (u8)(rnd() % 3);
	sec[0x499] = (u8)(rnd() % 3);
	sec[0x49d] = (u8)(rnd() % 3);
	sec[0x49e] = (u8)(rnd() % 3);
	sec[0x49f] = (u8)(rnd() % 3);
	sec[0x45a] = (u8)(rnd() % 2);
	sec[0x45b] = (u8)(rnd() % 2);
	sec[0x45c] = (u8)(rnd() % 3);
	sec[0x45d] = (u8)(rnd() % 3);
	sec[0x45f] = (u8)(rnd() % 2);
	sec[0x460] = (u8)(rnd() % 2);
	sec[0x461] = (u8)(rnd() % 3);
	sec[0x462] = (u8)(rnd() % 3);
	sec[0x465] = (u8)(rnd() % 2);
	sec[0x466] = (u8)(rnd() % 3);
	sec[0x467] = (u8)(rnd() % 3);
	sec[0x468] = (u8)(rnd() % 3);
}

static void gen_knots(u8 *lut, u32 knots, u32 ct)
{
	unsigned int k, i;

	for (k = 0; k < knots; k++) {
		u32 t = rnd() % 3 ? ct : 5000 + (rnd() % 2 ? 1 : -1) * (rnd() % 300);

		if (t < ct)
			t = ct;
		put32(lut + 40 * k, t);
		for (i = 0; i < 9; i++)
			put32(lut + 40 * k + 4 + 4 * i, (u32)((int)(rnd() % 4096) - 2048));
		ct = t + rnd() % 2500;
	}
}

static unsigned int our_frees, our_allocs;
static u8 our_pool[40 * MAXK + 16];
static void our_vfree(void *p)
{
	(void)p;
	our_frees++;
}

static void *our_vmalloc(u32 n)
{
	our_allocs++;
	memset(our_pool, 0xa5, sizeof(our_pool));
	return n <= sizeof(our_pool) ? our_pool : NULL;
}

static u8 ref_all[ALLB];

static int run_case(const char *what, int which, u32 ch, const struct input *in)
{
	u8 *base_ref, *cp_ref, *ap_ref, *p5_ref, *m112_ref, *ctx_ref, *st_ref, *arg_ref, *out_refp, *lenp;
	u8 *base_our;
	struct t32_bcsh *our_ctx, *slot;
	struct t32_front_write refw[MAXW];
	unsigned int ref_n, need = 0, realloc_exp = 0;
	int our_n = 0;
	u8 out_ref[BIGB], out_our[BIGB];
	u8 ref_ap[0x90], ref_cp[300], ref_p5[48], ref_m112[112];
	u8 csc_blob[T32_BCSH_CSC_AP_BYTES];
	u8 tail[8], lentail[4];
	u32 len_ref = 0, len_our = 0, rv = 0;
	u32 r_lo, r_hi, r_ct, r_code;
	u8 r_cev, r_cct, r_pend, r_b19;
	struct vctx *v;
	const u8 *arg = in->arg;
	int arg_ok = 1;

	memset(out_ref, 0, sizeof(out_ref));
	memset(out_our, 0, sizeof(out_our));

	/* reference side */
	t32o_reset();
	base_ref = t32o_alloc(ALLB);
	cp_ref = t32o_alloc(300);
	ap_ref = t32o_alloc(0x90);
	p5_ref = t32o_alloc(48);
	m112_ref = t32o_alloc(112);
	ctx_ref = t32o_alloc(32);
	st_ref = t32o_alloc(24);
	arg_ref = t32o_alloc(ARGB);
	out_refp = t32o_alloc(BIGB + 8);
	lenp = t32o_alloc(8);
	{
		u8 *info = t32o_alloc(64);

		if (!base_ref || !cp_ref || !ap_ref || !p5_ref || !m112_ref || !ctx_ref || !st_ref ||
		    !arg_ref || !out_refp || !lenp || !info)
			return 2;
		memset(base_ref, 0, ALLB);
		memcpy(base_ref, in->sec, SECB);
		memcpy(base_ref + SECB, in->lut, in->lutbytes);
		memset(cp_ref, 0, 300);
		memcpy(ap_ref, in->ap, 0x90);
		memset(p5_ref, 0, 48);
		memset(m112_ref, 0, 112);
		memset(ctx_ref, 0, 32);
		memset(st_ref, 0, 24);
		memset(out_refp, 0, BIGB + 8);
		memset(lenp, 0, 8);
		memset(arg_ref, 0, ARGB);
		if (arg)
			memcpy(arg_ref, arg, in->arglen);
		put32(st_ref, in->ev_lo);
		put32(st_ref + 4, in->ev_hi);
		put32(st_ref + 8, in->ct);
		put32(st_ref + 12, in->ct_code);
		st_ref[16] = in->ctr_ev;
		st_ref[17] = in->ctr_ct;
		st_ref[18] = in->pend;
		st_ref[19] = in->b19;
		v = (struct vctx *)ctx_ref;
		v->sec = T32O_PTR(base_ref);
		v->state = T32O_PTR(st_ref);
		v->cp = T32O_PTR(cp_ref);
		v->m112 = T32O_PTR(m112_ref);
		v->ap = T32O_PTR(ap_ref);
		v->p5 = T32O_PTR(p5_ref);
		memset(info, 0, 64);
		put32(info + 28, T32O_PTR(base_ref + SECB));
		put32(&t32_data_dc1a4[ch * 28 + 16], 0xffffffffu);
		t32_data_e2450[ch] = T32O_PTR(ctx_ref);
		t32_data_dc274[ch] = T32O_PTR(info);
		t32_data_dd0a4[ch] = T32O_PTR(base_ref);
	}

	if (which == CASE_REFRESH_CSC) {
		u8 *tmp = t32o_alloc(0x100);

		if (!tmp)
			return 2;
		memcpy(base_ref + CSC_OFF, in->csc, 104);
		memset(tmp, 0, 0x100);
		memcpy(tmp, in->ap, 0x90);
		t32o_call((t32o_fn)(void (*)(void))tisp_csc_init, ch, 0, 0, 0, 0);
		t32o_call((t32o_fn)(void (*)(void))tisp_csc_api_get, ch, T32O_PTR(tmp + 44), 0, 0, 0);
		memcpy(csc_blob, tmp + 44, T32_BCSH_CSC_AP_BYTES);
		arg = csc_blob;
		arg_ok = 0;
	}
	t32o_log.n = 0;

#define CALL1(fn, a1)	rv = (u32)t32o_call((t32o_fn)(void (*)(void))fn, ch, a1, 0, 0, 0)
	switch (which) {
	case CASE_SET_BRIGHT: CALL1(tisp_bcsh_api_set_brightness, T32O_PTR(arg_ref)); break;
	case CASE_GET_BRIGHT: CALL1(tisp_bcsh_api_get_brightness, T32O_PTR(out_refp)); break;
	case CASE_SET_CONTRAST: CALL1(tisp_bcsh_api_set_contrast, T32O_PTR(arg_ref)); break;
	case CASE_GET_CONTRAST: CALL1(tisp_bcsh_api_get_contrast, T32O_PTR(out_refp)); break;
	case CASE_SET_SAT: CALL1(tisp_bcsh_api_set_saturation, T32O_PTR(arg_ref)); break;
	case CASE_GET_SAT: CALL1(tisp_bcsh_api_get_saturation, T32O_PTR(out_refp)); break;
	case CASE_SET_HUE: CALL1(tisp_bcsh_api_set_hue, T32O_PTR(arg_ref)); break;
	case CASE_GET_HUE: CALL1(tisp_bcsh_api_get_hue, T32O_PTR(out_refp)); break;
	case CASE_REFRESH_CSC: CALL1(tisp_bcsh_refresh_by_csc, 0); break;
	case CASE_SET_CCM: CALL1(tisp_bcsh_api_set_ccm, T32O_PTR(arg_ref)); break;
	case CASE_GET_CCM: CALL1(tisp_bcsh_api_get_ccm, T32O_PTR(out_refp)); break;
	case CASE_SET_OFFSET: CALL1(tisp_bcsh_api_set_offset_rgb, T32O_PTR(arg_ref)); break;
	case CASE_GET_OFFSET: CALL1(tisp_bcsh_api_get_offset_rgb, T32O_PTR(out_refp)); break;
	case CASE_PARAM_GET:
		rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_bcsh_param_array_get, ch, T32O_PTR(out_refp), T32O_PTR(lenp), 0, 0);
		break;
	case CASE_PARAM_SET: CALL1(tisp_bcsh_param_array_set, T32O_PTR(arg_ref)); break;
	case CASE_DEINIT: CALL1(tisp_bcsh_deinit, 0); break;
	case CASE_DEINIT_NULL:
		t32_data_e2450[ch] = 0;
		CALL1(tisp_bcsh_deinit, 0);
		break;
	}
#undef CALL1
	ref_n = t32o_log.n;
	if (ref_n > MAXW) {
		fprintf(stderr, "ref_n %u > MAXW\n", ref_n);
		return 2;
	}
	memcpy(refw, t32o_log.w, ref_n * sizeof(refw[0]));
	memcpy(ref_all, base_ref, ALLB);
	memcpy(ref_ap, ap_ref, 0x90);
	memcpy(ref_cp, cp_ref, 300);
	memcpy(ref_p5, p5_ref, 48);
	memcpy(ref_m112, m112_ref, 112);
	memcpy(out_ref, out_refp, BIGB);
	memcpy(tail, out_refp + BIGB, 8);
	memcpy(lentail, lenp + 4, 4);
	len_ref = get32(lenp);
	r_lo = get32(st_ref);
	r_hi = get32(st_ref + 4);
	r_ct = get32(st_ref + 8);
	r_code = get32(st_ref + 12);
	r_cev = st_ref[16];
	r_cct = st_ref[17];
	r_pend = st_ref[18];
	r_b19 = st_ref[19];
	if (arg && arg_ok && which != CASE_DEINIT && which != CASE_DEINIT_NULL &&
	    memcmp(arg_ref, arg, in->arglen))
		goto bad;	/* the vendor must not modify its input */
	if (which == CASE_DEINIT) {
		/* the vendor clears the five pointers of the context and the slot */
		if (v->state || v->cp || v->m112 || v->ap || v->p5 || t32_data_e2450[ch])
			goto bad;
	}
	if (which == CASE_DEINIT_NULL && t32_data_e2450[ch])
		goto bad;

	/* our side */
	t32o_reset();
	base_our = t32o_alloc(ALLB);
	our_ctx = t32o_alloc(sizeof(*our_ctx));
	if (!base_our || !our_ctx)
		return 2;
	memset(base_our, 0, ALLB);
	memcpy(base_our, in->sec, SECB);
	memcpy(base_our + SECB, in->lut, in->lutbytes);
	memset(our_ctx, 0, sizeof(*our_ctx));
	our_ctx->sec = base_our;
	our_ctx->lut = base_our + SECB;
	our_ctx->lutbytes = which == CASE_PARAM_SET ? in->our_lb : 400;
	memcpy(our_ctx->ap, in->ap, 0x90);
	our_ctx->ev_lo = in->ev_lo;
	our_ctx->ev_hi = in->ev_hi;
	our_ctx->ct = in->ct;
	our_ctx->ct_code = in->ct_code;
	our_ctx->ctr_ev = in->ctr_ev;
	our_ctx->ctr_ct = in->ctr_ct;
	our_ctx->pend = in->pend;
	our_ctx->b19 = in->b19;
	slot = which == CASE_DEINIT_NULL ? NULL : our_ctx;
	our_frees = 0;
	our_allocs = 0;

	switch (which) {
	case CASE_SET_BRIGHT: t32_bcsh_api_set_brightness(our_ctx, arg[0]); break;
	case CASE_GET_BRIGHT: out_our[0] = t32_bcsh_api_get_brightness(our_ctx); break;
	case CASE_SET_CONTRAST: t32_bcsh_api_set_contrast(our_ctx, arg[0]); break;
	case CASE_GET_CONTRAST: out_our[0] = t32_bcsh_api_get_contrast(our_ctx); break;
	case CASE_SET_SAT: t32_bcsh_api_set_saturation(our_ctx, arg[0]); break;
	case CASE_GET_SAT: out_our[0] = t32_bcsh_api_get_saturation(our_ctx); break;
	case CASE_SET_HUE: t32_bcsh_api_set_hue(our_ctx, arg[0]); break;
	case CASE_GET_HUE: out_our[0] = t32_bcsh_api_get_hue(our_ctx); break;
	case CASE_REFRESH_CSC: t32_bcsh_refresh_by_csc(our_ctx, arg); break;
	case CASE_SET_CCM: t32_bcsh_api_set_ccm(our_ctx, arg); break;
	case CASE_GET_CCM: t32_bcsh_api_get_ccm(our_ctx, out_our); break;
	case CASE_SET_OFFSET: t32_bcsh_api_set_offset_rgb(our_ctx, arg); break;
	case CASE_GET_OFFSET: t32_bcsh_api_get_offset_rgb(our_ctx, out_our); break;
	case CASE_PARAM_GET: t32_bcsh_param_array_get(our_ctx, out_our, &len_our); break;
	case CASE_PARAM_SET:
		need = (unsigned int)arg[1199] * 40U;
		realloc_exp = our_ctx->lutbytes < need;
		t32_bcsh_param_array_set(our_ctx, arg, our_vmalloc, our_vfree);
		break;
	case CASE_DEINIT:
		t32_bcsh_deinit(our_vfree, &slot);
		if (slot || our_frees != 1)
			goto bad;
		break;
	case CASE_DEINIT_NULL:
		t32_bcsh_deinit(our_vfree, &slot);
		if (slot || our_frees != 0)
			goto bad;
		nullslots++;
		break;
	}

	compared++;
	if (rv)
		goto bad;			/* every vendor function here returns 0 */
	if (ref_n != 0 || our_n != 0)
		goto bad;			/* no register writes on these paths, either side */
	if (which == CASE_PARAM_SET) {
		if (our_frees != realloc_exp || our_allocs != realloc_exp)
			goto bad;
		reallocs += realloc_exp;
		if (our_ctx->lutbytes < need || !our_ctx->lut)
			goto bad;
	} else if (which != CASE_DEINIT && (our_frees || our_allocs)) {
		goto bad;
	}
	{
		unsigned int tailoff = SECB;

		if (memcmp(ref_all, base_our, SECB))
			goto bad;
		if (which == CASE_PARAM_SET) {
			need = (unsigned int)base_our[1199] * 40U;
			if (memcmp(ref_all + SECB, our_ctx->lut, need))
				goto bad;
			tailoff = SECB + need;
		}
		if (memcmp(ref_all + tailoff, base_our + tailoff,
			   (which == CASE_REFRESH_CSC ? BIGB : ALLB) - tailoff))
			goto bad;
	}
	if (memcmp(ref_ap, our_ctx->ap, 0x90) && which != CASE_DEINIT)
		goto bad;
	if (which != CASE_DEINIT) {
		if (memcmp(ref_cp, our_ctx->cp, 300) || memcmp(ref_p5, our_ctx->p5, 48))
			goto bad;
		if (memcmp(ref_m112, our_ctx->m112, 112))
			goto bad;
	}
	if (r_lo != our_ctx->ev_lo || r_hi != our_ctx->ev_hi || r_ct != our_ctx->ct || r_code != our_ctx->ct_code)
		goto bad;
	if (r_cev != our_ctx->ctr_ev || r_cct != our_ctx->ctr_ct || r_pend != our_ctx->pend || r_b19 != our_ctx->b19)
		goto bad;
	if (memcmp(out_ref, out_our, sizeof(out_ref)) || len_ref != len_our)
		goto bad;
	{
		static const u8 zero[8];

		if (memcmp(tail, zero, 8) || memcmp(lentail, zero, 4))
			goto bad;	/* guard bytes behind the outputs */
	}
	return 0;
bad:
	if (failed++ < 5)
		printf("MISMATCH %s: ch %u writes ref %u rv %u\n", what, ch, ref_n, rv);
	return 1;
}

int main(void)
{
	static const char *const names[CASE_COUNT] = {
		"set-bright", "get-bright", "set-contrast", "get-contrast", "set-sat", "get-sat",
		"set-hue", "get-hue", "refresh-csc", "set-ccm", "get-ccm", "set-offset", "get-offset",
		"param-get", "param-set", "deinit", "deinit-null"
	};
	const unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 3000;
	unsigned int c;

	for (c = 0; c < n; c++) {
		u8 sec[SECB], lut[400], ap[0x90], arg[64], csc[104];
		u8 sec2[SECB], lut2[40 * MAXK], pbuf[SECB + 40 * MAXK];
		struct input in;
		u32 knots = 1 + rnd() % 10, knots2 = 1 + rnd() % MAXK;
		u32 ct = 1500 + rnd() % 2500;
		u32 ch = rnd() & 3;
		unsigned int i, w;

		memset(lut, 0, sizeof(lut));
		memset(lut2, 0, sizeof(lut2));
		gen_section(sec, knots);
		gen_knots(lut, knots, ct);
		gen_section(sec2, knots2);
		gen_knots(lut2, knots2, ct);
		memcpy(pbuf, sec2, SECB);
		memcpy(pbuf + SECB, lut2, sizeof(lut2));
		for (i = 0; i < 0x90; i++)
			ap[i] = (u8)rnd();
		for (i = 0; i < 64; i++)
			arg[i] = (u8)rnd();
		for (i = 0; i < 104; i++)
			csc[i] = (u8)rnd();
		csc[0x5b] = (u8)(rnd() % 6);
		csc[0x62] = (u8)(rnd() % 5 ? (rnd() % 2) : rnd());
		csc[0x63] = (u8)(rnd() % 3);

		in.sec = sec;
		in.lut = lut;
		in.lutbytes = knots * 40;
		in.our_lb = rnd() % 2 ? 400 : knots * 40;
		in.ap = ap;
		in.csc = csc;
		in.ev_lo = rnd() % 4 ? rnd() : 0;
		in.ev_hi = rnd() % 8 ? rnd() % 300 : 256;
		in.ct = ct;
		in.ct_code = rnd() % 20;
		in.ctr_ev = (u8)rnd();
		in.ctr_ct = (u8)rnd();
		in.pend = (u8)(rnd() % 17);
		in.b19 = (u8)(rnd() % 2);
		for (w = 0; w < CASE_COUNT; w++) {
			in.arg = w == CASE_PARAM_SET ? pbuf : arg;
			in.arglen = w == CASE_PARAM_SET ? SECB + knots2 * 40 : 64;
			run_case(names[w], (int)w, w == CASE_REFRESH_CSC ? 0 : ch, &in);
		}
	}

	printf("t32_bcshx_oracle: random %u compared, %u mismatches (%u realloc, %u null slot)\n",
	       compared, failed, reallocs, nullslots);
	if (!reallocs || !nullslots) {
		printf("t32_bcshx_oracle: branch not reached\n");
		return 1;
	}
	return failed ? 1 : 0;
}
