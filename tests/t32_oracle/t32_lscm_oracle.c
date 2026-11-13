// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: LSC mirror / flip.  Runs the unmodified vendor functions
 * (Matt Davis' recovered C, PR #15 @ 2e2e06bc: tisp_lsc_mirror_flip, tisp_lsc_hvflip,
 * lsc_exchange_line, lsc_exchange_data) and our ports from driver/t32/tx_isp_t32_lsc.h
 * (t32_lsc_mirror_flip / _hvflip with t32_lsc_xops_default) from the same state and
 * compares everything.
 *
 * State as in t32_lscx_oracle (setup / compare helpers copied from it): the vendor side
 * is built by tisp_lsc_init, ours is seeded from the vendor's section, current values and
 * payloads.  Per case: random block / geometry / bayer / colour temperature, line flag
 * (a3) and element flag (arg4) each 0, 1 or another value, channel 0, the wrapper or the
 * direct call.  Compared: the shadow image (4096 words, canary filled), all mesh tables of
 * payload 2 (exchanged in place, plus guard bytes), info struct, current values (32 bytes),
 * the flip counters (ctx + 12, all 12 bytes), return values (hvflip: 0; mirror_flip's own
 * result is the printk left-over and not compared), the spin lock pairing (vendor).
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
#define NIMG	4096

extern u32 t32_data_e17e0[4];
unsigned char t32_data_bf4d0[64];	/* printk arguments of the vendor's trace line (string pointers only) */
unsigned char t32_data_c958c[64];

extern u64 tisp_lsc_mirror_flip(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_lsc_hvflip(u32 a0, u32 a1, u32 a2, u32 a3);

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

static unsigned int compared, refused, pool_skip, nline, nelem;
#define FAIL(...) do { printf("MISMATCH " __VA_ARGS__); printf("\n"); \
		       printf("t32_lscm_oracle: %u compared, 1 mismatch\n", compared); exit(1); } while (0)

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


/* ---- vendor + our state after init ---- */
static struct {
	u8 *base, *ctx, *cur, *regsh, *v1, *v2, *linfo;
} V;
static u8 oblk[T32_LSC_BLOCK_BYTES + 16];
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
		fprintf(stderr, "t32_lscm_oracle: pool exhausted\n");
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
		fprintf(stderr, "t32_lscm_oracle: tisp_lsc_init left no context\n");
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
		fprintf(stderr, "t32_lscm_oracle: our setup failed\n");
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


/* ---- tisp_lsc_mirror_flip / tisp_lsc_hvflip (vendor: a0 = channel, a3 = lines, arg4 = elements) ---- */
static u32 g_locks;
static void case_flip(const u8 *blk, u32 ct, u32 w, u32 h, u32 bayer, u32 a3, u32 a4, int wrap)
{
	u32 cap[2] = { N1, N2 }, rv, ro;
	u8 oinfo[32], ostats[12], *vin, *vst;
	unsigned int n0;

	if (setup(blk, w, h, bayer, cap, N1, N2))
		return;
	put32(V.cur + 8, ct);
	ocur.ct = ct;
	put32(V.cur + 28, rnd() & 1);
	ocur.wdr = t32_le32(V.cur + 28);
	vin = T32O_HOST(t32_le32(V.ctx + 8));
	vst = T32O_HOST(t32_le32(V.ctx + 12));
	if (!vin || !vst) {
		fprintf(stderr, "t32_lscm_oracle: no info / stats block after init\n");
		exit(2);
	}
	memcpy(oinfo, vin, sizeof(oinfo));
	memcpy(ostats, vst, sizeof(ostats));
	oc.info = oinfo;
	oc.stats = ostats;
	g_idx = 2000 + a3 * 10 + a4;
	g_old65 = blk[65];
	g_ct = ct;
	n0 = t32o_log.n;
	rv = (u32)t32o_call((t32o_fn)(void (*)(void))(wrap ? tisp_lsc_hvflip : tisp_lsc_mirror_flip), 0,
			    rnd(), rnd(), a3, a4);
	ro = wrap ? (u32)t32_lsc_hvflip(&oc, 0, a3, a4, &t32_lsc_xops_default)
		  : (u32)t32_lsc_mirror_flip(&oc, 0, a3, a4, &t32_lsc_xops_default);
	compared++;
	if (wrap && rv != ro)
		FAIL("hvflip return vendor %u ours %u", rv, ro);
	if (t32o_log.n != n0)
		FAIL("flip: %u direct register writes by the vendor", t32o_log.n - n0);
	if (memcmp(V.v2, olut.p2, N2 + 2)) {
		unsigned int i, d = 0;

		for (i = 0; i < N2 + 2; i++)
			if (V.v2[i] != olut.p2[i]) { if (!d) printf("first diff +%u: %02x / %02x\n", i, V.v2[i], olut.p2[i]); d++; }
		printf("%u bytes differ, orig %02x\n", d, blk[T32_LSC_TABLES + 0]);
		FAIL("flip tables differ (a3 %u arg4 %u rows %u cols %u ntab %u)", a3, a4, vin[17], vin[18], blk[116]);
	}
	{
		static const u8 g[6] = { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc };

		if (memcmp(V.v2 + N2 + 2, g, 6))
			FAIL("flip wrote behind the vendor tables");
	}
	if (memcmp(vst, ostats, sizeof(ostats)))
		FAIL("flip counters differ (a3 %u arg4 %u: %u/%u %u/%u)", a3, a4, vst[9], ostats[9], vst[10], ostats[10]);
	nline += a3 == 1;
	nelem += a4 == 1;
	cmp_image("flip");
	cmp_info("flip");
	cmp_cur("flip");
}

int main(void)
{
	const unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 1500;
	static u8 blk[T32_LSC_BLOCK_BYTES + 16];
	static const u32 fl[] = { 0, 1, 1, 2, 0xffffffffu, 1 };
	unsigned int c;

	(void)g_locks;
	for (c = 0; c < n; c++) {
		u32 zi = rnd() % 4, bayer = rnd() % 8, ct = rnd() % 4 ? 1500 + rnd() % 6000 : rnd() % 20000;

		gen_blk(blk);
		case_flip(blk, ct, sz[zi][0], sz[zi][1], bayer, fl[rnd() % 6], fl[rnd() % 6], rnd() & 1);
	}
	case_flip(blk, 5000, 1920, 1080, 0, 1, 1, 0);
	case_flip(blk, 5000, 640, 480, 1, 1, 0, 1);
	case_flip(blk, 5000, 1280, 720, 2, 0, 1, 1);
	printf("t32_lscm_oracle: random %u compared (%u refused, %u pool-skipped), %u line / %u element flips, 0 mismatches\n",
	       compared, refused, pool_skip, nline, nelem);
	return 0;
}
