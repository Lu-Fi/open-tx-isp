// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: LSC per-frame paths.  Runs the unmodified vendor functions
 * (Matt Davis' recovered C, PR #15 @ 2e2e06bc: tisp_lsc_itp, _tgain_update_s, _deinit,
 * _param_array_get, _param_array_set) and our readable ports from
 * driver/t32/tx_isp_t32_lsc.h from the same state and compares the result.
 *
 * State: the vendor side is built by tisp_lsc_init (as in t32_lsc_oracle); ours is seeded
 * from the vendor's section and shadow image after init.  Compared: shadow image (all
 * 20 + 3456 words), section bytes (except 48/49, the vendor lowers the hysteresis in
 * place), both payload buffers, the published table sizes (da850 + 232/234), return
 * values, the get buffer and length, the deinit free order.
 *
 * Not covered: tisp_lsc_mirror_flip / _hvflip (callees lsc_exchange_*, spin lock, printk are
 * not in env.c), the vmalloc path of param_array_set (buffer capacities are set large).
 * Exit 0 = all equal, 1 = first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_lsc.h"

#define N1	(3 * T32_LSC_RING_STRIDE)
#define N2	(3 * T32_LSC_TABLE_STRIDE)
#define BUFB	17400u
#define NIMG	(20 + T32_LSC_WORDS)

extern u32 t32_data_e17e0[4];

/*
 * env.c does not provide t32_data_c9570 (the file-name string tisp_lsc_param_array_set passes to
 * isp_printf on its allocation-error path, which the oracle never reaches); a placeholder is
 * defined here so the reference links.  No data is invented: it is never read.
 */
unsigned char t32_data_c9570[64];
unsigned char t32_data_bf4e8[64];	/* likewise: the format string of that isp_printf call */

extern u64 tisp_lsc_itp(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_lsc_tgain_update_s(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_lsc_deinit(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_lsc_param_array_get(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_lsc_param_array_set(u32 a0, u32 a1, u32 a2, u32 a3);

static u32 rs = 0x5c3a91u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static void put32(u8 *p, u32 v)
{
	p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
}

static unsigned int compared, refused;

#define FAIL(...) do { printf("MISMATCH " __VA_ARGS__); printf("\n"); \
		       printf("t32_lscx_oracle: %u compared, 1 mismatch\n", compared); exit(1); } while (0)

static void gen_sec(u8 *s)
{
	unsigned int i;
	u32 th = 0;

	for (i = 0; i < 120; i++)
		s[i] = (u8)(i % 3 ? rnd() : rnd() & 7);
	put32(s, T32_LSC_ID);
	s[4] = s[5] = s[6] = s[7] = (u8)(rnd() % 5 ? 0 : rnd());
	put32(s + 12, rnd() % 3 ? rnd() % 300 : rnd() % 3000);
	put32(s + 16, (u32)((int)(rnd() % 4000) - 2000));
	put32(s + 20, (u32)((int)(rnd() % 4000) - 2000));
	for (i = 0; i < 8; i++) {
		th += rnd() % 4 ? 200 + rnd() % 600 : rnd() % 20;
		s[32 + 2 * i] = (u8)th;
		s[33 + 2 * i] = (u8)(th >> 8);
	}
	put32(s + 48, rnd() % 3 ? rnd() % 400 : 0);
	s[50] = s[51] = 0;
	for (i = 0; i < 24; i++)
		s[77 + i] = (u8)(rnd() % 5 ? rnd() % 256 : 255);
	s[65] = (u8)(rnd() & 1);
	s[66] = (u8)(32 + rnd() % 96);
	s[67] = (u8)(32 + rnd() % 96);
	s[116] = (u8)(1 + rnd() % 3);
}

static void gen_blk(u8 *b)
{
	unsigned int i, k;
	u32 tg = 1500 + rnd() % 3000;

	gen_sec(b);
	for (i = 120; i < T32_LSC_BLOCK_BYTES; i++)
		b[i] = (u8)rnd();
	for (k = 0; k < 3; k++) {
		b[T32_LSC_TABLES + k * T32_LSC_TABLE_STRIDE] = (u8)tg;
		b[T32_LSC_TABLES + k * T32_LSC_TABLE_STRIDE + 1] = (u8)(tg >> 8);
		tg += rnd() % 4 ? 300 + rnd() % 2500 : rnd() % 50;
	}
}

/* ---- tisp_lsc_itp ---- */
static void case_itp(void)
{
	u32 a = rnd(), b = rnd(), f = rnd();
	u32 ref = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_lsc_itp, a, b, f, 0, 0);
	u32 our = t32_lsc_itp(a, b, f);

	compared++;
	if (ref != our)
		FAIL("itp a %08x b %08x f %08x vendor %08x ours %08x", a, b, f, ref, our);
}

/* ---- vendor + our state after init ---- */
static struct {
	u8 *base, *ctx, *cur, *regsh, *v1, *v2, *linfo;
} V;
static u8 oblk[T32_LSC_BLOCK_BYTES + 16];
static struct t32_lsc_ctx oc;
static struct t32_lsc_cur ocur;
static struct t32_lsc_lut olut;
static u32 oimg[4096];

/* 0 = ready, 1 = refused (ours cannot do the grid) */
static int setup(const u8 *blk, u32 w, u32 h, u32 bayer)
{
	struct t32_lsc g;
	u8 *info, *dbase, *vsec;
	u32 cap[2] = { N1, N2 };

	if (t32_lsc_geometry(blk, &g, w, h)) {
		refused++;
		return 1;
	}
	t32o_reset();
	V.base = t32o_alloc(0x8000);
	info = t32o_alloc(64);
	V.linfo = t32o_alloc(64);
	V.v1 = t32o_alloc(N1 + 8);
	V.v2 = t32o_alloc(N2 + 8);
	V.regsh = t32o_alloc(0x4000);
	dbase = t32o_alloc(0x400);
	if (!V.base || !info || !V.linfo || !V.v1 || !V.v2 || !V.regsh || !dbase) {
		fprintf(stderr, "t32_lscx_oracle: pool exhausted\n");
		exit(2);
	}
	put32(dbase + 204, T32O_PTR(V.regsh));
	t32_data_da844[0] = T32O_PTR(dbase);
	t32_data_d03a0[0] = 0;
	memcpy(V.base + T32_LSC_TUNING_OFF, blk, 120);
	memcpy(V.v1, blk + 120, N1);
	memcpy(V.v2, blk + T32_LSC_TABLES, N2);
	put32(V.linfo + 16, T32O_PTR(V.v1));
	put32(V.linfo + 20, T32O_PTR(V.v2));
	put32(info, w);
	put32(info + 4, h);
	put32(info + 8, bayer);
	t32_data_dd0a4[0] = T32O_PTR(V.base);
	t32_data_dc274[0] = T32O_PTR(V.linfo);
	t32o_call((t32o_fn)(void (*)(void))tisp_lsc_init, 0, T32O_PTR(info), 0, 0, 0);
	if (!t32_data_e17e0[0]) {
		fprintf(stderr, "t32_lscx_oracle: tisp_lsc_init left no context\n");
		exit(2);
	}
	V.ctx = T32O_HOST(t32_data_e17e0[0]);
	V.cur = T32O_HOST(t32_le32(V.ctx + 4));
	vsec = T32O_HOST(t32_le32(V.ctx));
	memcpy(t32_data_dc1a4, cap, 8);

	/* ours: same section (hysteresis possibly lowered), same payloads, same image */
	memset(&oc, 0, sizeof(oc));
	memset(&ocur, 0, sizeof(ocur));
	memcpy(oblk, vsec, 120);
	memcpy(oblk + 120, blk + 120, T32_LSC_BLOCK_BYTES - 120);
	if (t32_lsc_setup(oblk, &oc.s, &oc.wk, w, h, 5000)) {
		fprintf(stderr, "t32_lscx_oracle: our setup failed\n");
		exit(2);
	}
	memcpy(oimg, V.regsh, sizeof(oimg));
	olut.p1 = oblk + 120;
	olut.p2 = oblk + T32_LSC_TABLES;
	olut.size[0] = olut.size[1] = 0;
	oc.sec = oblk;
	oc.cur = &ocur;
	oc.img = oimg;
	oc.width = w;
	oc.height = h;
	oc.bayer = bayer;
	oc.lut = &olut;
	oc.cap[0] = N1;
	oc.cap[1] = N2;
	return 0;
}

static u32 g_idx, g_old65, g_ct;

static void cmp_image(const char *what)
{
	const u32 *sh = (const u32 *)V.regsh;
	unsigned int i;

	for (i = 0; i < NIMG; i++)
		if (sh[i] != oimg[i])
			FAIL("%s image[%u] vendor %08x ours %08x (idx %u p65 old %u new %u ntab %u ct %u none %u tables %u/%u frac %u cells %u)",
			     what, i, sh[i], oimg[i], g_idx, g_old65, oblk[65], oblk[116], g_ct, oc.s.none,
			     oc.s.table_lo, oc.s.table_hi, oc.s.fraction, oc.s.cells);
}

static const u32 sz[4][2] = { { 1920, 1080 }, { 1280, 720 }, { 2560, 1440 }, { 640, 480 } };

/* ---- tisp_lsc_tgain_update_s ---- */
static void case_tgain_s(const u8 *blk, u32 gain, u32 w, u32 h, u32 bayer)
{
	if (setup(blk, w, h, bayer))
		return;
	t32o_call((t32o_fn)(void (*)(void))tisp_lsc_tgain_update_s, 0, gain, 0, 0, 0);
	t32_lsc_tgain_update_s(&oc, 0, gain);
	compared++;
	cmp_image("tgain_s");
}

/* ---- tisp_lsc_param_array_set ---- */
static void *no_vm(u32 n) { (void)n; return NULL; }
static void no_free(void *p) { (void)p; }

static void case_set(const u8 *blk, const u8 *buf, u32 idx, u32 w, u32 h, u32 bayer)
{
	struct t32_lsc g;
	u8 *vbuf, *vsec;
	u32 ct, tg, tgs, vr, i;
	int orv;

	if (!idx && t32_lsc_geometry(buf, &g, w, h)) {
		refused++;
		return;
	}
	if (setup(blk, w, h, bayer))
		return;
	vbuf = t32o_alloc(BUFB);
	if (!vbuf) {
		fprintf(stderr, "t32_lscx_oracle: pool exhausted\n");
		exit(2);
	}
	memcpy(vbuf, buf, BUFB);
	ct = rnd() % 3 ? 1000 + rnd() % 7000 : 5000;
	tg = rnd() % 0x130000;
	tgs = rnd() % 0x130000;
	put32(V.cur + 8, ct);
	put32(V.cur + 20, tg);
	put32(V.cur + 24, tgs);
	g_idx = idx;
	g_old65 = blk[65];
	g_ct = ct;
	ocur.ct = ct;
	ocur.tgain = tg;
	ocur.tgain_s = tgs;

	vr = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_lsc_param_array_set, 0, idx, T32O_PTR(vbuf), 0, 0);
	orv = t32_lsc_param_array_set(&oc, 0, idx, buf, no_vm, no_free);
	compared++;
	if (vr != (u32)orv)
		FAIL("param_set idx %u return vendor %d ours %d", idx, (int)vr, orv);
	cmp_image("param_set");
	vsec = T32O_HOST(t32_le32(V.ctx));
	for (i = 0; i < 120; i++)
		if (i != 48 && i != 49 && vsec[i] != oblk[i])
			FAIL("param_set idx %u section[%u] vendor %02x ours %02x", idx, i, vsec[i], oblk[i]);
	if (memcmp(V.v1, oblk + 120, N1))
		FAIL("param_set idx %u payload 1 differs", idx);
	if (memcmp(V.v2, oblk + T32_LSC_TABLES, N2))
		FAIL("param_set idx %u payload 2 differs", idx);
	if (t32_le16(t32_data_da850 + 232) != oc.pub[0] || t32_le16(t32_data_da850 + 234) != oc.pub[1])
		FAIL("param_set idx %u published sizes vendor %u/%u ours %u/%u", idx,
		     t32_le16(t32_data_da850 + 232), t32_le16(t32_data_da850 + 234), oc.pub[0], oc.pub[1]);
}

/* ---- tisp_lsc_param_array_get ---- */
static void case_get(const u8 *sec, const u8 *tail)
{
	u8 *vctx, *vsec, *vtail, *vbuf, *vlen;
	static u8 obuf[256];
	u32 olen = 0, i;

	t32o_reset();
	vctx = t32o_alloc(32);
	vsec = t32o_alloc(128);
	vtail = t32o_alloc(16);
	vbuf = t32o_alloc(256);
	vlen = t32o_alloc(8);
	if (!vctx || !vsec || !vtail || !vbuf || !vlen)
		exit(2);
	memcpy(vsec, sec, 120);
	memcpy(vtail, tail, 12);
	memset(vbuf, 0xaa, 256);
	put32(vlen, 0);
	put32(vctx, T32O_PTR(vsec));
	put32(vctx + 12, T32O_PTR(vtail));
	t32_data_e17e0[0] = T32O_PTR(vctx);
	t32o_call((t32o_fn)(void (*)(void))tisp_lsc_param_array_get, 0, T32O_PTR(vbuf), T32O_PTR(vlen), 0, 0);

	memset(&oc, 0, sizeof(oc));
	oc.sec = vsec;
	oc.stats = vtail;
	memset(obuf, 0xaa, sizeof(obuf));
	t32_lsc_param_array_get(&oc, obuf, &olen);
	compared++;
	if (t32_le32(vlen) != olen)
		FAIL("param_get length vendor %u ours %u", t32_le32(vlen), olen);
	for (i = 0; i < 256; i++)
		if (vbuf[i] != obuf[i])
			FAIL("param_get buf[%u] vendor %02x ours %02x", i, vbuf[i], obuf[i]);
}

/* ---- tisp_lsc_deinit ---- */
static void *freed[8];
static unsigned int nfreed;
static void rec_free(void *p)
{
	if (nfreed < 8)
		freed[nfreed++] = p;
}

static void case_deinit(int have_ctx, int cur, int info, int stats)
{
	u8 *vctx, *a = NULL, *b = NULL, *c = NULL;
	void *exp[4];
	unsigned int ne = 0, i;
	struct t32_lsc_ctx *slot;

	t32o_reset();
	vctx = t32o_alloc(32);
	if (!vctx)
		exit(2);
	if (cur)
		a = t32o_alloc(16);
	if (info)
		b = t32o_alloc(16);
	if (stats)
		c = t32o_alloc(16);
	if (have_ctx) {
		put32(vctx + 4, T32O_PTR(a));
		put32(vctx + 8, T32O_PTR(b));
		put32(vctx + 12, T32O_PTR(c));
		t32_data_e17e0[0] = T32O_PTR(vctx);
	} else {
		t32_data_e17e0[0] = 0;
	}
	t32o_call((t32o_fn)(void (*)(void))tisp_lsc_deinit, 0, 0, 0, 0, 0);

	memset(&oc, 0, sizeof(oc));
	nfreed = 0;
	if (have_ctx) {
		oc.cur = (struct t32_lsc_cur *)a;
		oc.info = b;
		oc.stats = c;
		if (a)
			exp[ne++] = a;
		if (b)
			exp[ne++] = b;
		if (c)
			exp[ne++] = c;
		exp[ne++] = &oc;
		slot = &oc;
	} else {
		slot = NULL;
	}
	t32_lsc_deinit(rec_free, &slot);
	compared++;
	if (t32_data_e17e0[0])
		FAIL("deinit vendor slot not cleared (ctx %d)", have_ctx);
	if (slot)
		FAIL("deinit our slot not cleared (ctx %d)", have_ctx);
	if (nfreed != ne)
		FAIL("deinit ctx %d cur %d info %d stats %d: ours freed %u, expected %u", have_ctx, cur, info, stats,
		     nfreed, ne);
	for (i = 0; i < ne; i++)
		if (freed[i] != exp[i])
			FAIL("deinit free #%u pointer differs", i);
	if (have_ctx && (oc.cur || oc.info || oc.stats))
		FAIL("deinit our fields not cleared");
}

int main(void)
{
	const unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 3000;
	static u8 blk[T32_LSC_BLOCK_BYTES + 16], buf[BUFB];
	static const u32 idxs[] = { 0, 1, 2, 3, 3, 1, 17, 255, 0, 2 };
	unsigned int c;

	for (c = 0; c < n; c++) {
		u8 tail[12];
		u32 zi = rnd() % 4, bayer = rnd() % 8, idx = idxs[rnd() % 10], gain = rnd() % 3 ? rnd() : rnd() % 0x130000;
		unsigned int i;

		gen_blk(blk);
		for (i = 0; i < BUFB; i++)
			buf[i] = (u8)rnd();
		gen_sec(buf);
		/* the replacement mesh table (payload 2 starts at buf + 3464) carries a plausible tag:
		 * a tag below the hysteresis wraps in the unsigned tag - h of t32_lsc_ct, which is not
		 * a tuning value (the vendor's signedness there is not covered by this oracle) */
		{
			u32 tg = 1500 + rnd() % 3000;

			buf[T32_LSC_RING_STRIDE] = (u8)tg;
			buf[T32_LSC_RING_STRIDE + 1] = (u8)(tg >> 8);
		}
		for (i = 0; i < 12; i++)
			tail[i] = (u8)rnd();

		case_itp();
		case_tgain_s(blk, gain, sz[zi][0], sz[zi][1], bayer);
		case_get(blk, tail);
		case_set(blk, buf, idx, sz[zi][0], sz[zi][1], bayer);
		/* the table of the section's own count, so that the refresh path runs */
		case_set(blk, buf, blk[116], sz[zi][0], sz[zi][1], bayer);
		case_deinit(rnd() % 4 != 0, rnd() & 1, rnd() & 1, rnd() & 1);
	}
	case_itp();
	case_deinit(0, 0, 0, 0);
	case_deinit(1, 0, 0, 0);
	case_deinit(1, 1, 1, 1);
	case_deinit(1, 0, 1, 0);
	case_deinit(1, 0, 0, 1);

	printf("t32_lscx_oracle: random %u compared (%u refused), 0 mismatches\n", compared, refused);
	return 0;
}
