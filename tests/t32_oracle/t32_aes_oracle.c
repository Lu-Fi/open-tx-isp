// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: AE per-frame paths of driver/t32/tx_isp_t32_stats.h against the
 * unmodified vendor functions (Matt Davis' recovered C, PR #15 @ 2e2e06bc).
 * Compared per case: return value, the buffers the vendor writes, the outputs and
 * the logged calls of the callee mocks (tisp_ae_fliker_detect, tisp_ae_lib_bilinear_intp,
 * whose sources are not in ref/).  fix_point_* run as the vendor refs on both sides.
 * The whole work area (tuning, buffers, histograms, context, tables, outputs) is
 * compared.  tisp_ae_interrupt_hist runs as the vendor ref (the env.c stub is weak):
 * its return value, the cur register it reads and the two tisp_reg_read values of
 * get_hist (independent, so every a/b combination is reached) are checked.
 * get_hist / get_statistics are void in use: their vendor return is a leftover
 * register (spin unlock result / loop value) no caller reads, so it is not compared.
 * Exit 0 = all equal, 1 = first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_stats.h"

#define MEMSZ	0x10000
#define O_TUNE	0x0
#define O_BUF	0x1000
#define O_HB	0x4000
#define O_STAT	0x6100
#define O_Q	0xa100
#define O_CTX	0xa900
#define O_TAB	0xaa00
#define O_Y	0xab00
#define O_OUT	0xac00

u32 t32_data_e15a8[4] __attribute__((weak));
u32 t32_data_e1598[4] __attribute__((weak));
u32 t32_data_e15d8[4] __attribute__((weak));
u32 t32_data_e1588[4] __attribute__((weak));
u8 t32_data_e151c[4] __attribute__((weak));
u8 t32_data_e1518[4] __attribute__((weak));
u8 t32_data_e1524[4] __attribute__((weak));
u8 t32_data_e1520[4] __attribute__((weak));
u8 t32_small_bss[64] __attribute__((weak));

u64 tisp_ae_get_hist(u32, u32, u32, u32);
u64 tisp_ae_interrupt_hist(u32, u32, u32, u32);
u64 tisp_ae_get_statistics(u32, u32, u32, u32);
u64 tisp_ae_long_target(u32, u32, u32, u32, u32);
u64 tisp_ae_calc_convergence_speed(u32, u32, u32, u32, u32, u32);
u64 tisp_ae_weight_mean(u32, u32, u32, u32, u32, u32);
u64 fix_point_mult2_32(u32, u32, u32, u32);

static u32 rs = 0x51d3a7u, fs, g_base, g_rdval, g_rd[2], g_rdn, g_rdreg[4], g_cur, g_curreg;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}
static u32 frnd(void)
{
	fs ^= fs << 13; fs ^= fs >> 17; fs ^= fs << 5;
	return fs;
}

static u32 mlog[2][128];
static unsigned mn[2];
static int side;
static void mlogadd(u32 v)
{
	if (mn[side] < 128)
		mlog[side][mn[side]++] = v;
}

static u64 bil_core(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6, u32 a7)
{
	u32 v[8] = { a0, a1, a2, a3, a4, a5, a6, a7 }, h = 0x9e3779b9u, i;

	for (i = 0; i < 8; i++) {
		mlogadd(v[i]);
		h = (h ^ v[i]) * 0x01000193u + i;
		h ^= h >> 15;
	}
	return (u64)(h * 2654435761u) << 32 | h;
}

/* get_hist reads tisp_reg_read(ch, 96 or 4) twice: hand out g_rd[0], g_rd[1] and log ch / reg */
u64 tisp_reg_read(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	if (g_rdn < 2) {
		g_rdreg[2 * g_rdn] = a0;
		g_rdreg[2 * g_rdn + 1] = a1;
		return g_rd[g_rdn++];
	}
	g_rdn++;
	return g_rdval;
}
__attribute__((weak)) u64 system_reg_read(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a1; (void)a2; (void)a3;
	return g_rdval;
}
__attribute__((weak)) u64 private_dma_cache_sync(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a1; (void)a2; (void)a3;
	return 0;
}
__attribute__((weak)) u64 __private_spin_lock_irqsave(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a2; (void)a3;
	if (a1)
		*(u32 *)(uintptr_t)a1 = 0;
	return 0;
}
__attribute__((weak)) u64 private_spin_unlock_irqrestore(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a1; (void)a2; (void)a3;
	return 0;
}
__attribute__((weak)) u64 tisp_ae_fliker_detect(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	mlogadd(a0);
	mlogadd(a1 - g_base);
	return 0;
}
__attribute__((weak)) u64 tisp_ae_lib_bilinear_intp(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5,
						    u32 a6, u32 a7)
{
	return bil_core(a0, a1, a2, a3, a4, a5, a6, a7);
}

static u32 rd_hook(u32 reg)
{
	g_curreg = reg;
	return reg == 0x1708c ? g_cur : g_rdval;
}

/* hooks of our port: the vendor refs (fix_point_*) and the logging mocks */
static u32 w_div(u32 s, u32 a, u32 b)
{
	return (u32)t32o_call((t32o_fn)(void (*)(void))fix_point_div_32, s, a, b, 0, 0);
}
static u32 w_mult2(u32 s, u32 a, u32 b)
{
	return (u32)t32o_call((t32o_fn)(void (*)(void))fix_point_mult2_32, s, a, b, 0, 0);
}
static u32 w_mult3(u32 s, u32 a, u32 b, u32 c)
{
	return (u32)t32o_call((t32o_fn)(void (*)(void))fix_point_mult3_32, s, a, b, c, 0);
}
static void w_flicker(u32 ch, u8 *st)
{
	mlogadd(ch);
	mlogadd(T32O_PTR(st) - g_base);
}
static t32_u64 w_bil(t32_u64 x, t32_u64 e, t32_u64 en, u32 y0, u32 y1)
{
	return bil_core((u32)x, (u32)(x >> 32), (u32)e, (u32)(e >> 32), (u32)en, (u32)(en >> 32), y0, y1);
}
static const struct t32_ae_ops ops = { w_div, w_mult2, w_mult3, w_flicker, w_bil };

static struct {
	u32 ch, seed, rdval, rda, rdb, cur, d03a0, nx, ny, lum, divf, mode, shift, flick, rx0, rx1, ry0, ry1, qf, zarea, zsum;
	u64 tab[16];
	u32 y[16], p0, p1, h1, h2;
} g;

static void w16(u8 *p, u32 o, u32 v) { p[o] = (u8)v; p[o + 1] = (u8)(v >> 8); }
static void w32(u8 *p, u32 o, u32 v) { w16(p, o, v); w16(p, o + 2, v >> 16); }

static u8 *build(void)
{
	u8 *m;
	u32 i, ch = g.ch;

	t32o_reset();
	m = t32o_alloc(MEMSZ);
	fs = g.seed | 1;
	for (i = 0; i < MEMSZ; i++)
		m[i] = (u8)(frnd() >> 11);
	memset(m + O_OUT, 0, 0x100);
	g_base = T32O_PTR(m);
	g_rdval = g.rdval;
	g_rd[0] = g.rda; g_rd[1] = g.rdb; g_rdn = 0; g_cur = g.cur; g_curreg = 0;
	t32_data_d03a0[0] = (u8)g.d03a0;
	t32o_rd_hook = rd_hook;
	w16(m + O_TUNE, 2774, g.nx); w16(m + O_TUNE, 2770, g.ny); w16(m + O_TUNE, 2778, g.lum);
	w16(m + O_TUNE, 2944, g.divf); w16(m + O_TUNE, 1736, g.shift);
	w32(m + O_TUNE, 1684, g.mode); w32(m + O_TUNE, 1748, g.flick);
	/* zero zone areas: 1 = first row height, 2 = first column width, 3 = all row heights */
	if (g.zarea == 1 || g.zarea == 3)
		w16(m + O_BUF, 11796, 0);
	if (g.zarea == 2)
		w16(m + O_BUF, 11826, 0);
	if (g.zarea == 3)
		memset(m + O_BUF + 11796, 0, 30);
	/* zero e1..e3 sums: non-empty ROI with vsum == 0 */
	if (g.zsum)
		memset(m + O_BUF + 2048, 0, 3 * 900);
	m[O_Q + 1297] = (u8)g.qf;
	for (i = 0; i < 16; i++) {
		w32(m + O_TAB, 8 * i, (u32)g.tab[i]);
		w32(m + O_TAB, 8 * i + 4, (u32)(g.tab[i] >> 32));
		w16(m + O_Y, 2 * i, g.y[i]);
	}
	w32(m + O_TAB, 0x80, g.p0); w32(m + O_TAB, 0x84, g.p1);
	w16(m + O_OUT, 0, g.h1); w16(m + O_OUT, 8, g.h2);
	w32(m + O_CTX, 0, T32O_PTR(m + O_TUNE));
	w32(m + O_CTX, 4, T32O_PTR(m + O_BUF));
	w32(m + O_CTX, 64 + 16, T32O_PTR(m + O_HB));
	t32_data_e15a8[ch] = T32O_PTR(m + O_CTX);
	t32_data_e1598[ch] = T32O_PTR(m + O_CTX + 64);
	t32_data_e15d8[ch] = T32O_PTR(m + O_STAT);
	t32_data_e1588[ch] = T32O_PTR(m + O_Q);
	t32_data_e151c[ch] = (u8)g.rx0; t32_data_e1518[ch] = (u8)g.rx1;
	t32_data_e1524[ch] = (u8)g.ry0; t32_data_e1520[ch] = (u8)g.ry1;
	return m;
}

static u8 snap[2][MEMSZ];
static unsigned compared;

static void begin(int s) { side = s; mn[s] = 0; }
static void take(int s, u8 *m) { memcpy(snap[s], m, MEMSZ); }

static void same(const char *what, unsigned c, u32 rv, u32 pv)
{
	unsigned i, bad = rv != pv || mn[0] != mn[1] || memcmp(snap[0], snap[1], MEMSZ);

	for (i = 0; !bad && i < mn[0]; i++)
		bad = mlog[0][i] != mlog[1][i];
	compared++;
	if (bad) {
		printf("MISMATCH %s case %u: ret ref %08x ours %08x, mock calls ref %u ours %u\n",
		       what, c, rv, pv, mn[0], mn[1]);
		exit(1);
	}
}

static void gen_base(void)
{
	memset(&g, 0, sizeof(g));
	g.ch = rnd() & 3;
	g.seed = rnd();
	g.rdval = rnd();
	g.rda = rnd();
	g.rdb = rnd();
	g.cur = rnd();
	g.d03a0 = rnd() % 3 == 0;
	g.nx = rnd() % 16;
	g.ny = rnd() % 16;
}

/* the caller contract of t32_ae_get_hist: two reads of (ch, d03a0 ? 4 : 96) */
static void rdcheck(const char *what, unsigned c)
{
	u32 reg = g.d03a0 ? 4 : 96;

	if (g_rdn != 2 || g_rdreg[0] != g.ch || g_rdreg[1] != reg || g_rdreg[2] != g.ch || g_rdreg[3] != reg) {
		printf("MISMATCH %s case %u: vendor reads %u: (%u,%u) (%u,%u), expected 2 x (%u,%u)\n", what, c,
		       g_rdn, g_rdreg[0], g_rdreg[1], g_rdreg[2], g_rdreg[3], g.ch, reg);
		exit(1);
	}
}

static void c_hist(unsigned c)
{
	u8 *m;

	gen_base();
	begin(0); m = build();
	t32o_call((t32o_fn)(void (*)(void))tisp_ae_get_hist, g.ch, T32O_PTR(m + O_HB), 0, 0, 0);
	rdcheck("get_hist", c);
	take(0, m);
	begin(1); m = build();
	t32_ae_get_hist(m + O_BUF, m + O_HB, g.rda, g.rdb);
	take(1, m);
	same("get_hist", c, 0, 0);
}

static void c_inthist(unsigned c)
{
	u8 *m;
	u32 r, p;

	gen_base();
	begin(0); m = build();
	r = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_ae_interrupt_hist, g.ch, 0, 0, 0, 0);
	rdcheck("interrupt_hist", c);
	if (g_curreg != 0x1708c) {
		printf("MISMATCH interrupt_hist case %u: cur read from %x\n", c, g_curreg);
		exit(1);
	}
	take(0, m);
	begin(1); m = build();
	p = t32_ae_interrupt_hist(m + O_HB, g.cur, m + O_BUF, g.rda, g.rdb);
	take(1, m);
	same("interrupt_hist", c, r, p);
}

static void c_stats(unsigned c)
{
	u8 *m;

	gen_base();
	begin(0); m = build();
	t32o_call((t32o_fn)(void (*)(void))tisp_ae_get_statistics, g.ch, T32O_PTR(m + O_HB), 0, 0, 0);
	take(0, m);
	begin(1); m = build();
	t32_ae_get_statistics(m + O_TUNE, m + O_BUF, m + O_HB);
	take(1, m);
	same("get_statistics", c, 0, 0);
}

static void c_long(unsigned c)
{
	u8 *m;
	u32 i, r, p, asc = rnd() % 5 != 0, sh = rnd() % 64, sx = sh | ((rnd() & 1) ? rnd() << 16 : 0);
	u64 acc = rnd() % 1000, x;

	gen_base();
	for (i = 0; i < 16; i++) {
		acc += rnd() % 5000 + (rnd() & 1);
		g.tab[i] = asc ? acc : ((u64)rnd() << (rnd() % 20)) | rnd();
		g.y[i] = rnd() & 0xffff;
	}
	x = g.tab[rnd() % 16] << sh;
	switch (rnd() % 5) {
	case 1: x += 1; break;
	case 2: x -= 1; break;
	case 3: x = (u64)rnd() << 32 | rnd(); break;
	case 4: x = (g.tab[rnd() % 15] + g.tab[rnd() % 15]) / 2 << sh; break;
	}
	begin(0); m = build();
	r = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_ae_long_target, (u32)x, (u32)(x >> 32),
			   T32O_PTR(m + O_TAB), T32O_PTR(m + O_Y), sx);
	take(0, m);
	begin(1); m = build();
	p = t32_ae_long_target(x, m + O_TAB, m + O_Y, sx, &ops);
	take(1, m);
	same("long_target", c, r, p);
}

static void c_conv(unsigned c)
{
	u8 *m;
	u32 r, p, k = rnd(), a4 = rnd() % 25 | ((rnd() & 1) ? rnd() << 16 : 0), tot = rnd() % 7000;

	gen_base();
	if (rnd() % 4) {
		g.p0 = rnd() % 3000; g.p1 = rnd() % 3000; g.h1 = rnd() % 3000; g.h2 = rnd() % 3000;
	} else {
		g.p0 = rnd(); g.p1 = rnd(); g.h1 = rnd() & 0xffff; g.h2 = rnd() & 0xffff;
	}
	begin(0); m = build();
	t32o_arg5 = tot;
	r = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_ae_calc_convergence_speed,
			   T32O_PTR(m + O_TAB + 0x80), T32O_PTR(m + O_OUT), T32O_PTR(m + O_OUT + 8), k, a4);
	take(0, m);
	begin(1); m = build();
	p = t32_ae_calc_convergence_speed(m + O_TAB + 0x80, m + O_OUT, m + O_OUT + 8, k, a4, tot, &ops);
	take(1, m);
	same("calc_convergence_speed", c, r, p);
}

static void c_wmean(unsigned c)
{
	u8 *m;
	u32 r, p, a3 = rnd(), a4 = rnd(), mean = 0, ratio[2] = { 0, 0 }, wsum[2] = { 0, 0 };
	struct t32_ae_ctx pc;

	gen_base();
	switch (rnd() % 6) {
	case 0: g.lum = 253 + rnd() % 4; break;
	case 1: g.lum = rnd() % 600; break;
	default: g.lum = rnd() & 0xffff;
	}
	g.divf = (c % 13 == 0) ? 0 : (rnd() % 3 ? rnd() & 0xffff : rnd() % 64 + 1);
	g.mode = (rnd() & 1) ? 1 : rnd() % 3;
	g.flick = (rnd() & 1) ? 1 : rnd() % 3;
	g.shift = rnd() % 32;
	g.rx0 = rnd() % 16; g.rx1 = rnd() % 16; g.ry0 = rnd() % 16; g.ry1 = rnd() % 16;
	g.qf = rnd() & 0xff;
	g.zarea = c % 11 == 0 ? 1 + (c / 11) % 3 : 0;
	g.zsum = c % 7 == 3;
	begin(0); m = build();
	t32o_arg5 = T32O_PTR(m + O_OUT + 0x30);
	r = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_ae_weight_mean, g.ch, T32O_PTR(m + O_OUT + 0x10),
			   T32O_PTR(m + O_OUT + 0x20), a3, a4);
	take(0, m);
	begin(1); m = build();
	pc.tune = m + O_TUNE; pc.buf = m + O_BUF;
	pc.roi_x0 = (u8)g.rx0; pc.roi_x1 = (u8)g.rx1; pc.roi_y0 = (u8)g.ry0; pc.roi_y1 = (u8)g.ry1;
	p = t32_ae_weight_mean(&pc, m + O_STAT, m + O_Q, g.ch, &mean, ratio, a3, a4, wsum, &ops);
	w32(m + O_OUT, 0x10, mean);
	w32(m + O_OUT, 0x20, ratio[0]); w32(m + O_OUT, 0x24, ratio[1]);
	w32(m + O_OUT, 0x30, wsum[0]); w32(m + O_OUT, 0x34, wsum[1]);
	take(1, m);
	same("weight_mean", c, r, p);
}

int main(void)
{
	unsigned n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 3000, c;

	for (c = 0; c < n; c++) {
		c_hist(c);
		c_inthist(c);
		c_stats(c);
		c_long(c);
		c_conv(c);
		c_wmean(c);
	}
	printf("t32_aes_oracle: %u compared, 0 mismatches\n", compared);
	return 0;
}
