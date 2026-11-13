// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: CSCCR per-frame paths.  Runs the unmodified vendor
 * functions (Matt Davis' recovered C, PR #15 @ 2e2e06bc) and our readable
 * t32_csccr_* from driver/t32/tx_isp_t32_front.h from the same initial state
 * and compares every register write (address, value, order) and the
 * parameter section, API block, UV clip word, output buffers and freed
 * pointers.
 *
 * Vendor context t32_data_e3120[ch]: +0 param, +4 aux, +8 api.  The UV clip
 * word is t32_data_e3130[ch].  Objects the reference needs that env.c may not
 * have are defined weak here (e3120, e3130, bfef4, ca340, private_vfree,
 * tisp_day_or_night_g_ctrl).  The derived aux block is not compared directly
 * (only its register effect).  Modes are kept in 0..2 and ranges consistent
 * with the mode table where the update path reads them; for api_set the sync
 * gate byte 16 is forced to 1.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_front.h"

#define SECB	T32_CSCCR_BYTES
#define SEC_OFF	27592u
#define MAXW	32

u32 t32_data_e3120[4] __attribute__((weak));
u32 t32_data_e3130[4] __attribute__((weak));
u8 t32_data_bfef4[4] __attribute__((weak));
u8 t32_data_ca340[4] __attribute__((weak));

static u32 t32o_dn;
__attribute__((weak)) u64 tisp_day_or_night_g_ctrl(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a1; (void)a2; (void)a3;
	return t32o_dn;
}

static u32 vf_log[8];
static unsigned int vf_n;
__attribute__((weak)) u64 private_vfree(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	if (vf_n < 8)
		vf_log[vf_n] = a0;
	vf_n++;
	return 0;
}

static void *of_log[8];
static unsigned int of_n;
static void our_vfree(void *p)
{
	if (of_n < 8)
		of_log[of_n] = p;
	of_n++;
}

extern u64 tisp_csccr_switch_params_write_reg(u32, u32, u32, u32);
extern u64 tisp_csccr_sync_api_para(u32, u32, u32, u32);
extern u64 tisp_csccr_deinit(u32, u32, u32, u32);
extern u64 tisp_csccr_set_uv_clip(u32, u32, u32, u32);
extern u64 tisp_csccr_param_array_get(u32, u32, u32, u32);
extern u64 tisp_csccr_param_array_set(u32, u32, u32, u32);
extern u64 tisp_csccr_api_set(u32, u32, u32, u32);
extern u64 tisp_csccr_api_get(u32, u32, u32, u32);
extern u64 tisp_csccr_dn_params_refresh(u32, u32, u32, u32);
extern u64 tisp_csccr_update_para(u32, u32, u32, u32);
extern u64 tisp_csccr_write_reg(u32, u32, u32, u32);

/* ref/fn_..8fba0 (tisp_csccr_wake_up) does not compile with this gcc (unprototyped
 * call); this wrapper is its exact body: tail call of the vendor write_reg(a0 & 255). */
int tisp_csccr_wake_up(unsigned int a0)
{
	return (int)tisp_csccr_write_reg(a0 & 255, 0, 0, 0);
}

static u32 rs = 0x51c3a7u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

enum {
	CASE_SWITCH, CASE_SYNC, CASE_DEINIT, CASE_UV, CASE_PGET,
	CASE_PSET, CASE_ASET, CASE_AGET, CASE_WAKE, CASE_DN, CASE_UPD, CASE_N
};
static const char *const names[CASE_N] = {
	"switch", "sync", "deinit", "uv", "pget", "pset", "aset", "aget", "wake", "dn", "upd"
};

/* 8 range bytes: random, with forced equal / reversed pairs and clamping (tiny input, big output) */
static void wide_ranges(u8 *r);

struct in {
	u8 sec[SECB], buf[SECB], api0[12], aux0[16];
	u32 dir;
};

static unsigned int compared, vf_active;
static u8 base_o[SEC_OFF + 64];

static void wide_ranges(u8 *r)
{
	unsigned int i;

	for (i = 0; i < 8; i++)
		r[i] = (u8)rnd();
	switch (rnd() % 8) {
	case 0: r[1] = r[0]; break;
	case 1: r[5] = r[4]; break;
	case 2: r[3] = r[2]; break;
	case 3: r[7] = r[6]; break;
	case 4:
		r[0] = (u8)(rnd() % 50); r[1] = (u8)(r[0] + 1 + rnd() % 3);
		r[2] = 0; r[3] = (u8)(200 + rnd() % 56);
		break;
	case 5:
		r[4] = (u8)(rnd() % 50); r[5] = (u8)(r[4] + 1 + rnd() % 3);
		r[6] = 0; r[7] = (u8)(200 + rnd() % 56);
		break;
	default: break;
	}
}

static void gen(struct in *in, int which)
{
	unsigned int i;

	for (i = 0; i < SECB; i++) {
		in->sec[i] = (u8)rnd();
		in->buf[i] = (u8)rnd();
	}
	for (i = 0; i < 12; i++)
		in->api0[i] = (u8)rnd();
	for (i = 0; i < 16; i++)
		in->aux0[i] = (u8)rnd();
	in->sec[4] = (u8)(rnd() % 3);
	in->buf[4] = (u8)(rnd() % 3);
	for (i = 14; i <= 16; i++) {
		in->sec[i] = (u8)(rnd() % 3);
		in->buf[i] = (u8)(rnd() % 3);
	}
	if (which != CASE_SYNC) {
		/* update_para source (param[14]: 1 section, 2 api) unseen: keep both consistent */
		if (which == CASE_UPD) {
			wide_ranges(in->sec + 5);
			if (rnd() & 1)
				in->sec[4] = (u8)(3 + rnd() % 250);
		} else if (which == CASE_DN && !(rnd() & 3)) {
			in->sec[4] = (u8)(3 + rnd() % 250);
			wide_ranges(in->sec + 5);
		} else {
			memcpy(in->sec + 5, t32_csccr_modes[in->sec[4] % 3], 8);
		}
		in->sec[14] = (u8)(1 + rnd() % 2);
		memset(in->api0, 0, 4);
		in->api0[0] = in->sec[4];
		memcpy(in->api0 + 4, in->sec + 5, 8);
	}
	if (which == CASE_PSET) {
		/* update_para source selection is open: API block = the ranges pset ends with */
		const u8 *fin;

		if (!(rnd() & 3)) {
			in->buf[4] = (u8)(3 + rnd() % 250);
			wide_ranges(in->buf + 5);
			fin = in->buf + 5;
		} else {
			fin = t32_csccr_modes[in->buf[4] % 3];
		}
		memset(in->api0, 0, 4);
		in->api0[0] = in->buf[4];
		memcpy(in->api0 + 4, fin, 8);
	}
	if (which == CASE_ASET) {
		const u8 *fin;

		in->buf[0] = (u8)(rnd() % 3);
		in->buf[1] = in->buf[2] = in->buf[3] = 0;
		if (!(rnd() & 3)) {
			in->buf[0] = (u8)(3 + rnd() % 250);
			wide_ranges(in->buf + 4);
			fin = in->buf + 4;
		} else {
			fin = t32_csccr_modes[in->buf[0]];
		}
		in->sec[16] = 1;
		memcpy(in->sec + 5, fin, 8);
	}
	in->dir = rnd() % 3;
}

static void bad(const char *what, const char *why)
{
	printf("MISMATCH %s: %s\n", what, why);
	exit(1);
}

static void run_case(int which, u32 ch, const struct in *in, int nullctx, int nullaux, int nullapi)
{
	const char *what = names[which];
	u8 *base_r, *par_r, *aux_r, *api_r, *ctx_r, *ub_r, *out_r, *len_r;
	u8 par_o[32], aux_o[16], api_o[16], ub_o[32], out_o[32] = { 0 };
	struct t32_csccr_ctx our_ctx, *slot;
	struct t32_front_write refw[MAXW], ow[MAXW];
	unsigned int ref_n, i;
	int our_n = 0, ref_ret = 0, our_ret = 0;
	u32 dn, uv_o = 0xdeadbeef, our_len = 0;
	u8 *rp;
	u32 exp_r[3];
	void *exp_o[3];
	unsigned int ne = 0;

	t32o_dn = rnd() % 4;
	dn = (u32)tisp_day_or_night_g_ctrl(ch, 0, 0, 0);

	/* reference */
	t32o_reset();
	vf_n = 0;
	base_r = t32o_alloc(SEC_OFF + 64);
	par_r = t32o_alloc(32);
	aux_r = t32o_alloc(16);
	api_r = t32o_alloc(16);
	ctx_r = t32o_alloc(16);
	ub_r = t32o_alloc(32);
	out_r = t32o_alloc(32);
	len_r = t32o_alloc(8);
	if (!base_r || !par_r || !aux_r || !api_r || !ctx_r || !ub_r || !out_r || !len_r)
		bad(what, "alloc");
	memset(base_r, 0, SEC_OFF + 64);
	memcpy(base_r + SEC_OFF, in->sec, SECB);
	memset(par_r, 0, 32);
	memset(api_r, 0, 16);
	memcpy(par_r, in->sec, SECB);
	memcpy(aux_r, in->aux0, 16);
	memcpy(api_r, in->api0, 12);
	memcpy(ub_r, in->buf, SECB);
	memset(out_r, 0, 32);
	memset(len_r, 0, 8);
	*(u32 *)(ctx_r + 0) = T32O_PTR(par_r);
	*(u32 *)(ctx_r + 4) = nullaux ? 0 : T32O_PTR(aux_r);
	*(u32 *)(ctx_r + 8) = nullapi ? 0 : T32O_PTR(api_r);
	t32_data_e3120[ch] = nullctx ? 0 : T32O_PTR(ctx_r);
	t32_data_dd0a4[ch] = T32O_PTR(base_r);
	t32_data_e3130[ch] = 0xdeadbeef;

	switch (which) {
	case CASE_SWITCH:
		t32o_call((t32o_fn)(void (*)(void))tisp_csccr_switch_params_write_reg, ch, 0, 0, 0, 0);
		break;
	case CASE_SYNC:
		t32o_call((t32o_fn)(void (*)(void))tisp_csccr_sync_api_para, ch, in->dir, 0, 0, 0);
		break;
	case CASE_DEINIT:
		t32o_call((t32o_fn)(void (*)(void))tisp_csccr_deinit, ch, 0, 0, 0, 0);
		break;
	case CASE_UV:
		t32o_call((t32o_fn)(void (*)(void))tisp_csccr_set_uv_clip, ch, 0, 0, 0, 0);
		break;
	case CASE_PGET:
		t32o_call((t32o_fn)(void (*)(void))tisp_csccr_param_array_get, ch,
			  T32O_PTR(out_r), T32O_PTR(len_r), 0, 0);
		break;
	case CASE_PSET:
		t32o_call((t32o_fn)(void (*)(void))tisp_csccr_param_array_set, ch, T32O_PTR(ub_r), 0, 0, 0);
		break;
	case CASE_ASET:
		t32o_call((t32o_fn)(void (*)(void))tisp_csccr_api_set, ch, T32O_PTR(ub_r), 0, 0, 0);
		break;
	case CASE_AGET:
		ref_ret = (int)(u32)t32o_call((t32o_fn)(void (*)(void))tisp_csccr_api_get, ch,
					      T32O_PTR(out_r), 0, 0, 0);
		break;
	case CASE_WAKE:
		t32o_call((t32o_fn)(void (*)(void))tisp_csccr_update_para, ch, 0, 0, 0, 0);
		t32o_call((t32o_fn)(void (*)(void))tisp_csccr_wake_up, ch, 0, 0, 0, 0);
		break;
	case CASE_DN:
		t32o_call((t32o_fn)(void (*)(void))tisp_csccr_dn_params_refresh, ch, 0, 0, 0, 0);
		break;
	case CASE_UPD:
		ref_ret = (int)(u32)t32o_call((t32o_fn)(void (*)(void))tisp_csccr_update_para, ch, 0, 0, 0, 0);
		break;
	}
	ref_n = t32o_log.n;
	if (ref_n > MAXW)
		bad(what, "too many reference writes");
	memcpy(refw, t32o_log.w, ref_n * sizeof(refw[0]));

	/* ours */
	of_n = 0;
	memset(base_o, 0, sizeof(base_o));
	memcpy(base_o + SEC_OFF, in->sec, SECB);
	memset(par_o, 0, sizeof(par_o));
	memset(api_o, 0, sizeof(api_o));
	memcpy(par_o, in->sec, SECB);
	memcpy(aux_o, in->aux0, 16);
	memcpy(api_o, in->api0, 12);
	memcpy(ub_o, in->buf, SECB);
	our_ctx.param = par_o;
	our_ctx.aux = nullaux ? NULL : aux_o;
	our_ctx.api = nullapi ? NULL : api_o;
	slot = nullctx ? NULL : &our_ctx;

	switch (which) {
	case CASE_SWITCH:
		our_n = t32_csccr_switch_params_write_reg(&our_ctx, (u8)ch, ow);
		break;
	case CASE_SYNC:
		t32_csccr_sync_api_para(&our_ctx, in->dir);
		break;
	case CASE_DEINIT:
		t32_csccr_deinit(&slot, our_vfree);
		break;
	case CASE_UV: {
		unsigned int nn = 0;

		t32_csccr_set_uv_clip(dn, &uv_o, ow, &nn);
		our_n = (int)nn;
		break;
	}
	case CASE_PGET:
		t32_csccr_param_array_get(&our_ctx, out_o, &our_len);
		break;
	case CASE_PSET:
		our_n = t32_csccr_param_array_set(&our_ctx, ub_o, dn, &uv_o, ow);
		break;
	case CASE_ASET:
		our_n = t32_csccr_api_set(&our_ctx, ub_o, ow);
		break;
	case CASE_AGET:
		our_ret = t32_csccr_api_get(slot, out_o);
		break;
	case CASE_WAKE: {
		unsigned int nn = 0;

		t32_csccr_update_para(&our_ctx);
		t32_csccr_write_reg(&our_ctx, ow, &nn);
		our_n = (int)nn;
		break;
	}
	case CASE_DN:
		our_n = t32_csccr_dn_params_refresh(&our_ctx, (u8)ch, base_o, ow);
		break;
	case CASE_UPD:
		our_ret = t32_csccr_update_para(&our_ctx);
		break;
	}

	compared++;
	if (which == CASE_DEINIT) {
		if (!nullctx) {
			if (!nullaux) {
				exp_r[ne] = T32O_PTR(aux_r);
				exp_o[ne++] = aux_o;
			}
			if (!nullapi) {
				exp_r[ne] = T32O_PTR(api_r);
				exp_o[ne++] = api_o;
			}
			exp_r[ne] = T32O_PTR(ctx_r);
			exp_o[ne++] = &our_ctx;
			if (of_n != ne)
				bad(what, "our free count");
			for (i = 0; i < ne; i++)
				if (of_log[i] != exp_o[i])
					bad(what, "our free order");
			if (vf_active) {
				if (vf_n != ne)
					bad(what, "vendor free count");
				for (i = 0; i < ne; i++)
					if (vf_log[i] != exp_r[i])
						bad(what, "vendor free order");
			}
			if (*(u32 *)(ctx_r + 4) != 0 || *(u32 *)(ctx_r + 8) != 0 ||
			    our_ctx.aux || our_ctx.api)
				bad(what, "block pointers not cleared");
			if (t32_data_e3120[ch] != 0 || slot != NULL)
				bad(what, "slot not cleared");
		} else if (of_n || slot || (vf_active && vf_n) || t32_data_e3120[ch])
			bad(what, "null slot touched");
		return;
	}
	if (which == CASE_AGET) {
		if (ref_ret != our_ret)
			bad(what, "return value");
		if (memcmp(out_r, out_o, 32))
			bad(what, "output");
		return;
	}
	if (which == CASE_PGET) {
		if (*(u32 *)len_r != our_len || memcmp(out_r, out_o, 32))
			bad(what, "length/output");
		return;
	}
	if (which == CASE_UPD) {
		const u8 *m = in->sec + 5;
		int valid = !(m[1] < m[0] || m[3] < m[2] || m[5] < m[4] || m[7] < m[6]);

		if (valid && ref_ret != our_ret) {
			printf("  update_para ret vendor %d ours %d\n", ref_ret, our_ret);
			bad(what, "update_para return value");
		}
		if (!valid && our_ret != -1)
			bad(what, "our update_para failure return");
	}
	if (our_n < 0 || (unsigned int)our_n != ref_n) {
		printf("MISMATCH %s: writes ref %u ours %d\n", what, ref_n, our_n);
		exit(1);
	}
	for (i = 0; i < ref_n; i++)
		if (refw[i].reg != ow[i].reg || refw[i].val != ow[i].val) {
			printf("MISMATCH %s: #%u vendor %05x=%08x ours %05x=%08x\n", what, i,
			       refw[i].reg, refw[i].val, ow[i].reg, ow[i].val);
			printf("  state: sec[4]=%u sec[14..16]=%u,%u,%u api0[0]=%u buf[4]=%u buf[14..16]=%u,%u,%u buf[0]=%u dn=%u\n",
			       in->sec[4], in->sec[14], in->sec[15], in->sec[16], in->api0[0],
			       in->buf[4], in->buf[14], in->buf[15], in->buf[16], in->buf[0], dn);
			{
				const u8 *vp = T32O_HOST(*(u32 *)ctx_r);
				unsigned int k;

				printf("  vendor param[4..12]:");
				for (k = 4; k <= 12; k++)
					printf(" %02x", vp[k]);
				printf("\n  ours   param[4..12]:");
				for (k = 4; k <= 12; k++)
					printf(" %02x", our_ctx.param[k]);
				printf("\n  vendor api:");
				for (k = 0; k < 12; k++)
					printf(" %02x", api_r[k]);
				printf("\n");
			}
			exit(1);
		}
	rp = T32O_HOST(*(u32 *)ctx_r);
	if (which == CASE_DN) {
		if ((u32)(rp - base_r) != SEC_OFF || (u32)(our_ctx.param - base_o) != SEC_OFF)
			bad(what, "param pointer");
	}
	if (memcmp(rp, our_ctx.param, 32))
		bad(what, "parameter section");
	if (memcmp(T32O_HOST(*(u32 *)(ctx_r + 8)), our_ctx.api, 16))
		bad(what, "api block");
	{
		/* aux: only compared on the success path, and not bytes 3 / 7 (vendor
		 * layout of those and of a failed update_para is not provable here) */
		const u8 *m = rp + 5;
		static const unsigned int ab[12] = { 0, 1, 2, 4, 5, 6, 8, 9, 10, 11, 12, 13 };
		int valid = !(m[1] < m[0] || m[3] < m[2] || m[5] < m[4] || m[7] < m[6]);

		for (i = 0; valid && i < 12; i++)
			if (aux_r[ab[i]] != aux_o[ab[i]]) {
				printf("  aux[%u] vendor %02x ours %02x\n", ab[i], aux_r[ab[i]],
				       aux_o[ab[i]]);
				bad(what, "derived aux block");
			}
		if (!valid)
			for (i = 0; i < 16; i++)
				if (aux_o[i] != in->aux0[i])
					bad(what, "our aux changed on failure");
	}
	if (memcmp(base_r, base_o, sizeof(base_o)))
		bad(what, "tuning base buffer");
	if (t32_data_e3130[ch] != uv_o)
		bad(what, "uv clip word");
}

int main(void)
{
	const unsigned int n = getenv("T32O_NRAND") ?
		(unsigned int)atoi(getenv("T32O_NRAND")) : 3000;
	unsigned int c;
	int w;

	vf_n = 0;
	private_vfree(0, 0, 0, 0);
	vf_active = (vf_n == 1);
	vf_n = 0;

	for (c = 0; c < n; c++) {
		u32 ch = rnd() & 3;

		for (w = 0; w < CASE_N; w++) {
			struct in in;
			int nullctx = 0, nullaux = 0, nullapi = 0;

			gen(&in, w);
			if (w == CASE_AGET || w == CASE_DEINIT)
				nullctx = !(rnd() & 7);
			if (w == CASE_DEINIT) {
				nullaux = !(rnd() & 3);
				nullapi = !(rnd() & 3);
			}
			run_case(w, ch, &in, nullctx, nullaux, nullapi);
		}
	}
	printf("t32_csccrx_oracle: random %u compared, 0 mismatches\n", compared);
	return 0;
}
