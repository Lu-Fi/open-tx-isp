// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: LSC per-frame paths.  Runs the unmodified vendor functions
 * tisp_lsc_dn_params_refresh / tisp_lsc_wdr_en / tisp_lsc_wake_up (Matt Davis' recovered
 * C, PR #15 @ 2e2e06bc) and our t32_lsc_dn_params_refresh / _wdr_en / _wake_up from
 * driver/t32/tx_isp_t32_lsc.h from the same state (after tisp_lsc_init, as in
 * t32_lsc_oracle) with random colour temperature, gains, wdr flag and random section
 * bytes.  Compared after every call: the shadow image (enable, 13 control registers,
 * port select, mesh words), the direct register log (shadow mode: none), the section
 * pointer of the vendor context, the context values (ct, gains, wdr) and the calls of
 * tisp_single_lsc_rcfg_cfg_trig (when this oracle's weak stub is the live one).
 * wake_up is run with the image entries 2, 7, 14, 15 poisoned.
 *
 * Vendor context (t32_data_e17e0 of env.c): ctx = e17e0[ch], ctx+0 = section pointer,
 * ctx+4 -> values (+8 ct, +20 tgain, +24 tgain_s, +28 wdr).  The context is read back
 * after tisp_lsc_init (which may allocate it).  The direct-register mode
 * (t32_data_d03a0 == 1) is not compared: d03a0 stays 0.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_iq.h"
#include "tx_isp_t32_lsc.h"

extern u32 t32_data_e17e0[4];
extern u64 tisp_lsc_dn_params_refresh(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_lsc_wdr_en(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_lsc_wake_up(u32 a0, u32 a1, u32 a2, u32 a3);

static unsigned int ref_trig_n, ref_trig_a0, our_trig_n, our_trig_ch;
static int trig_live, probed;
static unsigned int trig_logs;	/* direct writes an env.c cfg_trig adds */

/* weak: an env.c definition wins */
__attribute__((weak)) u64 tisp_single_lsc_rcfg_cfg_trig(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	ref_trig_n++;
	ref_trig_a0 = a0;
	return 0;
}

static void our_trig(struct t32_lsc_ctx *c, u8 ch)
{
	(void)c;
	our_trig_n++;
	our_trig_ch = ch;
}

static u32 rs = 0x51c0de1u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static unsigned int compared, refused, failed;

#define N1	(3 * 3464)
#define N2	(3 * 13826)
#define BLKB	(120 + N1 + N2 + 16)

static u8 ourbase[26820 + BLKB];
static struct t32_lsc_ctx oc;
static struct t32_lsc_cur ocur;
static u32 oimg[4096];
static struct t32_lsc_dwr odlog[16];

static void gen_blk(u8 *blk, unsigned int c)
{
	unsigned int i, k;
	u32 tg;

	for (i = 0; i < 120; i++)
		blk[i] = (u8)(c % 3 ? rnd() : rnd() & 7);
	blk[0] = (u8)T32_LSC_ID; blk[1] = (u8)(T32_LSC_ID >> 8);
	blk[2] = (u8)(T32_LSC_ID >> 16); blk[3] = (u8)(T32_LSC_ID >> 24);
	blk[65] = (u8)(rnd() & 1);
	blk[66] = (u8)(32 + rnd() % 96);
	blk[67] = (u8)(32 + rnd() % 96);
	for (k = 0; k < 2; k++) {
		u32 v = (u32)((int)(rnd() % 4000) - 2000);

		for (i = 0; i < 4; i++)
			blk[16 + 4 * k + i] = (u8)(v >> (8 * i));
	}
	blk[4] = blk[5] = blk[6] = blk[7] = (u8)(rnd() % 5 ? 0 : rnd());
	for (i = 120; i < 120 + N1 + N2; i++)
		blk[i] = (u8)rnd();
	blk[0x74] = 3;
	blk[0x75] = 3;
	blk[116] = (u8)(1 + rnd() % 3);
	{
		u32 th = rnd() % 3 ? rnd() % 300 : rnd() % 3000;

		for (i = 0; i < 4; i++)
			blk[12 + i] = (u8)(th >> (8 * i));
	}
	{
		u32 hy = rnd() % 3 ? rnd() % 400 : 0;

		blk[48] = (u8)hy; blk[49] = (u8)(hy >> 8); blk[50] = blk[51] = 0;
	}
	tg = 1500 + rnd() % 3000;
	for (k = 0; k < 3; k++) {
		blk[T32_LSC_TABLES + k * T32_LSC_TABLE_STRIDE] = (u8)tg;
		blk[T32_LSC_TABLES + k * T32_LSC_TABLE_STRIDE + 1] = (u8)(tg >> 8);
		tg += rnd() % 4 ? 300 + rnd() % 2500 : rnd() % 50;
	}
	if (rnd() % 5 == 0) {
		u32 tr = 1000 + rnd() % 7000;
		unsigned int k3 = rnd() % 3;

		blk[T32_LSC_TABLES + k3 * T32_LSC_TABLE_STRIDE] = (u8)tr;
		blk[T32_LSC_TABLES + k3 * T32_LSC_TABLE_STRIDE + 1] = (u8)(tr >> 8);
	}
	{
		u32 th = 0;

		for (k = 0; k < 8; k++) {
			th += rnd() % 4 ? 200 + rnd() % 600 : rnd() % 20;
			blk[32 + 2 * k] = (u8)th;
			blk[33 + 2 * k] = (u8)(th >> 8);
		}
	}
	for (i = 0; i < 24; i++)
		blk[77 + i] = (u8)(rnd() % 5 ? rnd() % 256 : 255);
}

static int run_case(const u8 *blk, u32 w_, u32 h_, u32 bayer)
{
	struct t32_lsc g;
	u8 *base, *info, *linfo, *v1, *v2, *regsh, *dbase, *ctxh, *xh;
	u32 *sh, ctxp, saved_ctx = t32_data_e17e0[0];
	int own_x = 0, bad = 0;
	unsigned int step, i, cells;
	const char *what = "";

	if (t32_lsc_geometry(blk, &g, w_, h_)) {
		refused++;
		return 0;
	}
	t32o_reset();
	base = t32o_alloc(0x10000);
	info = t32o_alloc(64);
	linfo = t32o_alloc(64);
	v1 = t32o_alloc(N1 + 8);
	v2 = t32o_alloc(N2 + 8);
	regsh = t32o_alloc(0x10000);
	dbase = t32o_alloc(0x400);
	memset(dbase, 0, 0x400);
	memset(regsh, 0, 0x10000);
	*(u32 *)(dbase + 204) = T32O_PTR(regsh);
	t32_data_da844[0] = T32O_PTR(dbase);
	memcpy(base + 26820, blk, 120);
	memcpy(v1, blk + 120, N1);
	memcpy(v2, blk + 120 + N1, N2);
	*(u32 *)(linfo + 16) = T32O_PTR(v1);
	*(u32 *)(linfo + 20) = T32O_PTR(v2);
	*(u32 *)(info) = w_;
	*(u32 *)(info + 4) = h_;
	*(u32 *)(info + 8) = bayer;
	t32_data_dd0a4[0] = T32O_PTR(base);
	t32_data_dc274[0] = T32O_PTR(linfo);
	t32_data_d03a0[0] = 0;

	/* vendor context: provide one if env.c has none yet (init may replace it) */
	if (!t32_data_e17e0[0]) {
		u8 *c = t32o_alloc(64), *x = t32o_alloc(64);

		memset(c, 0, 64);
		memset(x, 0, 64);
		*(u32 *)(c + 4) = T32O_PTR(x);
		*(u32 *)c = T32O_PTR(base) + 26820;
		t32_data_e17e0[0] = T32O_PTR(c);
	}

	t32o_call((t32o_fn)(void (*)(void))tisp_lsc_init, 0, T32O_PTR(info), 0, 0, 0);

	ctxp = t32_data_e17e0[0];
	ctxh = T32O_HOST(ctxp);
	if (!*(u32 *)(ctxh + 4)) {
		u8 *x = t32o_alloc(64);

		memset(x, 0, 64);
		*(u32 *)(ctxh + 4) = T32O_PTR(x);
		own_x = 1;
	}
	*(u32 *)ctxh = T32O_PTR(base) + 26820;
	xh = T32O_HOST(*(u32 *)(ctxh + 4));
	sh = (u32 *)regsh;

	if (!probed) {
		unsigned int n0 = t32o_log.n, t0 = ref_trig_n;

		t32o_call((t32o_fn)(void (*)(void))tisp_single_lsc_rcfg_cfg_trig, 0, 0, 0, 0, 0);
		trig_live = ref_trig_n != t0;
		trig_logs = t32o_log.n - n0;
		t32o_log.n = n0;
		ref_trig_n = 0;
		probed = 1;
	}

	/* ours: same initial state as t32_lsc_oracle */
	memcpy(ourbase + 26820, blk, 120 + N1 + N2);
	memset(&oc, 0, sizeof(oc));
	memset(&ocur, 0, sizeof(ocur));
	oc.sec = ourbase + 26820;
	oc.cur = &ocur;
	oc.img = oimg;
	oc.width = w_; oc.height = h_; oc.bayer = bayer;
	oc.direct = 0;
	oc.dlog = odlog;
	oc.cfg_trig = our_trig;
	t32_lsc_setup(oc.sec, &oc.s, &oc.wk, w_, h_, 5000);
	memcpy(oimg, sh, sizeof(oimg));
	cells = t32_lsc_cell_count(oc.sec, &oc.s, bayer);

	for (step = 0; step < 4 && !bad; step++) {
		u32 op = rnd() % 3, ct, tg, tgs, wdr = rnd() & 1, en = rnd() & 1;
		unsigned int exp_n;

		ct = rnd() % 3 ? 1000 + rnd() % 7000 : 5000 + rnd() % 600 - 300;
		tg = rnd() % 4 ? rnd() % (13u << 16) : rnd();
		tgs = rnd() % 4 ? rnd() % (13u << 16) : rnd();
		*(u32 *)(xh + 8) = ocur.ct = ct;
		*(u32 *)(xh + 20) = ocur.tgain = tg;
		*(u32 *)(xh + 24) = ocur.tgain_s = tgs;
		*(u32 *)(xh + 28) = ocur.wdr = wdr;
		if (op == 2) {
			static const u32 pi[4] = { 2, 7, 14, 15 };

			for (i = 0; i < 4; i++)
				sh[pi[i]] = oimg[pi[i]] = 0xdead0000u | pi[i];
		}
		t32o_log.n = 0;
		ref_trig_n = our_trig_n = 0;
		oc.ndlog = 0;
		if (op == 0) {
			what = "dn_params_refresh";
			t32o_call((t32o_fn)(void (*)(void))tisp_lsc_dn_params_refresh, 0, 0, 0, 0, 0);
			t32_lsc_dn_params_refresh(&oc, 0, ourbase);
		} else if (op == 1) {
			what = "wdr_en";
			t32o_call((t32o_fn)(void (*)(void))tisp_lsc_wdr_en, 0, en, 0, 0, 0);
			t32_lsc_wdr_en(&oc, 0, en);
		} else {
			what = "wake_up";
			t32o_call((t32o_fn)(void (*)(void))tisp_lsc_wake_up, 0, 0, 0, 0, 0);
			t32_lsc_wake_up(&oc, 0);
		}
		compared++;

		if (*(u32 *)ctxh != T32O_PTR(base) + 26820 ||
		    (u32)(oc.sec - ourbase) != 26820) {
			printf("MISMATCH %s: section pointer\n", what);
			bad = 1;
		}
		if (*(u32 *)(xh + 8) != ocur.ct || *(u32 *)(xh + 20) != ocur.tgain ||
		    *(u32 *)(xh + 24) != ocur.tgain_s || *(u32 *)(xh + 28) != ocur.wdr) {
			printf("MISMATCH %s: context values wdr ref %u ours %u\n", what,
			       *(u32 *)(xh + 28), ocur.wdr);
			bad = 1;
		}
		exp_n = oc.ndlog + (op == 2 && !trig_live ? trig_logs : 0);
		if (t32o_log.n != exp_n) {
			printf("MISMATCH %s: direct writes ref %u ours %u\n", what, t32o_log.n, oc.ndlog);
			bad = 1;
		} else {
			for (i = 0; i < oc.ndlog; i++)
				if (t32o_log.w[i].reg != odlog[i].reg || t32o_log.w[i].val != odlog[i].val) {
					printf("MISMATCH %s: direct write #%u\n", what, i);
					bad = 1;
				}
		}
		if (trig_live && (ref_trig_n != our_trig_n || ref_trig_n != (op == 2) ||
				  (ref_trig_n && ref_trig_a0 != our_trig_ch))) {
			printf("MISMATCH %s: cfg_trig calls ref %u ours %u\n", what, ref_trig_n, our_trig_n);
			bad = 1;
		}
		if (sh[2] != oimg[2]) {
			printf("MISMATCH %s: enable ref %x ours %x\n", what, sh[2], oimg[2]);
			bad = 1;
		}
		for (i = 4; i <= 16; i++)
			if (sh[i] != oimg[i]) {
				printf("MISMATCH %s: image[%u] ref %08x ours %08x (wdr %u op %u)\n", what, i,
				       sh[i], oimg[i], ocur.wdr, op);
				bad = 1;
			}
		if (sh[18] != oimg[18]) {
			printf("MISMATCH %s: select ref %x ours %x\n", what, sh[18], oimg[18]);
			bad = 1;
		}
		for (i = 20; i < 20 + 3 * cells; i++)
			if (sh[i] != oimg[i]) {
				printf("MISMATCH %s: mesh word %u ref %08x ours %08x (p65 %u)\n", what,
				       i - 20, sh[i], oimg[i], blk[65]);
				bad = 1;
				break;
			}
	}
	/* leave env.c's context table as we found it (no pointers into the reset pool) */
	if (own_x)
		*(u32 *)(ctxh + 4) = 0;
	t32_data_e17e0[0] = saved_ctx;
	return bad;
}

int main(void)
{
	static u8 blk[BLKB];
	static const u32 sz[4][2] = { { 1920, 1080 }, { 1280, 720 }, { 2560, 1440 }, { 640, 480 } };
	unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 3000, c;

	for (c = 0; c < n; c++) {
		u32 zi = rnd() % 4;

		gen_blk(blk, c);
		if (run_case(blk, sz[zi][0], sz[zi][1], rnd() % 8)) {
			failed++;
			break;
		}
	}
	printf("t32_lscd_oracle: %u calls compared (%u cases refused by ours), %u mismatches%s\n",
	       compared, refused, failed, trig_live ? "" : " (cfg_trig calls not compared: env.c defines it)");
	return failed ? 1 : 0;
}
