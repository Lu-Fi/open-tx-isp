// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: LSC register / LUT writer and its entry points.  Runs the unmodified
 * vendor functions (Matt Davis' recovered C, PR #15 @ 2e2e06bc: tisp_lsc_write_all_reg,
 * tisp_lsc_ct_update, tisp_lsc_tgain_update, tisp_lsc_irq_frame_done, with
 * tisp_reg_lut_write and the oracle's tisp_reg_write of ref/oa_reg_write.c) and our ports
 * from driver/t32/tx_isp_t32_lscct.h from the same state and compares everything.
 *
 * State as in t32_lscm_oracle (setup copied from it): the vendor side is built by
 * tisp_lsc_init, ours from the vendor's section and current values.  Per case: random
 * block / geometry / bayer, shadow mode d03a0 0 or 1, sensor number 1 or 2, register
 * list DMA busy or idle (0x22014), wdr, the trigger / waiter flags before the call.
 * Compared: the LSC list image (4096 words, canary filled), the main shadow image
 * (8192 words: d03a0 = 1 LUT writes of the many-sensor path), the register write log
 * (address / shadow tag, value, order), the hook sequence with its position in the write
 * log (both spin locks with their flags, completion init / wait / complete with their
 * object and timeout, isp_printf), trigger / waiter flags, frame counter, current
 * values (32 bytes), info, return values, decoy channels 1..3.
 * The vendor's tisp_single_lsc_rcfg_cfg_trig is an env.c stub: its call is seen as the
 * trigger flag going 1 -> 0 in tisp_lsc_irq_frame_done (the only place that clears it
 * there, right after the call).  The mutex (env.c stub) is not observed.
 * The image after tisp_lsc_init is checked against t32_lscct_img_fill.
 * Exit 0 = all equal, 1 = first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_lscct.h"

#define N1	(3 * T32_LSC_RING_STRIDE)
#define N2	(3 * T32_LSC_TABLE_STRIDE)
#define NIMG	4096
#define NMAIN	8192

extern u32 t32_data_e17e0[4];
extern u8 t32_data_e17d0[0x80];
extern u8 t32_small_bss[64];
extern u8 t32o_sensor_num;
extern char t32_data_c94d8[4];
unsigned char t32_data_bf4d0[64];
unsigned char t32_data_c958c[64];

extern u64 tisp_lsc_write_all_reg(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_lsc_irq_frame_done(u32 a0, u32 a1, u32 a2, u32 a3);

#define V_FRAMES	((u32 *)(void *)t32_data_e17d0)
#define V_WAITING	(t32_data_e17d0[0x20])
#define V_COMP		(T32O_PTR(t32_data_e17d0 + 0x24))
#define V_ARMED		(t32_data_e17d0[0x3c])

static u32 rs = 0x7e15c3d1u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static void put32(u8 *p, u32 v)
{
	p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
}

static unsigned int compared, refused, pool_skip, nbusy, nmesh, nmulti, nshadow, ntrig, nct_upd, nfill;
#define FAIL(...) do { printf("MISMATCH " __VA_ARGS__); printf("\n"); \
		       printf("t32_lscct_oracle: %u compared, 1 mismatch\n", compared); exit(1); } while (0)

/* ---- hook sequence: vendor (side 0) and ours (side 1) ---- */
enum { EV_LOCK = 1, EV_UNLOCK, EV_CLOCK, EV_CUNLOCK, EV_CINIT, EV_CWAIT, EV_COMPLETE, EV_PRINT, EV_TRIG };
#define NEV 64
struct ev { u32 code, arg, pos; };
static struct ev evl[2][NEV];
static unsigned int nev[2];
static u32 lock_tok;

static struct t32o_log olog;		/* our write log, same format as t32o_log */
static u32 omain[NMAIN];

static void ev_add(unsigned int side, u32 code, u32 arg)
{
	if (nev[side] < NEV) {
		evl[side][nev[side]].code = code;
		evl[side][nev[side]].arg = arg;
		evl[side][nev[side]].pos = side ? olog.n : t32o_log.n;
	}
	nev[side]++;
}

static u32 vctx_lock;	/* vendor ctx + 16 */

/* vendor-side OS stubs (weak in env.c) */
u64 __private_spin_lock_irqsave(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	lock_tok++;
	if (a1)
		*(u32 *)T32O_HOST(a1) = 0x1000u + lock_tok;
	if (a0 == T32O_PTR(t32_small_bss))
		ev_add(0, EV_LOCK, 0x1000u + lock_tok);
	else
		ev_add(0, EV_CLOCK, a0 == vctx_lock ? 0x1000u + lock_tok : 0xbad);
	return 0;
}

u64 private_spin_unlock_irqrestore(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	if (a0 == T32O_PTR(t32_small_bss))
		ev_add(0, EV_UNLOCK, a1);
	else
		ev_add(0, EV_CUNLOCK, a0 == vctx_lock ? a1 : 0xbad);
	return 0;
}

u64 private_init_completion(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	ev_add(0, EV_CINIT, a0 == V_COMP ? 1 : 0xbad);
	return 0;
}

u64 private_wait_for_completion_timeout(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	ev_add(0, EV_CWAIT, a0 == V_COMP ? a1 : 0xbad);
	return 1;
}

u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	t32o_log.printfs++;
	ev_add(0, EV_PRINT, a0 == 0 && a1 == T32O_PTR(t32_data_c94d8) ? 1 : 0xbad);
	return 0;
}

/* ---- our hooks ---- */
static u32 busy_val;
static u32 rd_hook(u32 reg)
{
	return reg == T32_LSCCT_BUSY_REG ? busy_val : 0x5a000000u | reg;
}

static void olog_add(u32 reg, u32 val)
{
	if (olog.n < T32O_MAX_WRITES) {
		olog.w[olog.n].reg = reg;
		olog.w[olog.n].val = val;
	}
	olog.n++;
}

static unsigned int o_reads, o_mlock;
static u32 o_read(void *priv, u32 reg) { (void)priv; o_reads++; return rd_hook(reg); }
static void o_reg_write(void *priv, u8 ch, u32 reg, u32 val)
{
	(void)priv;
	if (t32_data_d03a0[0] == 1)
		olog_add(0x80000000u | (ch & 7u) << 24 | (reg & 0xffffffu), val);
	else
		olog_add(reg, val);
}
static void o_reg_lut_write(void *priv, u8 ch, u32 reg, u32 val, u32 off)
{
	(void)priv; (void)ch;
	if (t32_data_d03a0[0] == 1)
		omain[reg + off] = val;
	else
		olog_add(reg, val);
}
static u32 o_sensor_num(void *priv) { (void)priv; return t32o_sensor_num; }
static void o_mutex_lock(void *priv) { (void)priv; o_mlock++; }
static void o_mutex_unlock(void *priv) { (void)priv; o_mlock--; }
static unsigned long o_lock(void *priv) { (void)priv; lock_tok++; ev_add(1, EV_LOCK, 0x1000u + lock_tok); return 0x1000u + lock_tok; }
static void o_unlock(void *priv, unsigned long f) { (void)priv; ev_add(1, EV_UNLOCK, (u32)f); }
static unsigned long o_ch_lock(void *priv, u8 ch)
{
	(void)priv;
	lock_tok++;
	ev_add(1, EV_CLOCK, ch == 0 ? 0x1000u + lock_tok : 0xbad);
	return 0x1000u + lock_tok;
}
static void o_ch_unlock(void *priv, u8 ch, unsigned long f) { (void)priv; ev_add(1, EV_CUNLOCK, ch == 0 ? (u32)f : 0xbad); }
static void o_comp_init(void *priv) { (void)priv; ev_add(1, EV_CINIT, 1); }
static long o_comp_wait(void *priv, u32 t) { (void)priv; ev_add(1, EV_CWAIT, t); return 1; }
static unsigned int o_completes;
static void o_complete(void *priv) { (void)priv; o_completes++; }
static void o_print(void *priv) { (void)priv; ev_add(1, EV_PRINT, 1); }
static unsigned int o_trigs;
static void o_cfg_trig(void *priv, u8 ch) { (void)priv; o_trigs += ch == 0 ? 1 : 100; }

static const struct t32_lscct_ops oops = {
	.reg_read = o_read, .reg_write = o_reg_write, .reg_lut_write = o_reg_lut_write,
	.sensor_num = o_sensor_num, .mutex_lock = o_mutex_lock, .mutex_unlock = o_mutex_unlock,
	.lock = o_lock, .unlock = o_unlock, .ch_lock = o_ch_lock, .ch_unlock = o_ch_unlock,
	.comp_init = o_comp_init, .comp_wait = o_comp_wait, .complete = o_complete,
	.print_wait = o_print, .cfg_trig = o_cfg_trig,
};

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
	u8 *base, *ctx, *cur, *regsh, *v1, *v2, *linfo, *main;
} V;
static u8 oblk[T32_LSC_BLOCK_BYTES + 16];
static struct t32_lsc_ctx oc;
static struct t32_lsc_cur ocur;
static struct t32_lsc_lut olut;
static u32 oimg[NIMG];
static struct t32_lscct ow;

static int setup(const u8 *blk, u32 w, u32 h, u32 bayer)
{
	static u32 fimg[NIMG];
	struct t32_lsc g;
	u8 *info, *dbase, *vsec;
	unsigned int i;
	const u32 *sh;

	if (t32_lsc_geometry(blk, &g, w, h)) {
		refused++;
		return 1;
	}
	t32o_reset();
	t32o_rd_hook = rd_hook;
	busy_val = 0;
	t32o_sensor_num = 1;
	V.base = t32o_alloc(0x8000);
	info = t32o_alloc(64);
	V.linfo = t32o_alloc(64);
	V.v1 = t32o_alloc(N1 + 8);
	V.v2 = t32o_alloc(N2 + 8);
	V.regsh = t32o_alloc(NIMG * 4);
	V.main = t32o_alloc(NMAIN * 4);
	dbase = t32o_alloc(0x400);
	if (!V.base || !info || !V.linfo || !V.v1 || !V.v2 || !V.regsh || !V.main || !dbase) {
		pool_skip++;
		return 1;
	}
	memset(V.v2 + N2 + 2, 0xcc, 6);
	put32(dbase + 200, T32O_PTR(V.main));
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
	for (i = 0; i < NIMG; i++)
		((u32 *)V.regsh)[i] = 0xc0de0000u ^ i;
	t32o_call((t32o_fn)(void (*)(void))tisp_lsc_init, 0, T32O_PTR(info), 0, 0, 0);
	if (!t32_data_e17e0[0]) {
		fprintf(stderr, "t32_lscct_oracle: tisp_lsc_init left no context\n");
		exit(2);
	}
	V.ctx = T32O_HOST(t32_data_e17e0[0]);
	V.cur = T32O_HOST(t32_le32(V.ctx + 4));
	vctx_lock = t32_data_e17e0[0] + 16;
	vsec = T32O_HOST(t32_le32(V.ctx));

	memset(&oc, 0, sizeof(oc));
	memcpy(&ocur, V.cur, sizeof(ocur));
	memcpy(oblk, vsec, 120);
	memcpy(oblk + 120, blk + 120, T32_LSC_BLOCK_BYTES - 120);
	if (t32_lsc_setup(oblk, &oc.s, &oc.wk, w, h, 5000)) {
		fprintf(stderr, "t32_lscct_oracle: our setup failed\n");
		exit(2);
	}
	olut.p1 = oblk + 120;
	olut.p2 = oblk + T32_LSC_TABLES;
	oc.sec = oblk;
	oc.cur = &ocur;
	oc.width = w;
	oc.height = h;
	oc.bayer = bayer;
	oc.lut = &olut;

	/* the image tisp_lsc_init (write_all_reg(ch, 0, 0, 1)) leaves vs t32_lscct_img_fill */
	sh = (const u32 *)V.regsh;
	for (i = 0; i < NIMG; i++)
		fimg[i] = 0xc0de0000u ^ i;
	oc.img = fimg;
	t32_lscct_img_fill(&oc, 0);
	for (i = 0; i < NIMG; i++) {
		int vw = sh[i] != (0xc0de0000u ^ i), ow2 = fimg[i] != (0xc0de0000u ^ i);

		if (i == 7 || i == 14 || i == 15) {	/* the constants (tisp_lsc_wake_up; tisp_lsc_init writes some) */
			if ((vw && sh[i] != fimg[i]) || fimg[i] != (i == 7 ? 1u : i == 14 ? 0u : 0x04000400u))
				FAIL("init image[%u] vendor %08x fill %08x", i, sh[i], fimg[i]);
			continue;
		}
		if (i == 3608) {		/* the vendor's close write behind its list */
			if (!vw || ow2)
				FAIL("init image[3608] vendor %08x fill %08x", sh[i], fimg[i]);
			continue;
		}
		if (vw != ow2 || sh[i] != fimg[i])
			FAIL("init image[%u] vendor %08x fill %08x (p65 %u cells %u)", i, sh[i], fimg[i], oblk[65], oc.s.cells);
	}
	nfill++;

	for (i = 0; i < NIMG; i++)
		((u32 *)V.regsh)[i] = oimg[i] = 0xdeadbeefu ^ i;
	for (i = 0; i < NMAIN; i++)
		((u32 *)V.main)[i] = omain[i] = 0xa5a50000u ^ i;
	oc.img = oimg;
	memset(&ow, 0, sizeof(ow));
	ow.lsc = &oc;
	ow.ops = &oops;
	ow.shadow = t32_data_d03a0;
	memset(&olog, 0, sizeof(olog));
	t32o_log.n = 0;
	nev[0] = nev[1] = 0;
	o_completes = o_trigs = 0;
	t32o_complete_n = 0;
	t32o_decoy_arm();
	return 0;
}

/* random environment for one call: shadow mode, sensors, busy, wdr, flags (both sides) */
static void env_random(void)
{
	u32 wdr = rnd() % 3 ? 0 : rnd() % 4 ? 1 : 2, c4 = rnd() & 1;

	t32_data_d03a0[0] = (u8)(rnd() % 3 ? 0 : rnd() % 5 ? 1 : 2);	/* 2: not the shadow mode, but nonzero */
	t32o_sensor_num = (u8)(rnd() % 3 ? 1 : 2);
	busy_val = rnd() % 3 ? 0 : (1u << (16 + rnd() % 2));
	put32(V.cur + 28, wdr);
	ocur.wdr = wdr;
	if (rnd() % 4 == 0) {		/* cur word 4 (vendor: Bayer >= 4) decides the cells of byte 65 */
		put32(V.cur + 16, c4);
		ocur.rsvd1[1] = c4;
	}
	V_ARMED = ow.armed = (u8)(rnd() % 3 ? rnd() & 1 : rnd());
	V_WAITING = ow.waiting = (u8)(rnd() % 3 ? rnd() & 1 : rnd());
	V_FRAMES[0] = ow.frames[0] = rnd() % 4 ? rnd() : 0xffff;
	nbusy += busy_val != 0;
	nmulti += t32o_sensor_num != 1;
	nshadow += t32_data_d03a0[0] == 1;
}

static void cmp_all(const char *what, u32 rv, u32 ro)
{
	const u32 *sh = (const u32 *)V.regsh, *mn = (const u32 *)V.main;
	const u8 *in = T32O_HOST(t32_le32(V.ctx + 8));
	unsigned int i;

	compared++;
	if (rv != ro)
		FAIL("%s return vendor %u ours %u", what, rv, ro);
	for (i = 0; i < NIMG; i++)
		if (sh[i] != oimg[i])
			FAIL("%s image[%u] vendor %08x ours %08x (d03a0 %u sensors %u p65 %u cells %u)", what, i, sh[i],
			     oimg[i], t32_data_d03a0[0], t32o_sensor_num, oblk[65], oc.s.cells);
	for (i = 0; i < NMAIN; i++)
		if (mn[i] != omain[i])
			FAIL("%s main shadow[%u] vendor %08x ours %08x", what, i, mn[i], omain[i]);
	if (t32o_log.n != olog.n)
		FAIL("%s %u register writes by the vendor, %u by ours (d03a0 %u sensors %u)", what, t32o_log.n, olog.n,
		     t32_data_d03a0[0], t32o_sensor_num);
	for (i = 0; i < olog.n && i < T32O_MAX_WRITES; i++)
		if (t32o_log.w[i].reg != olog.w[i].reg || t32o_log.w[i].val != olog.w[i].val)
			FAIL("%s write %u vendor %08x = %08x ours %08x = %08x", what, i, t32o_log.w[i].reg,
			     t32o_log.w[i].val, olog.w[i].reg, olog.w[i].val);
	if (nev[0] != nev[1]) {
		for (i = 0; i < nev[0] || i < nev[1]; i++)
			printf("  ev %u: vendor %u/%x@%u ours %u/%x@%u\n", i, evl[0][i].code, evl[0][i].arg, evl[0][i].pos,
			       evl[1][i].code, evl[1][i].arg, evl[1][i].pos);
		FAIL("%s %u hook events by the vendor, %u by ours", what, nev[0], nev[1]);
	}
	for (i = 0; i < nev[0] && i < NEV; i++)
		if (evl[0][i].code != evl[1][i].code || evl[0][i].arg != evl[1][i].arg || evl[0][i].pos != evl[1][i].pos)
			FAIL("%s hook %u vendor %u/%x@%u ours %u/%x@%u", what, i, evl[0][i].code, evl[0][i].arg,
			     evl[0][i].pos, evl[1][i].code, evl[1][i].arg, evl[1][i].pos);
	if (t32o_complete_n != o_completes || (t32o_complete_n && t32o_complete_arg != V_COMP))
		FAIL("%s completes vendor %u ours %u", what, t32o_complete_n, o_completes);
	if (V_ARMED != ow.armed || V_WAITING != ow.waiting || V_FRAMES[0] != ow.frames[0])
		FAIL("%s flags vendor armed %u waiting %u frames %u ours %u %u %u", what, V_ARMED, V_WAITING,
		     V_FRAMES[0], ow.armed, ow.waiting, ow.frames[0]);
	if (o_mlock)
		FAIL("%s mutex not released", what);
	{
		const u32 f[8] = { ocur.rsvd0[0], ocur.rsvd0[1], ocur.ct, ocur.rsvd1[0], ocur.rsvd1[1],
				   ocur.tgain, ocur.tgain_s, ocur.wdr };

		for (i = 0; i < 8; i++)
			if (t32_le32(V.cur + 4 * i) != f[i])
				FAIL("%s cur word %u vendor %08x ours %08x", what, i, t32_le32(V.cur + 4 * i), f[i]);
	}
	if (t32_le32(in) != oc.s.r2_center || t32_le32(in + 4) != oc.s.cells || in[10] != oc.s.strength ||
	    in[14] != oc.s.strength_wdr || in[15] != oc.s.rem_y || in[16] != oc.s.rem_x || in[17] != oc.s.rows ||
	    in[18] != oc.s.cols || in[19] != oc.s.zone)
		FAIL("%s info differs", what);
	for (i = 0; i < 6; i++)
		if (in[20 + i] != oc.s.flags[i])
			FAIL("%s info flag %u differs", what, i);
	if (t32o_decoy_check())
		FAIL("%s a decoy channel changed", what);
}

static const u32 sz[4][2] = { { 1920, 1080 }, { 1280, 720 }, { 2560, 1440 }, { 640, 480 } };
static const u8 modes[] = { 0, 1, 2, 3, 4, 0xff, 2, 0, 1, 3 };
static const u8 flags3[] = { 0, 1, 1, 2, 0xff };

static void case_war(const u8 *blk, u32 w, u32 h, u32 bayer)
{
	u8 mode = modes[rnd() % sizeof(modes)], so = flags3[rnd() % sizeof(flags3)], me = flags3[rnd() % sizeof(flags3)];
	u32 rv, ro;

	if (setup(blk, w, h, bayer))
		return;
	env_random();
	nmesh += me == 1;
	lock_tok = 0;
	rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_lsc_write_all_reg, 0,
			    mode | (rnd() & 0xffffff00u), so | (rnd() & 0xffffff00u), me | (rnd() & 0xffffff00u), 0);
	lock_tok = 0;
	ro = (u32)t32_lscct_write_all_reg(&ow, 0, mode, so, me);
	cmp_all("write_all_reg", rv, ro);
}

static void case_ct(const u8 *blk, u32 w, u32 h, u32 bayer)
{
	u32 old = rnd() % 4 ? 1500 + rnd() % 6000 : rnd() % 20000, ct, rv, ro;
	u32 th = t32_le32(blk + 12);

	ct = rnd() % 3 ? 1500 + rnd() % 6000 : rnd() % 2 ? old + th : old - th + rnd() % 3;
	if (setup(blk, w, h, bayer))
		return;
	env_random();
	put32(V.cur + 8, old);
	ocur.ct = old;
	lock_tok = 0;
	rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_lsc_ct_update, 0, rnd(), ct, rnd(), 0);
	lock_tok = 0;
	ro = (u32)t32_lscct_ct_update(&ow, 0, ct);
	nct_upd += t32o_log.n || nev[0] || ocur.ct == ct;
	cmp_all("ct_update", rv, ro);
}

static void case_tgain(const u8 *blk, u32 w, u32 h, u32 bayer)
{
	u32 g = rnd() % 4 ? rnd() % (12u << 16) : rnd(), rv, ro;

	if (setup(blk, w, h, bayer))
		return;
	env_random();
	lock_tok = 0;
	rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_lsc_tgain_update, 0, rnd(), g, rnd(), 0);
	lock_tok = 0;
	ro = (u32)t32_lscct_tgain_update(&ow, 0, g);
	cmp_all("tgain_update", rv, ro);
}

static void case_fd(const u8 *blk, u32 w, u32 h, u32 bayer)
{
	u32 rv, ro;
	u8 armed;

	if (setup(blk, w, h, bayer))
		return;
	env_random();
	armed = V_ARMED;
	lock_tok = 0;
	rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_lsc_irq_frame_done, 0, rnd(), rnd(), rnd(), 0);
	if (armed == 1 && V_ARMED == 0)		/* the vendor's tisp_single_lsc_rcfg_cfg_trig ran */
		ev_add(0, EV_TRIG, 0);
	lock_tok = 0;
	ro = (u32)t32_lscct_frame_done(&ow, 0);
	if (o_trigs) {
		ev_add(1, EV_TRIG, o_trigs - 1);
		ntrig++;
	}
	cmp_all("frame_done", rv, ro);
}

/* the driver's sequence: ct update arms the list, the next frame done runs it */
static void case_seq(const u8 *blk, u32 w, u32 h, u32 bayer)
{
	u32 rv, ro, ct = 1500 + rnd() % 6000, k;

	if (setup(blk, w, h, bayer))
		return;
	t32_data_d03a0[0] = 0;
	put32(V.cur + 8, 20000);
	ocur.ct = 20000;
	for (k = 0; k < 3; k++) {
		lock_tok = 0;
	rv = (u32)t32o_call((t32o_fn)(void (*)(void))(k == 1 ? tisp_lsc_tgain_update : tisp_lsc_ct_update), 0,
				    0, k == 1 ? 0x30000 : ct, 0, 0);
		lock_tok = 0;
		ro = (u32)(k == 1 ? t32_lscct_tgain_update(&ow, 0, 0x30000) : t32_lscct_ct_update(&ow, 0, ct));
		cmp_all("seq update", rv, ro);
		lock_tok = 0;
	rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_lsc_irq_frame_done, 0, 0, 0, 0, 0);
		lock_tok = 0;
	ro = (u32)t32_lscct_frame_done(&ow, 0);
		cmp_all("seq frame_done", rv, ro);
		ct += 3000;
	}
}

int main(void)
{
	const unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 600;
	static u8 blk[T32_LSC_BLOCK_BYTES + 16];
	unsigned int c;

	for (c = 0; c < n; c++) {
		u32 zi = rnd() % 4, bayer = rnd() % 8, k = rnd() % 9;

		gen_blk(blk);
		if (k < 4)
			case_war(blk, sz[zi][0], sz[zi][1], bayer);
		else if (k < 6)
			case_ct(blk, sz[zi][0], sz[zi][1], bayer);
		else if (k < 7)
			case_tgain(blk, sz[zi][0], sz[zi][1], bayer);
		else if (k < 8)
			case_fd(blk, sz[zi][0], sz[zi][1], bayer);
		else
			case_seq(blk, sz[zi][0], sz[zi][1], bayer);
	}
	printf("t32_lscct_oracle: %u compared (%u refused, %u pool-skipped), %u init images, %u busy, %u mesh, "
	       "%u many-sensor, %u shadow, %u list triggers, %u ct updates, 0 mismatches\n",
	       compared, refused, pool_skip, nfill, nbusy, nmesh, nmulti, nshadow, ntrig, nct_upd);
	return 0;
}
