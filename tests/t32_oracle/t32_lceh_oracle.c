// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: LCE per-frame helpers of driver/t32/tx_isp_t32_lce.h against the
 * unmodified vendor functions (Matt Davis' recovered C, PR #15 @ 2e2e06bc):
 * tisp_lce_hist_adjust, _16bit_data_converge, _pdf_to_cdf, _hist_method, _thres_search,
 * _std_hist_transform.  None of them writes a register; the compared state is every
 * output byte the vendor changes (arrays, the excess word of std_hist_transform), the
 * untouched tail of the cdf output, and that the inputs (up/down, target, pdf, thr/par,
 * histogram) stay unchanged.  The vendor return values are not used by its callers.
 *
 * The vendor tables (t32_data_d6b50, d6c64/65/67, bf9ec, d6be4, d6b64) and the printk
 * error path data (bf994, c9660, printk) are not provided by env.c; they are defined
 * here as weak symbols with random per-case content (the code is table agnostic),
 * ours gets the same bytes through struct t32_lce_tab.  Cases ours refuses on purpose
 * (t32_lce_hist_method: parameter > 32 or negative weight index, where the vendor
 * overflows its stack / reads before the table) are counted, not compared.
 *
 * Exit 0 = equal; 1 = first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_lce.h"

__attribute__((weak)) u8 t32_data_d6b50[20];
__attribute__((weak)) u8 t32_data_d6c64[4];
__attribute__((weak)) u8 t32_data_d6c65[4];
__attribute__((weak)) u8 t32_data_d6c67[4];
__attribute__((weak)) u16 t32_data_bf9ec[32];
__attribute__((weak)) u32 t32_data_d6be4[32];
__attribute__((weak)) u32 t32_data_d6b64[32];
__attribute__((weak)) u8 t32_data_bf994[64];
__attribute__((weak)) u8 t32_data_c9660[64];
__attribute__((weak)) u64 printk(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a1; (void)a2; (void)a3;
	return 0;
}

extern u64 tisp_lce_hist_adjust(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_lce_16bit_data_converge(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4);
extern u64 tisp_lce_pdf_to_cdf(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_lce_hist_method(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_lce_thres_search(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_lce_std_hist_transform(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4);

#define CALL5(f, a, b, c, d, e) \
	t32o_call((t32o_fn)(void (*)(void))(f), (u32)(a), (u32)(b), (u32)(c), (u32)(d), (u32)(e))

static u32 rs = 0x2c1d77u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static unsigned int compared, refused;

static void fill_tab(struct t32_lce_tab *t)
{
	unsigned int i;

	for (i = 0; i < 20; i++)
		t32_data_d6b50[i] = (u8)rnd();
	t32_data_d6c64[0] = (u8)rnd();
	t32_data_d6c65[0] = (u8)rnd();
	t32_data_d6c67[0] = (u8)rnd();
	for (i = 0; i < 32; i++) {
		t32_data_bf9ec[i] = (u16)rnd();
		t32_data_d6be4[i] = rnd();
		t32_data_d6b64[i] = rnd();
	}
	memcpy(t->adj_base, t32_data_d6b50, 20);
	t->pdf_rnd = t32_data_d6c64[0];
	t->pdf_sh = t32_data_d6c65[0];
	t->pdf_shift = t32_data_d6c67[0];
	for (i = 0; i < 32; i++) {
		t->w[i] = t32_data_bf9ec[i];
		t->up[i] = t32_data_d6be4[i];
		t->down[i] = t32_data_d6b64[i];
	}
}

static int bad(const char *what)
{
	printf("MISMATCH %s\n", what);
	return 1;
}

static int case_adjust(void)
{
	struct t32_lce_tab t;
	u8 cur[20], pos[20], neg[20], oc[20], *vc, *vp, *vn;
	unsigned int i;

	fill_tab(&t);
	for (i = 0; i < 20; i++) {
		u32 b = t.adj_base[i];

		cur[i] = (rnd() % 4) ? (u8)(b + rnd() % 21 - 10) : (u8)rnd();
		pos[i] = (rnd() % 3) ? (u8)rnd() : (u8)(rnd() % 4);
		neg[i] = (rnd() % 3) ? (u8)rnd() : (u8)(rnd() % 4);
	}
	memcpy(oc, cur, 20);
	t32_lce_hist_adjust(oc, pos, neg, &t);
	t32o_reset();
	vc = t32o_alloc(32); vp = t32o_alloc(32); vn = t32o_alloc(32);
	memcpy(vc, cur, 20); memcpy(vp, pos, 20); memcpy(vn, neg, 20);
	CALL5(tisp_lce_hist_adjust, T32O_PTR(vc), T32O_PTR(vp), T32O_PTR(vn), 0, 0);
	compared++;
	if (memcmp(vp, pos, 20) || memcmp(vn, neg, 20))
		return bad("hist_adjust: vendor changed up/down");
	return memcmp(vc, oc, 20) ? bad("hist_adjust") : 0;
}

static int case_converge(void)
{
	u8 target[20], cur[20], oc[20], *vt, *vc;
	u32 gain, lo, hi;
	unsigned int i;

	gain = (rnd() % 3) ? (rnd() & 0xff) : rnd();
	lo = (rnd() % 4) ? rnd() % 24 : rnd();
	hi = (rnd() % 4) ? lo + rnd() % 30 : rnd();
	for (i = 0; i < 20; i++) {
		target[i] = (u8)rnd();
		cur[i] = (rnd() % 5) ? (u8)(target[i] + (int)(rnd() % 81) - 40) : (u8)rnd();
	}
	memcpy(oc, cur, 20);
	t32_lce_16bit_data_converge(gain, lo, hi, target, oc);
	t32o_reset();
	vt = t32o_alloc(32); vc = t32o_alloc(32);
	memcpy(vt, target, 20); memcpy(vc, cur, 20);
	CALL5(tisp_lce_16bit_data_converge, gain, lo, hi, T32O_PTR(vt), T32O_PTR(vc));
	compared++;
	if (memcmp(vt, target, 20))
		return bad("16bit_data_converge: vendor changed target");
	return memcmp(vc, oc, 20) ? bad("16bit_data_converge") : 0;
}

static int case_cdf(void)
{
	struct t32_lce_tab t;
	u16 pdf[32];
	u8 oo[32], *vo, sent[32];
	u16 *vp;
	unsigned int i, m = rnd() % 6;

	fill_tab(&t);
	for (i = 0; i < 32; i++) {
		switch (m) {
		case 0: pdf[i] = (u16)rnd(); break;
		case 1: pdf[i] = (u16)(rnd() & 3); break;
		case 2: pdf[i] = 0; break;
		case 3: pdf[i] = (rnd() % 6) ? 0 : (u16)rnd(); break;
		case 4: pdf[i] = 0; break;
		default: pdf[i] = 0; break;
		}
	}
	if (m == 4) {
		pdf[rnd() % 32] = 0x8000;
		pdf[rnd() % 32] += 0x8000;
	} else if (m == 5) {
		pdf[rnd() % 32] = 1;
	}
	for (i = 0; i < 32; i++)
		sent[i] = (u8)rnd();
	memcpy(oo, sent, 32);
	t32_lce_pdf_to_cdf(pdf, oo, &t);
	t32o_reset();
	vp = t32o_alloc(64);
	vo = t32o_alloc(32);
	memcpy(vp, pdf, 64);
	memcpy(vo, sent, 32);
	CALL5(tisp_lce_pdf_to_cdf, T32O_PTR(vp), T32O_PTR(vo), 0, 0, 0);
	compared++;
	if (memcmp(vp, pdf, 64))
		return bad("pdf_to_cdf: vendor changed the pdf");
	return memcmp(vo, oo, 32) ? bad("pdf_to_cdf") : 0;
}

static int case_method(void)
{
	struct t32_lce_tab t;
	u16 h[32], oh[32], *vh;
	u8 thr[4], par[4], *vt, *vq;
	u32 lo, mid, hi, m = rnd() % 3;
	unsigned int i;

	fill_tab(&t);
	for (i = 0; i < 32; i++)
		h[i] = (m == 0) ? (u16)(rnd() % 40) : (m == 1) ? (u16)(rnd() & 0xff) : (u16)rnd();
	thr[0] = (rnd() & 1) ? (u8)(rnd() % 64) : (u8)rnd();
	lo = rnd() % 33;
	if (rnd() & 1) {
		mid = rnd() % 33;
	} else {
		mid = lo + rnd() % 7;
		if (mid > 32)
			mid = 32;
	}
	if (rnd() & 1) {
		hi = rnd() % 33;
	} else {
		hi = mid + rnd() % 7;
		if (hi > 32)
			hi = 32;
	}
	if (rnd() % 8 == 0)
		hi = 0;
	par[0] = (u8)lo; par[1] = (u8)mid; par[2] = (u8)hi; par[3] = 0;
	memcpy(oh, h, 64);
	if (t32_lce_hist_method(oh, thr, par, &t) < 0) {
		refused++;
		return 0;
	}
	t32o_reset();
	vh = t32o_alloc(80); vt = t32o_alloc(8); vq = t32o_alloc(8);
	memcpy(vh, h, 64);
	vt[0] = thr[0];
	memcpy(vq, par, 4);
	CALL5(tisp_lce_hist_method, T32O_PTR(vh), T32O_PTR(vt), T32O_PTR(vq), 0, 0);
	compared++;
	if (vt[0] != thr[0] || memcmp(vq, par, 4))
		return bad("hist_method: vendor changed thr/par");
	return memcmp(vh, oh, 64) ? bad("hist_method") : 0;
}

static int case_thres(void)
{
	u16 h[40], thr[4], *vh, *vt;
	u8 oo[4], *vo;
	unsigned int i, m = rnd() % 3;
	u32 a, b, c;

	memset(h, 0, sizeof(h));
	for (i = 0; i < 32; i++)
		h[i] = (m == 0) ? (u16)(rnd() % 4) : (m == 1) ? (u16)(rnd() % 40) : (u16)rnd();
	a = (m == 2) ? rnd() & 0xffff : rnd() % 200;
	b = (rnd() & 1) ? a + rnd() % 200 : rnd() % 400;
	c = (rnd() & 1) ? b + rnd() % 200 : rnd() % 600;
	if (m == 2) {
		b = rnd() & 0xffff;
		c = rnd() & 0xffff;
	}
	thr[0] = (u16)a; thr[1] = (u16)b; thr[2] = (u16)c; thr[3] = 0;
	oo[0] = oo[1] = oo[2] = 0xee;
	t32_lce_thres_search(h, thr, oo);
	t32o_reset();
	vh = t32o_alloc(96); vt = t32o_alloc(16); vo = t32o_alloc(8);
	memset(vh, 0, 96);
	memcpy(vh, h, 64);
	memcpy(vt, thr, 8);
	vo[0] = vo[1] = vo[2] = 0xee;
	CALL5(tisp_lce_thres_search, T32O_PTR(vh), T32O_PTR(vt), T32O_PTR(vo), 0, 0);
	compared++;
	if (memcmp(vh, h, 64) || memcmp(vt, thr, 8))
		return bad("thres_search: vendor changed hist/thr");
	return memcmp(vo, oo, 3) ? bad("thres_search") : 0;
}

static int case_std(void)
{
	u16 h[32], oh[32], over_o = 0xbeef, *vh, *vo;
	u32 mult, shift, sum = 0, m = rnd() % 5;
	unsigned int i;

	for (i = 0; i < 32; i++)
		h[i] = (m == 3) ? 1024 : (m == 1 || m == 2) ? (u16)(rnd() % 2048) : (u16)rnd();
	if (m == 3) {
		if (rnd() & 1)
			h[rnd() % 32] += (u16)((int)(rnd() % 3) - 1);
		mult = 1;
		shift = 0;
	} else if (m == 2) {
		for (i = 0; i < 32; i++)
			sum += h[i];
		if (!sum) {
			h[0] = 1;
			sum = 1;
		}
		shift = rnd() % 17;
		mult = (u32)((((unsigned long long)0x8000 + rnd() % 65 - 32) << shift) / sum);
	} else if (m == 1) {
		mult = rnd() % 4096;
		shift = rnd() % 20;
	} else {
		mult = rnd();
		shift = rnd();
	}
	memcpy(oh, h, 64);
	t32_lce_std_hist_transform(oh, mult, shift, &over_o);
	t32o_reset();
	vh = t32o_alloc(64);
	vo = t32o_alloc(4);
	memcpy(vh, h, 64);
	*vo = 0xbeef;
	CALL5(tisp_lce_std_hist_transform, 0, T32O_PTR(vh), mult, shift, T32O_PTR(vo));
	compared++;
	if (memcmp(vh, oh, 64) || *vo != over_o)
		return bad("std_hist_transform");
	return 0;
}

int main(void)
{
	unsigned int c, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 3000;
	int fail = 0;

	for (c = 0; c < n && !fail; c++) {
		fail = case_adjust();
		if (!fail) fail = case_converge();
		if (!fail) fail = case_cdf();
		if (!fail) fail = case_method();
		if (!fail) fail = case_thres();
		if (!fail) fail = case_std();
	}
	printf("t32_lceh_oracle: %u compared (%u hist_method refused by ours), %u mismatches\n",
	       compared, refused, fail ? 1u : 0u);
	return fail ? 1 : 0;
}
