// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: LSC per-frame paths.  Runs the unmodified vendor functions
 * (Matt Davis' recovered C, PR #15 @ 2e2e06bc: tisp_lsc_itp, _tgain_update_s, _deinit,
 * _param_array_get, _param_array_set) and our readable ports from
 * driver/t32/tx_isp_t32_lsc.h from the same state and compares everything.
 *
 * State: the vendor side is built by tisp_lsc_init (as in t32_lsc_oracle); ours is seeded
 * from the vendor's section, current-values block and payloads.  The shadow image (4096
 * words) is canary filled on both sides before the call and compared whole, so the set of
 * written registers is proven (the order is not observable: every index is written once and
 * env.c's shadow writer keeps no log).  Compared: image, section (all 120 bytes), info
 * struct (geometry, strengths, flags), current values (32 bytes), both payload buffers with
 * guard bytes, the input buffer (read only), caps (dc1a4), table sizes (dc274), the pointers
 * (NULL-ness / reallocation), published sizes (da850 + 232/234, sentinels), return values,
 * the get buffer (1 KiB canary) and length word, deinit: the free order of ours plus the
 * vendor's cleared fields.
 *
 * param_array_set allocation: caps are varied (below / at / above the need).  Ours gets a
 * vmalloc callback that mirrors the vendor's real result (buffer contents, NULL), since the
 * port reads tables from the section-contiguous block; the pool is drained in some cases to
 * try to make the vendor's vmalloc fail.  If the new buffers contain non-zero bytes the
 * refresh image is not compared (vendor reads uninitialised memory there).
 *
 * Not covered: tisp_lsc_mirror_flip / _hvflip (no reference, callees lsc_exchange_*, spin
 * lock, printk are not in env.c); the vendor's free order (private_vfree is env.c's);
 * vmalloc failure only if env's vmalloc takes memory from t32o_alloc.
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
#define NIMG	4096
#define BIG1	(17 * T32_LSC_RING_STRIDE + 16)
#define BIG2	(17 * T32_LSC_TABLE_STRIDE + 16)

extern u32 t32_data_e17e0[4];

/*
 * env.c does not provide t32_data_c9570 (the file-name string tisp_lsc_param_array_set passes to
 * isp_printf on its allocation-error path); a placeholder is defined here so the reference
 * links.  It is only read as a string pointer by the isp_printf stub.
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

static unsigned int compared, refused, pool_skip, big_cases, alloc_cases, fail_cases;

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
static void one_itp(u32 a, u32 b, u32 f)
{
	u32 ref = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_lsc_itp, a, b, f, 0, 0);
	u32 our = t32_lsc_itp(a, b, f);

	compared++;
	if (ref != our)
		FAIL("itp a %08x b %08x f %08x vendor %08x ours %08x", a, b, f, ref, our);
}

static void case_itp(void)
{
	one_itp(rnd(), rnd(), rnd());
}

static void edge_itp(void)
{
	static const u32 v[] = { 0, 1, 0xfff, 0x1000, 0xffff, 0x10000, 0x80000000u, 0xffffffffu };
	static const u32 f[] = { 0, 1, 0xfff, 0x1000, 0x1001, 0xffffffffu };
	unsigned int a, b, c;

	for (a = 0; a < 8; a++)
		for (b = 0; b < 8; b++)
			for (c = 0; c < 6; c++)
				one_itp(v[a], v[b], f[c]);
}

/* ---- vendor + our state after init ---- */
static struct {
	u8 *base, *ctx, *cur, *regsh, *v1, *v2, *linfo;
} V;
static u8 oblk[T32_LSC_BLOCK_BYTES + 16];
static u8 o1big[BIG1], o2big[BIG2];
static struct t32_lsc_ctx oc;
static struct t32_lsc_cur ocur;
static struct t32_lsc_lut olut;
static u32 oimg[NIMG];

/* 0 = ready, 1 = refused (ours cannot do the grid / pool too small for a big case) */
static int setup(const u8 *blk, u32 w, u32 h, u32 bayer, const u32 cap[2], u32 b1, u32 b2)
{
	struct t32_lsc g;
	u8 *info, *dbase, *vsec;
	unsigned int i;
	int big = b1 > N1;

	if (t32_lsc_geometry(blk, &g, w, h)) {
		refused++;
		return 1;
	}
	t32o_reset();
	V.base = t32o_alloc(0x8000);
	info = t32o_alloc(64);
	V.linfo = t32o_alloc(64);
	V.v1 = t32o_alloc(b1 + 8);
	V.v2 = t32o_alloc(b2 + 8);
	V.regsh = t32o_alloc(0x4000);
	dbase = t32o_alloc(0x400);
	if (!V.base || !info || !V.linfo || !V.v1 || !V.v2 || !V.regsh || !dbase) {
		if (big) {
			pool_skip++;
			return 1;
		}
		fprintf(stderr, "t32_lscx_oracle: pool exhausted\n");
		exit(2);
	}
	memset(V.linfo, 0, 64);
	memset(V.v1, 0, b1 + 8);
	memset(V.v2, 0, b2 + 8);
	put32(dbase + 204, T32O_PTR(V.regsh));
	t32_data_da844[0] = T32O_PTR(dbase);
	t32_data_d03a0[0] = 0;
	memcpy(V.base + T32_LSC_TUNING_OFF, blk, 120);
	memcpy(V.v1, blk + 120, N1);
	memcpy(V.v2, blk + T32_LSC_TABLES, N2);
	/* guards behind the buffers: p1 all 0xcc; p2 keeps the two tag bytes the vendor's
	 * distance loop reads one table past the end at 0 (the port reads them as 0) */
	memset(V.v1 + b1, 0xcc, 8);
	memset(V.v2 + b2 + 2, 0xcc, 6);
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

	/* ours: same section (hysteresis possibly lowered), payloads, current values */
	memset(&oc, 0, sizeof(oc));
	memcpy(&ocur, V.cur, sizeof(ocur));
	memcpy(oblk, vsec, 120);
	memcpy(oblk + 120, blk + 120, T32_LSC_BLOCK_BYTES - 120);
	if (t32_lsc_setup(oblk, &oc.s, &oc.wk, w, h, 5000)) {
		fprintf(stderr, "t32_lscx_oracle: our setup failed\n");
		exit(2);
	}
	for (i = 0; i < NIMG; i++)
		((u32 *)V.regsh)[i] = oimg[i] = 0xdeadbeefu;
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
	oc.cap[0] = cap[0];
	oc.cap[1] = cap[1];
	/* sentinels for the published sizes */
	t32_data_da850[232] = t32_data_da850[233] = 0x11;
	t32_data_da850[234] = t32_data_da850[235] = 0x22;
	oc.pub[0] = 0x1111;
	oc.pub[1] = 0x2222;
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

#define CI(n, a, b) do { if ((u32)(a) != (u32)(b)) \
		FAIL("%s info %s vendor %u ours %u (idx %u)", what, n, (u32)(a), (u32)(b), g_idx); } while (0)

static void cmp_info(const char *what)
{
	const u8 *in = T32O_HOST(t32_le32(V.ctx + 8));
	unsigned int i;

	CI("r2_center", t32_le32(in), oc.s.r2_center);
	CI("cells", t32_le32(in + 4), oc.s.cells);
	CI("inv_main", t32_le16(in + 8), oc.s.inv_main);
	CI("strength", in[10], oc.s.strength);
	CI("inv_wdr", t32_le16(in + 12), oc.s.inv_wdr);
	CI("strength_wdr", in[14], oc.s.strength_wdr);
	CI("rem_y", in[15], oc.s.rem_y);
	CI("rem_x", in[16], oc.s.rem_x);
	CI("rows", in[17], oc.s.rows);
	CI("cols", in[18], oc.s.cols);
	CI("zone", in[19], oc.s.zone);
	for (i = 0; i < 6; i++)
		CI("flag", in[20 + i], oc.s.flags[i]);
}

static void cmp_cur(const char *what)
{
	const u32 f[8] = { ocur.rsvd0[0], ocur.rsvd0[1], ocur.ct, ocur.rsvd1[0], ocur.rsvd1[1],
			   ocur.tgain, ocur.tgain_s, ocur.wdr };
	unsigned int i;

	for (i = 0; i < 8; i++)
		if (t32_le32(V.cur + 4 * i) != f[i])
			FAIL("%s cur word %u vendor %08x ours %08x (idx %u)", what, i, t32_le32(V.cur + 4 * i), f[i], g_idx);
}

static const u32 sz[4][2] = { { 1920, 1080 }, { 1280, 720 }, { 2560, 1440 }, { 640, 480 } };
static const u32 wds[4] = { 0, 1, 2, 5 };

/* ---- tisp_lsc_tgain_update_s (vendor: a0 = channel, a2 = gain) ---- */
static void case_tgain_s(const u8 *blk, u32 gain, u32 w, u32 h, u32 bayer)
{
	u32 cap[2] = { N1, N2 }, wd = wds[rnd() % 4];

	if (setup(blk, w, h, bayer, cap, N1, N2))
		return;
	put32(V.cur + 28, wd);
	ocur.wdr = wd;
	g_idx = 1000;
	g_old65 = blk[65];
	g_ct = ocur.ct;
	{
		static struct t32_lsc_dwr dl[8];
		unsigned int n0 = t32o_log.n;

		oc.dlog = dl;
		oc.ndlog = 0;
		t32o_call((t32o_fn)(void (*)(void))tisp_lsc_tgain_update_s, 0, 0, gain, 0, 0);
		t32_lsc_tgain_update_s(&oc, 0, gain);
		compared++;
		/* the vendor's system register writes: exactly 0x5050 = strength */
		if (t32o_log.n - n0 != oc.ndlog)
			FAIL("tgain_s register write count vendor %u ours %u", t32o_log.n - n0, oc.ndlog);
		if (oc.ndlog && (t32o_log.w[n0].reg != dl[0].reg || t32o_log.w[n0].val != dl[0].val))
			FAIL("tgain_s register write vendor %x=%08x ours %x=%08x", t32o_log.w[n0].reg,
			     t32o_log.w[n0].val, dl[0].reg, dl[0].val);
	}
	cmp_image("tgain_s");
	cmp_info("tgain_s");
	cmp_cur("tgain_s");
}

/* ---- tisp_lsc_param_array_set ---- */
static u8 *g_vp1, *g_vp2;
static u32 g_req[4], g_nreq, g_nfr;
static void *g_fr[4];

/* mirrors the vendor's real vmalloc result into the section-contiguous block the port reads */
static void *cb_vm(u32 n)
{
	if (g_nreq < 4)
		g_req[g_nreq++] = n;
	if (n % T32_LSC_TABLE_STRIDE == 0) {
		if (!g_vp2)
			return NULL;
		if (n > N2)
			n = N2;
		memcpy(oblk + T32_LSC_TABLES, g_vp2, n);
		memcpy(oblk + T32_LSC_TABLES + n, g_vp2 + n, 2);	/* the tag read one table past the end */
		return oblk + T32_LSC_TABLES;
	}
	if (!g_vp1)
		return NULL;
	if (n > N1)
		n = N1;
	memcpy(oblk + 120, g_vp1, n);
	return oblk + 120;
}

static void cb_free(void *p)
{
	if (g_nfr < 4)
		g_fr[g_nfr++] = p;
}

static u32 pickc(u32 need, u32 full)
{
	switch (rnd() % 6) {
	case 1: return need;
	case 2: return need ? need - 1 : 0;
	case 3: return 0;
	case 4: return rnd() % (full + 1);
	default: return full;
	}
}

static void case_set(const u8 *blk, const u8 *buf, u32 idx, u32 w, u32 h, u32 bayer, int drain)
{
	struct t32_lsc g;
	u8 *vbuf, *vsec;
	u32 ct, tg, tgs, wd, i, cap[2], b1 = N1, b2 = N2, vp1, vp2, len1, len2, vr;
	u32 sec116 = buf[116], need1 = sec116 * T32_LSC_RING_STRIDE, need2 = sec116 * T32_LSC_TABLE_STRIDE;
	u32 ereq[4];
	void *efr[4];
	unsigned int ne_fr = 0, ne_req = 0, k;
	int orv, strict = 1, re1, re2, big = idx > 3, stop = 0;

	if (!idx && t32_lsc_geometry(buf, &g, w, h)) {
		refused++;
		return;
	}
	if (big) {
		u32 bi = idx > 16 ? 17 : idx;

		b1 = bi * T32_LSC_RING_STRIDE;
		b2 = bi * T32_LSC_TABLE_STRIDE;
	}
	cap[0] = N1;
	cap[1] = N2;
	if (!idx) {
		cap[0] = pickc(need1, N1);
		cap[1] = pickc(need2, N2);
	}
	re1 = !idx && cap[0] < need1;
	re2 = !idx && cap[1] < need2;
	if (setup(blk, w, h, bayer, cap, b1, b2))
		return;
	vbuf = t32o_alloc(BUFB + 16);
	if (!vbuf) {
		if (big) {
			pool_skip++;
			return;
		}
		fprintf(stderr, "t32_lscx_oracle: pool exhausted\n");
		exit(2);
	}
	memcpy(vbuf, buf, BUFB);
	memset(vbuf + BUFB, 0xcc, 16);
	ct = rnd() % 3 ? 1000 + rnd() % 7000 : 5000;
	tg = rnd() % 0x130000;
	tgs = rnd() % 0x130000;
	wd = wds[rnd() % 4];
	put32(V.cur + 8, ct);
	put32(V.cur + 20, tg);
	put32(V.cur + 24, tgs);
	put32(V.cur + 28, wd);
	g_idx = idx;
	g_old65 = blk[65];
	g_ct = ct;
	ocur.ct = ct;
	ocur.tgain = tg;
	ocur.tgain_s = tgs;
	ocur.wdr = wd;
	if (big) {
		memset(o1big, 0, sizeof(o1big));
		memset(o2big, 0, sizeof(o2big));
		memcpy(o1big, blk + 120, N1);
		memcpy(o2big, blk + T32_LSC_TABLES, N2);
		olut.p1 = o1big;
		olut.p2 = o2big;
		big_cases++;
	}
	if (drain && (re1 || re2)) {
		for (k = 0; k < (1u << 20) && t32o_alloc(4096); k++)
			;
		for (k = 0; k < (1u << 20) && t32o_alloc(16); k++)
			;
	}

	vr = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_lsc_param_array_set, 0, idx, T32O_PTR(vbuf), 0, 0);
	vp1 = t32_le32(V.linfo + 16);
	vp2 = t32_le32(V.linfo + 20);
	g_vp1 = vp1 ? T32O_HOST(vp1) : NULL;
	g_vp2 = vp2 ? T32O_HOST(vp2) : NULL;
	if (re1 && g_vp1)
		for (i = 0; i < need1; i++)
			if (g_vp1[i])
				strict = 0;
	if (re2 && g_vp2) {
		for (i = 0; i < need2; i++)
			if (g_vp2[i])
				strict = 0;
		if (sec116 == 3 && t32_le16(g_vp2 + need2))
			strict = 0;
	}
	/* expected allocator traffic of ours */
	if (re1) {
		efr[ne_fr++] = oblk + 120;
		ereq[ne_req++] = need1;
		if (!vp1)
			stop = 1;
	}
	if (re2 && !stop) {
		efr[ne_fr++] = oblk + T32_LSC_TABLES;
		ereq[ne_req++] = need2;
	}
	if (re1 || re2) {
		alloc_cases++;
		if (!vp1 || !vp2)
			fail_cases++;
	}
	g_nreq = g_nfr = 0;
	orv = t32_lsc_param_array_set(&oc, 0, idx, buf, cb_vm, cb_free);
	compared++;
	if (vr != (u32)orv)
		FAIL("param_set idx %u return vendor %d ours %d", idx, (int)vr, orv);
	if (g_nreq != ne_req || g_nfr != ne_fr)
		FAIL("param_set idx %u allocator calls ours %u/%u expected %u/%u", idx, g_nreq, g_nfr, ne_req, ne_fr);
	for (k = 0; k < ne_req; k++)
		if (g_req[k] != ereq[k] || g_fr[k] != efr[k])
			FAIL("param_set idx %u allocator call #%u differs (req %u vs %u)", idx, k, g_req[k], ereq[k]);
	if (t32_le32(t32_data_dc1a4) != oc.cap[0] || t32_le32(t32_data_dc1a4 + 4) != oc.cap[1])
		FAIL("param_set idx %u caps vendor %u/%u ours %u/%u", idx, t32_le32(t32_data_dc1a4),
		     t32_le32(t32_data_dc1a4 + 4), oc.cap[0], oc.cap[1]);
	if (t32_le16(V.linfo) != olut.size[0] || t32_le16(V.linfo + 2) != olut.size[1])
		FAIL("param_set idx %u lut sizes vendor %u/%u ours %u/%u", idx, t32_le16(V.linfo),
		     t32_le16(V.linfo + 2), olut.size[0], olut.size[1]);
	if ((vp1 == 0) != (olut.p1 == NULL) || (vp2 == 0) != (olut.p2 == NULL))
		FAIL("param_set idx %u payload pointer NULL-ness differs", idx);
	if (t32_le16(t32_data_da850 + 232) != oc.pub[0] || t32_le16(t32_data_da850 + 234) != oc.pub[1])
		FAIL("param_set idx %u published sizes vendor %u/%u ours %u/%u", idx,
		     t32_le16(t32_data_da850 + 232), t32_le16(t32_data_da850 + 234), oc.pub[0], oc.pub[1]);
	if (strict || orv)
		cmp_image("param_set");
	cmp_info("param_set");
	cmp_cur("param_set");
	vsec = T32O_HOST(t32_le32(V.ctx));
	for (i = 0; i < 120; i++) {
		if (!strict && (i == 48 || i == 49))
			continue;
		if (vsec[i] != oblk[i])
			FAIL("param_set idx %u section[%u] vendor %02x ours %02x", idx, i, vsec[i], oblk[i]);
	}
	len1 = big ? b1 : re1 ? need1 : N1;
	len2 = big ? b2 : re2 ? need2 : N2;
	if (vp1 && memcmp(g_vp1, olut.p1, len1))
		FAIL("param_set idx %u payload 1 differs", idx);
	if (vp2 && memcmp(g_vp2, olut.p2, len2))
		FAIL("param_set idx %u payload 2 differs", idx);
	if (!re1)
		for (i = 0; i < 8; i++)
			if (V.v1[b1 + i] != 0xcc)
				FAIL("param_set idx %u vendor wrote behind payload 1", idx);
	if (!re2)
		for (i = 0; i < 8; i++)
			if (V.v2[b2 + i] != (i < 2 ? 0 : 0xcc))
				FAIL("param_set idx %u vendor wrote behind payload 2", idx);
	if (memcmp(vbuf, buf, BUFB))
		FAIL("param_set idx %u vendor modified the input buffer", idx);
	for (i = 0; i < 16; i++)
		if (vbuf[BUFB + i] != 0xcc)
			FAIL("param_set idx %u vendor wrote behind the input buffer", idx);
}

/* ---- tisp_lsc_param_array_get ---- */
static void case_get(const u8 *sec, const u8 *tail)
{
	u8 *vctx, *vsec, *vtail, *vbuf, *vlen;
	static u8 obuf[1024];
	u8 sc[120], tl[12];
	u32 olen = 0, i;

	for (i = 0; i < 120; i++)
		sc[i] = sec[i] == 0xaa ? 0x55 : sec[i];
	for (i = 0; i < 12; i++)
		tl[i] = tail[i] == 0xaa ? 0x55 : tail[i];
	t32o_reset();
	vctx = t32o_alloc(32);
	vsec = t32o_alloc(128);
	vtail = t32o_alloc(16);
	vbuf = t32o_alloc(1024);
	vlen = t32o_alloc(8);
	if (!vctx || !vsec || !vtail || !vbuf || !vlen)
		exit(2);
	memcpy(vsec, sc, 120);
	memcpy(vtail, tl, 12);
	memset(vbuf, 0xaa, 1024);
	put32(vlen, 0);
	put32(vlen + 4, 0xaaaaaaaau);
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
	if (t32_le32(vlen + 4) != 0xaaaaaaaau)
		FAIL("param_get vendor wrote behind the length word");
	for (i = 0; i < 1024; i++)
		if (vbuf[i] != obuf[i])
			FAIL("param_get buf[%u] vendor %02x ours %02x", i, vbuf[i], obuf[i]);
	if (memcmp(vsec, sc, 120) || memcmp(vtail, tl, 12))
		FAIL("param_get vendor modified its source");
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
	memset(vctx, 0, 32);
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
	if (have_ctx && (t32_le32(vctx + 4) || t32_le32(vctx + 8) || t32_le32(vctx + 12)))
		FAIL("deinit vendor left a field set (cur %d info %d stats %d)", cur, info, stats);
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
	static const u32 idxs[] = { 0, 1, 2, 3, 3, 1, 4, 5, 7, 16, 17, 255, 0, 2, 8, 15, 16, 18, 0, 0 };
	unsigned int c;

	edge_itp();
	for (c = 0; c < n; c++) {
		u8 tail[12];
		u32 zi = rnd() % 4, bayer = rnd() % 8, idx = idxs[rnd() % 20], gain = rnd() % 3 ? rnd() : rnd() % 0x130000;
		unsigned int i;

		gen_blk(blk);
		for (i = 0; i < BUFB; i++)
			buf[i] = (u8)rnd();
		gen_sec(buf);
		/* the replacement mesh table (payload 2 starts at buf + 3464) carries a plausible tag */
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
		case_set(blk, buf, idx, sz[zi][0], sz[zi][1], bayer, rnd() % 3 == 0);
		/* the table of the section's own count, so that the refresh path runs */
		case_set(blk, buf, blk[116], sz[zi][0], sz[zi][1], bayer, 0);
		case_deinit(rnd() % 4 != 0, rnd() & 1, rnd() & 1, rnd() & 1);
	}
	case_itp();
	case_deinit(0, 0, 0, 0);
	case_deinit(1, 0, 0, 0);
	case_deinit(1, 1, 1, 1);
	case_deinit(1, 0, 1, 0);
	case_deinit(1, 0, 0, 1);

	printf("t32_lscx_oracle: random %u compared (%u refused, %u pool-skipped), %u big-idx, %u alloc, %u vmalloc-fail, 0 mismatches\n",
	       compared, refused, pool_skip, big_cases, alloc_cases, fail_cases);
	return 0;
}
