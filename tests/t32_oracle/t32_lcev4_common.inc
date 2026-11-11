/*
 * Shared harness of the t32_lcev4_* oracles: Tisp_lce_soft_simd_v4 (vendor 0x4db64; Matt Davis' recovered C, PR #15
 * @ 2e2e06bc) against the blocks t32_lce_v4_* of driver/t32/tx_isp_t32_lce.h.
 *
 * Method (as t32_lcet41_*): each phase oracle runs the whole vendor function truncated by a build-time sed
 * (trunc_v4.sh, REFFIX, T32O_STOP = label) at the first arrival at the label that ends the phase, and the port
 * blocks of that phase on the first 16 tile group / first tile row; the end-to-end oracle runs both whole.
 * Compared after the run: the whole working buffer (every byte, also those neither side may touch), the vendor's
 * scratch .bss e1e48 .. e20d8 against the port's struct t32_lce_v4_scr, the parameter block and the band table
 * (unchanged).  The leaves (thres_search(_simd_v4), hist_method, hist_adjust, 16bit_data_converge,
 * get_distance_1dim) run as the vendor functions on the vendor side and as our ports (hook table) on the port side.
 * Cases our port refuses (t32_lce_v4 returns -1) are counted, not compared.  Exit 0 = equal; 1 = first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_lce.h"

#define WSZ 0x26400U
#define PSZ 400U

/* the vendor's scratch .bss e1e48 .. e20d8 (one blob, 16 byte objects) */
u8 oc_v4bss[0x290 + 16] __attribute__((aligned(16)));
__asm__(".globl t32_data_e1e48, t32_data_e1e58, t32_data_e1e68, t32_data_e1e78, t32_data_e1e88, t32_data_e1e98, t32_data_e1ea8, t32_data_e1eb8, t32_data_e1ec8, t32_data_e1ed8, t32_data_e1ee8, t32_data_e1ef8, t32_data_e1f08, t32_data_e1f18, t32_data_e1f28, t32_data_e1f38, t32_data_e1f48, t32_data_e1f58, t32_data_e1f68, t32_data_e1f78, t32_data_e1f88, t32_data_e1f98, t32_data_e1fa8, t32_data_e1fb8, t32_data_e1fc8, t32_data_e1fd8, t32_data_e1fe8, t32_data_e1ff8, t32_data_e2008, t32_data_e2018, t32_data_e2028, t32_data_e2038, t32_data_e2048, t32_data_e2058, t32_data_e2068, t32_data_e2078, t32_data_e2088, t32_data_e2098, t32_data_e20a8, t32_data_e20b8, t32_data_e20c8\n.set t32_data_e1e48, oc_v4bss+0x0\n.set t32_data_e1e58, oc_v4bss+0x10\n.set t32_data_e1e68, oc_v4bss+0x20\n.set t32_data_e1e78, oc_v4bss+0x30\n.set t32_data_e1e88, oc_v4bss+0x40\n.set t32_data_e1e98, oc_v4bss+0x50\n.set t32_data_e1ea8, oc_v4bss+0x60\n.set t32_data_e1eb8, oc_v4bss+0x70\n.set t32_data_e1ec8, oc_v4bss+0x80\n.set t32_data_e1ed8, oc_v4bss+0x90\n.set t32_data_e1ee8, oc_v4bss+0xa0\n.set t32_data_e1ef8, oc_v4bss+0xb0\n.set t32_data_e1f08, oc_v4bss+0xc0\n.set t32_data_e1f18, oc_v4bss+0xd0\n.set t32_data_e1f28, oc_v4bss+0xe0\n.set t32_data_e1f38, oc_v4bss+0xf0\n.set t32_data_e1f48, oc_v4bss+0x100\n.set t32_data_e1f58, oc_v4bss+0x110\n.set t32_data_e1f68, oc_v4bss+0x120\n.set t32_data_e1f78, oc_v4bss+0x130\n.set t32_data_e1f88, oc_v4bss+0x140\n.set t32_data_e1f98, oc_v4bss+0x150\n.set t32_data_e1fa8, oc_v4bss+0x160\n.set t32_data_e1fb8, oc_v4bss+0x170\n.set t32_data_e1fc8, oc_v4bss+0x180\n.set t32_data_e1fd8, oc_v4bss+0x190\n.set t32_data_e1fe8, oc_v4bss+0x1a0\n.set t32_data_e1ff8, oc_v4bss+0x1b0\n.set t32_data_e2008, oc_v4bss+0x1c0\n.set t32_data_e2018, oc_v4bss+0x1d0\n.set t32_data_e2028, oc_v4bss+0x1e0\n.set t32_data_e2038, oc_v4bss+0x1f0\n.set t32_data_e2048, oc_v4bss+0x200\n.set t32_data_e2058, oc_v4bss+0x210\n.set t32_data_e2068, oc_v4bss+0x220\n.set t32_data_e2078, oc_v4bss+0x230\n.set t32_data_e2088, oc_v4bss+0x240\n.set t32_data_e2098, oc_v4bss+0x250\n.set t32_data_e20a8, oc_v4bss+0x260\n.set t32_data_e20b8, oc_v4bss+0x270\n.set t32_data_e20c8, oc_v4bss+0x280\n");

/* data.c:2717..2733 (bf9e8 ..): the weight table bf9ec (1024 / n, Q15) and the halfword before it (index -1) */
u16 oc_bf9e8[34] __attribute__((aligned(4))) = { 0x0020, 0x0000, 0x7fff, 0x2aab, 0x1555, 0x0ccd, 0x0889, 0x0618, 0x0492,
	0x038e, 0x02d8, 0x0254, 0x01f0, 0x01a4, 0x0168, 0x0138, 0x0111, 0x00f1, 0x00d6, 0x00c0, 0x00ac, 0x009c, 0x008e,
	0x0082, 0x0077, 0x006d, 0x0065, 0x005d, 0x0057, 0x0051, 0x004b, 0x0046, 0x0042, 0x003e };
__asm__(".globl t32_data_bf9ec\n.set t32_data_bf9ec, oc_bf9e8+4\n");
/* data.c:25734 / 25766: the falling (32 .. 1) and rising (1 .. 32) ramp tables d6b64 / d6be4 */
u32 oc_d6b64[32] __asm__("t32_data_d6b64");
u32 oc_d6be4[32] __asm__("t32_data_d6be4");
/* rodata strings of the vendor's printk (not printed) */
u8 oc_bf994[64] __asm__("t32_data_bf994");
u8 oc_c9660[64] __asm__("t32_data_c9660");

extern u32 t32_data_d6b50[5];
extern u32 t32_data_d6c64[4];
extern u8 t32_data_e188c[64];
extern u64 Tisp_lce_soft_simd_v4(u32 a0, u32 a1, u32 a2, u32 a3);

static u32 rs = 0x4db64u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static struct t32_lce_tab tab;

/* hook table of the port side: the existing ports of this header */
static void h_ts(const u16 h[16][32], const u16 *t, u8 *a, u8 *b, u8 *c) { t32_lce_thres_search_simd(h, t, a, b, c); }
static void h_t(const u16 *h, const u16 *t, u8 *o) { t32_lce_thres_search(h, t, o); }
static int h_hm(u16 *h, const u8 *t, const u8 *p) { return t32_lce_hist_method(h, t, p, &tab); }
static void h_ha(u8 *c, const u8 *u, const u8 *d) { t32_lce_hist_adjust(c, u, d, &tab); }
static void h_cv(u32 g, u32 lo, u32 hi, const u8 *t, u8 *c) { t32_lce_16bit_data_converge(g, lo, hi, t, c); }
static void h_cvs(u32 g, u32 lo, u32 hi, const u8 *t, u8 *c) { t32_lce_16bit_data_converge_simd(g, lo, hi, t, c); }

static const struct t32_lce_v4_ops ops = { h_ts, h_t, h_hm, h_ha, h_cv, h_cvs, &tab };

static const u32 real_d6b50[5] = { 0x18100800, 0x38302820, 0x70605040, 0xb0a09080, 0xf0e0d0c0 };
static unsigned int compared, refused;

static int port_phase(struct t32_lce_v4_st *st);
static void gen_phase(u8 *par);

/* the constant tables: the data.c values, in a quarter of the cases random ones (the index -1 halfword stays 0) */
static void gen_tables(void)
{
	u8 *d6c = (u8 *)t32_data_d6c64;
	int rt = !(rnd() % 4);
	unsigned int i;

	for (i = 0; i < 32; i++) {
		oc_bf9e8[2 + i] = rt ? (u16)rnd() : (u16)(i ? 0x8000 / (i + 1) : 0x7fff);
		oc_d6b64[i] = rt ? (rnd() % 3 ? rnd() % 64 : rnd()) : 32 - i;
		oc_d6be4[i] = rt ? (rnd() % 3 ? rnd() % 64 : rnd()) : i + 1;
	}
	if (!rt) {
		static const u16 bf[32] = { 0x7fff, 0x2aab, 0x1555, 0x0ccd, 0x0889, 0x0618, 0x0492, 0x038e, 0x02d8, 0x0254,
			0x01f0, 0x01a4, 0x0168, 0x0138, 0x0111, 0x00f1, 0x00d6, 0x00c0, 0x00ac, 0x009c, 0x008e, 0x0082,
			0x0077, 0x006d, 0x0065, 0x005d, 0x0057, 0x0051, 0x004b, 0x0046, 0x0042, 0x003e };

		memcpy(oc_bf9e8 + 2, bf, sizeof(bf));
	}
	memcpy(t32_data_d6b50, real_d6b50, 20);
	if (rt)
		for (i = 0; i < 20; i++)
			((u8 *)t32_data_d6b50)[i] = (u8)rnd();
	d6c[0] = 0x0b; d6c[1] = 0x0c; d6c[3] = 0x14;
	if (rt) {
		d6c[0] = (u8)(rnd() % 40);
		d6c[1] = (u8)((rnd() % 8) ? rnd() % 40 : 32 * (rnd() % 2));
		d6c[3] = (u8)(rnd() % 40);
	}
	memcpy(tab.adj_base, t32_data_d6b50, 20);
	tab.pdf_rnd = d6c[0];
	tab.pdf_sh = d6c[1];
	tab.pdf_shift = d6c[3];
	memcpy(tab.w, oc_bf9e8 + 2, 64);
	memcpy(tab.up, oc_d6be4, 128);
	memcpy(tab.down, oc_d6b64, 128);
}

static int one(u8 *par, u8 *w, u32 mode, const u16 *band)
{
	u8 *wo = malloc(WSZ), *po = malloc(PSZ), *wr, *pr;
	static struct t32_lce_v4_st st;
	u16 bo[12];
	int bad = 0, rc;
	unsigned int i;

	memcpy(wo, w, WSZ);
	memcpy(po, par, PSZ);
	memcpy(bo, band, 24);
	memset(&st, 0, sizeof(st));
	st.par = po; st.w = wo; st.mode = mode & 0xff; st.band = bo; st.ops = &ops;
	rc = port_phase(&st);
	if (rc < 0) {
		refused++;
		free(wo); free(po);
		return 0;
	}
	t32o_reset();
	memcpy(t32_data_e188c, band, 24);
	wr = t32o_alloc(WSZ); pr = t32o_alloc(PSZ);
	memcpy(wr, w, WSZ);
	memcpy(pr, par, PSZ);
	memset(oc_v4bss, 0, sizeof(oc_v4bss));
	t32o_call((t32o_fn)(void (*)(void))Tisp_lce_soft_simd_v4, 0, T32O_PTR(pr), T32O_PTR(wr), mode, 0);
	compared++;
	if (memcmp(pr, par, PSZ))
		bad = printf("MISMATCH vendor changed par\n");
	if (memcmp(po, par, PSZ) || memcmp(bo, band, 24))
		bad = printf("MISMATCH port changed par / band\n");
	if (memcmp(t32_data_e188c, band, 24))
		bad = printf("MISMATCH vendor changed the band table\n");
	for (i = 0; i < WSZ && !bad; i++)
		if (wr[i] != wo[i])
			bad = printf("MISMATCH w+0x%x vendor %02x port %02x (mode %u, case %u)\n", i, wr[i], wo[i], mode, compared);
	for (i = 0; i < sizeof(st.s) && !bad; i++)
		if (oc_v4bss[i] != ((u8 *)&st.s)[i])
			bad = printf("MISMATCH bss e1e48+0x%x vendor %02x port %02x (mode %u, case %u)\n", i, oc_v4bss[i],
				     ((u8 *)&st.s)[i], mode, compared);
	for (i = sizeof(st.s); i < sizeof(oc_v4bss) && !bad; i++)
		if (oc_v4bss[i])
			bad = printf("MISMATCH vendor wrote bss past e20d8\n");
	free(wo); free(po);
	return bad;
}

/* random inputs: tile histograms of several scales, band thresholds near fractions of a tile total, all parameters */
static int gen_and_run(void)
{
	static u8 par[PSZ], w[WSZ];
	unsigned int i, sc = 1u << (rnd() % 13), tot;
	u16 band[12];
	u32 mode;

	for (i = 0; i < PSZ; i++)
		par[i] = (u8)rnd();
	for (i = 0; i < WSZ; i++)
		w[i] = (u8)rnd();
	for (i = 0; i < 0x9000; i += 2) {
		u32 v = rnd() % (1 + sc);

		if (!(rnd() % 200))
			v = 0xffff - rnd() % 4;
		if (sc == 1 && rnd() % 2)
			v = 0;
		w[i] = (u8)v; w[i + 1] = (u8)(v >> 8);
	}
	tot = 16 * sc;
	for (i = 0; i < 576; i++) {
		u8 *t = w + 64 * i;
		unsigned int k, v;

		if (!(rnd() % (i < 16 ? 4 : 3000))) {	/* a big first bin: lo / mid (or all three) thresholds at bin 0 */
			v = (tot * (1 + rnd() % 4)) / 4 + rnd() % (1 + tot / 4);
			t[0] = (u8)v; t[1] = (u8)(v >> 8);
		}
		if (!(rnd() % (i < 16 ? 8 : 2000)))	/* several bins near 65535: saturating lanes */
			for (k = 0; k < 32; k++)
				if (rnd() % 2) {
					v = 0xffff - rnd() % 0x3000;
					t[2 * k] = (u8)v; t[2 * k + 1] = (u8)(v >> 8);
				}
	}
	if (rnd() % 2)	/* saved histograms close to the current ones (get_distance_1dim around par[109]) */
		for (i = 0; i < 0x9000; i += 2) {
			u32 v = (w[i] | w[i + 1] << 8) + rnd() % 9 - 4;

			if (!(rnd() % 300))
				v ^= 0x8000;
			w[0x9900 + i] = (u8)v; w[0x9900 + i + 1] = (u8)(v >> 8);
		}
	for (i = 0; i < 12; i++) {
		u32 k = i % 3;

		band[i] = (u16)((rnd() % 10) ? (tot * (k + 1)) / 4 + rnd() % (1 + tot / 8) - tot / 16 : rnd() % (2 * tot + 2));
		if (!(rnd() % 50))
			band[i] = (u16)rnd();
	}
	for (i = 0; i < 4; i++)
		par[201 + i] = (rnd() % 3) ? (u8)(sc / 2 + rnd() % (1 + sc)) : (u8)rnd();
	par[12] = (u8)(rnd() % 19);
	par[13] = (u8)(rnd() % 33);
	if (!(rnd() % 4))
		par[13] = (u8)(16 * (rnd() % 3));
	par[108] = (rnd() % 2) ? 0 : (u8)rnd();
	par[109] = (u8)(rnd() % 3 ? rnd() % 40 : rnd());
	par[119] = (u8)rnd();
	par[120] = (u8)(rnd() % 3 ? rnd() % 8 : rnd());
	par[121] = (u8)(rnd() % 3 ? rnd() % 64 : rnd());
	for (i = 161; i < 201; i++)
		par[i] = (u8)(rnd() % 3 ? rnd() % 64 : rnd());
	if (!(rnd() % 100))	/* refused geometry */
		par[12] = (u8)(19 + rnd() % 200);
	if (!(rnd() % 100))
		par[13] = (u8)(33 + rnd() % 200);
	mode = (rnd() % 3) ? ((rnd() % 3) ? 4 : 0) : rnd() % 6;
	if (!(rnd() % 8))
		mode |= rnd() << 8;
	gen_tables();
	gen_phase(par);
	return one(par, w, mode, band);
}

int main(void)
{
	unsigned int c, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 300;
	int fail = 0;

	for (c = 0; c < n && !fail; c++)
		fail = gen_and_run();
	printf("%s: %u compared (%u refused by ours), %u mismatches\n", ORACLE_NAME, compared, refused, fail ? 1u : 0u);
	return fail || !compared;
}
