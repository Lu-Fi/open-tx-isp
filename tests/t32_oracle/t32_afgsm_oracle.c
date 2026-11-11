// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: the AF / GSM blocks as the driver runs them
 * (driver/t32/tx_isp_t32_afgsm.h) against Matt Davis' recovered tisp_af_* and
 * tisp_gsm_* (ref/, PR #15 @ 2e2e06bc), from the vendor's module .data on.
 *
 * Per case both sides start from the load state: the vendor objects
 * t32_data_d5c60 / d5c90 / d6014 (AF attribute records and gain tables, the
 * values of data.c, laid out as the vendor .data) and d6e00 .. d7e28 (GSM),
 * the .bss zero.  Init equivalence: tisp_af_init(0, {w, h}) on a tuning
 * block whose + 0xec30 holds the AF section and tisp_gsm_init(0, {w, h})
 * against t32_afg_af_init / t32_afg_gsm_init on the same section: register
 * writes (address, value, order; the four DMA slot addresses relative to the
 * side's own DMA buffer), par / regs / blk / ctx / dma words, AF globals,
 * the whole GSM context, the irq numbers and event callbacks the vendor
 * registers.  Then frames: bit 6 (AF statistics from a random DMA slot, the
 * vendor's event 15 callback), bit 7 (GSM RAM read, status 1 or not, event
 * 16 callback), bit 63, gain updates and the AF / GSM API calls of the
 * tuning ids (get / set attr, weight, metric, statis info; GSM api set /
 * get), compared after every step.  Sections: every T32 IQ bin (section 24)
 * and random ones (mode 0 and the gain mode with rows inside the vendor
 * tables).
 */
#include <dirent.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_afgsm.h"

#define MAXW	600
#define PB	T32_AF_PAR_BYTES
#define SB	T32_AF_REGS_BYTES
#define BB	T32_AF_BLK_BYTES
#define TUNB	(T32_AF_PAR_IQ_OFF + PB)

/* ---- vendor data: AF .data in vendor order, GSM .data / .bss as in t32_gsm_oracle ---- */

struct afdata {
	u8 d5c60[48];
	u8 d5c90[900];
	u8 d6014[T32_AFG_ROWS * 48U];	/* 1020 vendor bytes, then zero (vendor: d6410 ..) */
};
struct afdata t32_afg_vdata __attribute__((aligned(16)));
__asm__(
".globl t32_data_d5c60\n.set t32_data_d5c60, t32_afg_vdata+0\n"
".globl t32_data_d5c90\n.set t32_data_d5c90, t32_afg_vdata+48\n"
".globl t32_data_d6014\n.set t32_data_d6014, t32_afg_vdata+948\n"
);

struct garena {
	u8 pre[64];
	struct t32_gsm_ctx ctx;
	u8 mid[64];
	struct t32_gsm_ram ram;
	u8 post[64];
};
_Static_assert(offsetof(struct garena, ctx.api.zone) == 1100, "zone");
_Static_assert(offsetof(struct garena, ctx.api.bound) == 1120, "bound");
_Static_assert(offsetof(struct garena, ctx.cfg) == 4200, "cfg");
_Static_assert(offsetof(struct garena, ram) == 4268, "ram");
_Static_assert(offsetof(struct garena, ram.ram_a) == 5308, "ram_a");
struct garena t32_afg_garena __attribute__((aligned(16)));
__asm__(
".globl t32_data_d6e00\n.set t32_data_d6e00, t32_afg_garena+64\n"
".globl t32_data_d6e04\n.set t32_data_d6e04, t32_afg_garena+68\n"
".globl t32_data_d6e08\n.set t32_data_d6e08, t32_afg_garena+72\n"
".globl t32_data_d6e0c\n.set t32_data_d6e0c, t32_afg_garena+76\n"
".globl t32_data_d720c\n.set t32_data_d720c, t32_afg_garena+1100\n"
".globl t32_data_d7220\n.set t32_data_d7220, t32_afg_garena+1120\n"
".globl t32_data_d7228\n.set t32_data_d7228, t32_afg_garena+1128\n"
".globl t32_data_d7628\n.set t32_data_d7628, t32_afg_garena+2152\n"
".globl t32_data_d7a28\n.set t32_data_d7a28, t32_afg_garena+3176\n"
".globl t32_data_d7e28\n.set t32_data_d7e28, t32_afg_garena+4200\n"
".globl t32_data_e2900\n.set t32_data_e2900, t32_afg_garena+4268\n"
".globl t32_data_e2910\n.set t32_data_e2910, t32_afg_garena+4284\n"
".globl t32_data_e2d10\n.set t32_data_e2d10, t32_afg_garena+5308\n"
);

/* data.c:24773.. (d5c60), 24785.. (d5c90), 25010.. (d6014), 25901 / 25903 / 26160 / 26165 / 26166 / 26935 (GSM) */
static const u32 v_d5c60[12] = {
	0x0, 0x0, 0x2a0001, 0xc80016, 0x3010013, 0x3a0f0f, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
};
static const u32 v_d5c90[225] = {
#include "t32_afgsm_d5c90.inc"
};
static const u32 v_d6014[255] = {
#include "t32_afgsm_d6014.inc"
};

#define WEAK __attribute__((weak))
extern u8 t32_data_dd6c0[0x400];
WEAK u32 t32_data_e1600[8];
WEAK u32 t32_data_e1610[8];
WEAK u32 t32_data_e1620[8];
WEAK u32 t32_data_e15f0[8];
WEAK u8 t32_data_bf410[16];
WEAK u8 t32_data_c8728[16];
WEAK u8 t32_data_ca288[64], t32_data_ca224[64], t32_data_ca244[64], t32_data_ca268[64], t32_data_ca274[64];
WEAK u8 t32_data_bfe70[64], t32_data_bfe88[64];

extern u64 tisp_af_init(u32, u32, u32, u32);
extern u64 tisp_af_interrupt_static(u32, u32, u32, u32);
extern u64 tisp_af_calc_process(u32, u32, u32, u32);
extern u64 tisp_af_tgain_update(u32, u32, u32, u32);
extern u64 tisp_af_api_set_weight(u32, u32, u32, u32);
extern u64 tisp_af_api_get_weight(u32, u32, u32, u32);
extern u64 tisp_af_api_get_metric(u32, u32, u32, u32);
extern u64 tisp_af_api_get_attr(u32, u32, u32, u32);
extern u64 tisp_af_api_set_attr(u32, u32, u32, u32);
extern u64 tisp_af_api_get_statis_info(u32, u32, u32, u32);
extern u64 tisp_gsm_init(u32, u32, u32, u32);
extern u64 tisp_gsm_interrupt_hist(u32, u32, u32, u32);
extern u64 tisp_gsm_irq_frmae_done(u32, u32, u32, u32);
extern u64 tisp_gsm_process(u32, u32, u32, u32);
extern u64 tisp_gsm_api_set(u32, u32, u32, u32);
extern u64 tisp_gsm_api_get(u32, u32, u32, u32);

#define RC(f, a, b, c, d) t32o_call((t32o_fn)(void (*)(void))(f), (u32)(a), (u32)(b), (u32)(c), (u32)(d), 0)

static u32 rs = 0x3af65du;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}
static void rfill(u8 *p, u32 n)
{
	while (n--)
		*p++ = (u8)rnd();
}
static void put16(u8 *p, u32 o, u32 v) { p[o] = (u8)v; p[o + 1] = (u8)(v >> 8); }
static u32 get32(const u8 *p, u32 o) { u32 v; memcpy(&v, p + o, 4); return v; }
static void put32(u8 *p, u32 o, u32 v) { memcpy(p + o, &v, 4); }

static unsigned int compared, cases, bins, frames;
static char dmsg[400];
static void die(const char *what)
{
	printf("MISMATCH %s: %s\n", what, dmsg);
	printf("t32_afgsm_oracle: %u compared, 1 mismatches\n", compared);
	exit(1);
}
#define DIE(what, ...) do { snprintf(dmsg, sizeof(dmsg), __VA_ARGS__); die(what); } while (0)

/* ---- register reads: one model for both sides ---- */
static u32 rd_seed, rd_rs, rd_slot, rd_gsm, rd_n, rd_log[1100];
static u32 rd_model(u32 reg)
{
	if (rd_n < 1100)
		rd_log[rd_n] = reg;
	rd_n++;
	if (reg == T32_AF_REG_STAT_RD_0 || reg == T32_AF_REG_STAT_RD_N)
		return rd_slot;
	if (reg == T32_GSM_REG_STATUS)
		return rd_gsm;
	rd_rs = rd_rs * 1664525u + 1013904223u;
	return rd_rs ^ (rd_rs >> 15) ^ (rd_rs << 9);
}
static u32 rd_ref_log[1100], rd_ref_n;
static void rd_arm(void) { rd_rs = rd_seed; rd_n = 0; }
static void rd_keep(void) { memcpy(rd_ref_log, rd_log, sizeof(rd_log)); rd_ref_n = rd_n; }
static void rd_check(const char *what)
{
	if (rd_n != rd_ref_n || memcmp(rd_log, rd_ref_log, (rd_n < 1100 ? rd_n : 1100) * 4))
		DIE(what, "register reads: %u vs %u", rd_ref_n, rd_n);
}

/* ---- our side ---- */
static struct t32_afg g;
static struct t32_front_write ow[MAXW];
static unsigned int on;
static u32 o_ev[8][2], o_evn;
static u32 o_irq[4], o_irqn, o_cb[2], o_cbn;
static void *o_irq_fn, *o_cb_fn;

static void *o_vmalloc(u32 n) { return t32o_alloc(n); }
static void *o_kmalloc(u32 n, u32 f) { (void)f; return t32o_alloc(n); }
static void o_vfree(void *p) { (void)p; }
static void o_kfree(void *p) { (void)p; }
static u32 o_rd(u32 reg) { return rd_model(reg); }
static void o_sync(u32 dev, void *a, u32 n, u32 dir) { (void)dev; (void)a; (void)n; (void)dir; }
static void o_push(u32 ch, const u32 *ev)
{
	if (o_evn < 8) {
		o_ev[o_evn][0] = ch;
		o_ev[o_evn][1] = ev[2];
	}
	o_evn++;
}
static void o_gpush(u8 ch, const struct t32_gsm_event *ev)
{
	if (o_evn < 8) {
		o_ev[o_evn][0] = ch;
		o_ev[o_evn][1] = ev->type;
	}
	o_evn++;
}
static void o_irq_set(u32 ch, u32 irq, void *fn) { o_irq[0] = ch; o_irq[1] = irq; o_irq_fn = fn; o_irqn++; }
static void o_cb_set(u32 ch, u32 ev, void *fn) { o_cb[0] = ch; o_cb[1] = ev; o_cb_fn = fn; o_cbn++; }
static const struct t32_af_ops ops = { o_vmalloc, o_kmalloc, o_vfree, o_kfree, o_rd, o_sync, o_push, o_irq_set, o_cb_set };

/* ---- vendor side ---- */
static u32 r_ev0;	/* evlog entries consumed */

static void vendor_load(void)
{
	unsigned int i;

	t32o_reset();
	memset(&t32_afg_vdata, 0, sizeof(t32_afg_vdata));
	for (i = 0; i < 12; i++)
		put32(t32_afg_vdata.d5c60, 4 * i, v_d5c60[i]);
	for (i = 0; i < 225; i++)
		put32(t32_afg_vdata.d5c90, 4 * i, v_d5c90[i]);
	for (i = 0; i < 255; i++)
		put32(t32_afg_vdata.d6014, 4 * i, v_d6014[i]);
	memset(&t32_afg_garena, 0x5c, sizeof(t32_afg_garena));
	memset(&t32_afg_garena.ctx, 0, sizeof(t32_afg_garena.ctx));
	memset(&t32_afg_garena.ram, 0, sizeof(t32_afg_garena.ram));
	t32_afg_garena.ctx.scale = 1;
	t32_afg_garena.ctx.pixels = 1;
	t32_afg_garena.ctx.api.zone[0] = 0xffff;
	put32(t32_afg_garena.ctx.api.bound, 0, 0xc090400du);
	t32_afg_garena.ctx.api.rsvd = 0xf0f;
	t32_afg_garena.ctx.cfg = 0x4080;
	memset(t32_data_e1600, 0, sizeof(t32_data_e1600));
	memset(t32_data_e1610, 0, sizeof(t32_data_e1610));
	memset(t32_data_e1620, 0, sizeof(t32_data_e1620));
	memset(t32_data_e15f0, 0, sizeof(t32_data_e15f0));
	t32_data_d03a0[0] = 0;
	t32o_rd_hook = rd_model;
	t32o_ev_n = 0;
	r_ev0 = 0;
}

static u8 *r_ctx(void) { return T32O_HOST(t32_data_e1620[0]); }
static u8 *r_dma(void) { return T32O_HOST(t32_data_e1610[0]); }
static u8 *r_blk(void) { return T32O_HOST(get32(r_ctx(), 0)); }
static u8 *r_regs(void) { return T32O_HOST(get32(r_ctx(), 4)); }
static u8 *r_par(void) { return T32O_HOST(get32(r_ctx(), 12)); }
static u32 r_cb(u32 type) { return get32(t32_data_dd6c0, (0 * 18 + type) * 4); }

/* the vendor's event thread: the callbacks the inits registered, in push order */
static void r_events(void)
{
	while (r_ev0 < t32o_ev_n && r_ev0 < T32O_EVLOG) {
		u32 ch = t32o_evlog[r_ev0][0], type = t32o_evlog[r_ev0][3], cb = get32(t32_data_dd6c0, (ch * 18 + type) * 4);

		r_ev0++;
		if (cb)
			t32o_call((t32o_fn)T32O_HOST(cb), ch, 0, 0, 0, 0);
	}
}

static void o_events(void)
{
	u32 i;

	for (i = 0; i < o_evn && i < 8; i++)
		t32_afg_event(&g, o_ev[i][0], o_ev[i][1]);
}

/* ---- compares ---- */
static void cmp_writes(const char *what, unsigned int n0, unsigned int nw, const struct t32_front_write *w)
{
	unsigned int i, n = t32o_log.n - n0;
	u32 rbase = r_dma() ? get32(r_dma(), 8) : 0, obase = g.af.dma[0] ? g.af.dma[0]->phys : 0;

	if (n != nw)
		DIE(what, "write count %u vs %u", n, nw);
	for (i = 0; i < n; i++) {
		u32 ra = t32o_log.w[n0 + i].reg, rv = t32o_log.w[n0 + i].val, oa = w[i].reg, ov = w[i].val;

		if (ra >= T32_AF_REG_DMA0 && ra <= T32_AF_REG_DMA3) {
			rv -= rbase;
			ov -= obase;
		}
		if (ra != oa || rv != ov)
			DIE(what, "write %u: %05x=%08x vs %05x=%08x", i, ra, rv, oa, ov);
	}
	compared++;
}

static void cmp_mem(const char *what, const char *n, const void *a, const void *b, u32 len)
{
	u32 i;

	if (!memcmp(a, b, len))
		return;
	for (i = 0; ((const u8 *)a)[i] == ((const u8 *)b)[i]; i++)
		;
	DIE(what, "%s byte %u: %02x vs %02x", n, i, ((const u8 *)a)[i], ((const u8 *)b)[i]);
}

static void cmp_state(const char *what)
{
	u8 *rc = r_ctx(), *rd = r_dma();
	unsigned int ch;

	if (!rc != !g.af.ctx[0] || !rd != !g.af.dma[0])
		DIE(what, "ctx / dma presence");
	if (rc) {
		cmp_mem(what, "par", r_par(), g.af.ctx[0]->par, PB);
		cmp_mem(what, "regs", r_regs(), g.af.ctx[0]->regs, SB);
		cmp_mem(what, "blk", r_blk(), g.af.ctx[0]->blk, BB);
		if (get32(rc, 8) != g.af.ctx[0]->rsvd || get32(rc, 16) != (g.af.ctx[0]->width | (u32)g.af.ctx[0]->height << 16))
			DIE(what, "ctx words");
		if (get32(rd, 0) != g.af.dma[0]->count)
			DIE(what, "dma count");
	}
	for (ch = 0; ch < 4; ch++) {
		if (t32_data_e1600[ch] != g.af.tgain[ch] || t32_data_e15f0[ch] != g.af.last_focus[ch])
			DIE(what, "AF globals ch %u: tgain %08x / %08x focus %08x / %08x", ch, t32_data_e1600[ch],
			    g.af.tgain[ch], t32_data_e15f0[ch], g.af.last_focus[ch]);
		if (ch && (t32_data_e1620[ch] || t32_data_e1610[ch] || g.af.ctx[ch] || g.af.dma[ch]))
			DIE(what, "decoy channel %u", ch);
	}
	cmp_mem(what, "attr", t32_afg_vdata.d5c60, g.af.attr, 48);
	cmp_mem(what, "attr2", t32_afg_vdata.d5c90, g.af.attr[2], 48);
	cmp_mem(what, "row5", t32_afg_vdata.d5c90, g.af.tbl_row5, 900);
	cmp_mem(what, "row12", t32_afg_vdata.d6014, g.af.tbl_row12, T32_AFG_ROWS * 48U);
	cmp_mem(what, "gsm ctx", &t32_afg_garena.ctx, &g.gsm, sizeof(g.gsm));
	cmp_mem(what, "gsm ram", &t32_afg_garena.ram, &g.ram, sizeof(g.ram));
	compared++;
}

/* ---- inputs ---- */
static u32 dim(void)
{
	static const u32 b[] = { 1, 7, 8, 9, 15 };

	return (rnd() & 1) ? b[rnd() % 5] : 1 + rnd() % 15;
}

/* a random AF section; gain mode with rows inside the vendor tables (row 12 index + 1 <= 20, row 5 + 1 <= 44) */
static void gen_sec(u8 *p, int gainmode)
{
	unsigned int i;

	rfill(p, PB);
	put16(p, T32_AF_P_Y0, rnd() % 40);
	put16(p, T32_AF_P_X0, rnd() % 40);
	put16(p, T32_AF_P_ROWS, dim());
	put16(p, T32_AF_P_COLS, dim());
	p[T32_AF_P_MODE] = gainmode ? (u8)(1 + rnd() % 255) : 0;
	put16(p, T32_AF_P_RNG0_LO, rnd() % 10);
	put16(p, T32_AF_P_RNG0_HI, rnd() % 10);
	put16(p, T32_AF_P_RNG1_LO, rnd() % 10);
	put16(p, T32_AF_P_RNG1_HI, rnd() % 10);
	put16(p, T32_AF_P_RNG2_LO, rnd() % 13);
	put16(p, T32_AF_P_RNG2_HI, rnd() % 13);
	put16(p, T32_AF_P_RNG3_LO, rnd() % 13);
	put16(p, T32_AF_P_RNG3_HI, rnd() % 13);
	put16(p, T32_AF_P_PARM_SH, rnd() % 17);
	for (i = 0; i < 13; i++) {
		p[T32_AF_P_GIDX_A + i] = (u8)(rnd() % 20);
		p[T32_AF_P_GIDX_C + i] = (u8)(rnd() % 44);
	}
}

/* ---- one case: init equivalence, then frames ---- */
static void one_case(const u8 *sec, u32 w, u32 h, unsigned int nframes)
{
	u8 *tun, *wh, *buf, *a, *b;
	unsigned int f, n0;
	u32 ref_cb15, ref_cb16, rv, ov;
	u8 ra[T32_AF_STATIS_BYTES], oa[T32_AF_STATIS_BYTES];

	/* vendor: lib_tisp_init's tisp_af_init(0, &{w, h}) ... tisp_gsm_init(0, &{w, h}) */
	vendor_load();
	tun = t32o_alloc(TUNB);
	rfill(tun, T32_AF_PAR_IQ_OFF);
	memcpy(tun + T32_AF_PAR_IQ_OFF, sec, PB);
	t32_data_dd0a4[0] = T32O_PTR(tun);
	wh = t32o_alloc(8);
	put32(wh, 0, w);
	put32(wh, 4, h);
	rd_seed = rnd();
	rd_arm();
	RC(tisp_af_init, 0, T32O_PTR(wh), 0, 0);
	if (t32o_log.irq != T32_AFG_IRQ_AF)
		DIE("init", "vendor AF irq %u", t32o_log.irq);
	RC(tisp_gsm_init, 0, T32O_PTR(wh), 0, 0);
	if (t32o_log.irq != T32_AFG_IRQ_GSM_FD)
		DIE("init", "vendor GSM irq (last) %u", t32o_log.irq);
	rd_keep();
	ref_cb15 = r_cb(T32_AFG_EV_AF);
	ref_cb16 = r_cb(T32_AFG_EV_GSM);
	if (ref_cb15 != T32O_PTR(tisp_af_calc_process) || ref_cb16 != T32O_PTR(tisp_gsm_process))
		DIE("init", "vendor event callbacks");

	/* ours */
	memset(&g, 0xa5, sizeof(g));
	g.af.ops = &ops;
	t32_afg_data(&g);
	o_evn = o_irqn = o_cbn = 0;
	rd_arm();
	on = (unsigned int)t32_afg_af_init(&g, 0, sec, w, h, ow);
	on += (unsigned int)t32_afg_gsm_init(&g, w, h, ow + on);
	rd_check("init");
	cmp_writes("init", 0, on, ow);
	if (o_irqn != 1 || o_irq[0] != 0 || o_irq[1] != T32_AFG_IRQ_AF || o_irq_fn != (void *)t32_af_interrupt_static ||
	    o_cbn != 1 || o_cb[0] != 0 || o_cb[1] != T32_AFG_EV_AF || o_cb_fn != (void *)t32_af_calc_process)
		DIE("init", "our AF registrations");
	if (!g.af_on || !g.gsm_on)
		DIE("init", "blocks not on");
	cmp_state("init");
	cases++;

	for (f = 0; f < nframes; f++) {
		u32 st0 = 0, st3 = 0, op = rnd() % 10;

		/* tisp_long_tgain_update's AF hand-over (low gain word) */
		if (op == 0) {
			u32 gn = (rnd() & 0xffffU) | ((rnd() % 13U) << 16);

			RC(tisp_af_tgain_update, 0, gn, 0, 0);
			t32_af_tgain_update(&g.af, 0, gn);
		}
		/* the interrupt bits; the DMA slot gets the same random statistics on both sides */
		if (rnd() % 4)
			st0 |= 1U << T32_AFG_IRQ_AF;
		if (rnd() % 3)
			st0 |= 1U << T32_AFG_IRQ_GSM;
		if (rnd() % 3 == 0)
			st3 |= 1U << (T32_AFG_IRQ_GSM_FD - 32);
		rd_slot = rnd() & 3;
		rd_gsm = (rnd() & 3) ? 1 : rnd() % 3;
		rd_seed = rnd();
		buf = T32O_HOST(get32(r_dma(), 4));
		rfill(buf, T32_AF_DMA_BYTES);
		memcpy(g.af.dma[0]->buf, buf, T32_AF_DMA_BYTES);

		t32o_log.n = 0;
		t32o_ev_n = 0;
		r_ev0 = 0;
		n0 = 0;
		rd_arm();
		if (st0 & (1U << T32_AFG_IRQ_AF))
			RC(tisp_af_interrupt_static, 0, 0, 0, 0);
		if (st0 & (1U << T32_AFG_IRQ_GSM))
			RC(tisp_gsm_interrupt_hist, 0, 0, 0, 0);
		if (st3)
			RC(tisp_gsm_irq_frmae_done, 0, 0, 0, 0);
		r_events();
		rd_keep();
		o_evn = 0;
		rd_arm();
		on = (unsigned int)t32_afg_irq(&g, st0, st3, 0, o_rd, o_gpush, ow);
		o_events();
		rd_check("frame");
		cmp_writes("frame", n0, on, ow);
		cmp_state("frame");

		/* tuning ids on the blocks */
		a = t32o_alloc(T32_AF_STATIS_BYTES);
		b = t32o_alloc(16);
		switch (op) {
		case 1:	/* tisp_s_statis_config_attr: af_api_set_attr */
			rfill(a, 24);
			a[20] = (u8)dim();	/* the zone grid stays inside the 15 x 15 arrays (vendor: no check) */
			a[21] = (u8)dim();
			t32o_log.n = n0 = 0;
			RC(tisp_af_api_set_attr, 0, T32O_PTR(a), 0, 0);
			on = (unsigned int)t32_af_api_set_attr(&g.af, 0, a, ow);
			cmp_writes("set_attr", n0, on, ow);
			break;
		case 2:	/* tisp_s_af_weight_attr */
			rfill(a, T32_AF_WEIGHT_BYTES);
			RC(tisp_af_api_set_weight, 0, T32O_PTR(a), 0, 0);
			t32_af_api_set_weight(&g.af, 0, a);
			break;
		case 3:	/* tisp_s_statis_config_attr: gsm_api_set (1052 B scratch, bounds at 1044) */
			a = t32o_alloc(1052U);
			rfill(a, 1052U);
			RC(tisp_gsm_api_set, 0, T32O_PTR(a), 0, 0);
			t32_gsm_api_set(&g.gsm, (const struct t32_gsm_api *)a);
			break;
		default:
			break;
		}
		cmp_state("api set");
		/* getters: statis config / af weight / af statistics / AF metrics; GSM api get */
		memset(ra, 0x11, sizeof(ra));
		memset(oa, 0x11, sizeof(oa));
		RC(tisp_af_api_get_attr, 0, T32O_PTR(a), 0, 0);
		t32_af_api_get_attr(&g.af, 0, oa);
		cmp_mem("get_attr", "out", a, oa, T32_AF_ATTR_BYTES);
		RC(tisp_af_api_get_weight, 0, T32O_PTR(a), 0, 0);
		t32_af_api_get_weight(&g.af, 0, oa);
		cmp_mem("get_weight", "out", a, oa, T32_AF_WEIGHT_BYTES);
		RC(tisp_af_api_get_statis_info, 0, T32O_PTR(a), 0, 0);
		t32_af_api_get_statis_info(&g.af, 0, oa);
		cmp_mem("statis_info", "out", a, oa, T32_AF_STATIS_BYTES);
		memset(b, 0x22, 16);
		memset(ra, 0x22, 16);
		rv = (u32)RC(tisp_af_api_get_metric, 0, T32O_PTR(b), T32O_PTR(b + 4), T32O_PTR(b + 8));
		ov = (u32)t32_af_api_get_metric(&g.af, 0, (u32 *)(void *)ra, (u32 *)(void *)(ra + 4), ra + 8);
		if (rv != ov)
			DIE("get_metric", "return %u vs %u", rv, ov);
		cmp_mem("get_metric", "out", b, ra, 12);
		a = t32o_alloc(sizeof(struct t32_gsm_api));
		RC(tisp_gsm_api_get, 0, T32O_PTR(a), 0, 0);
		t32_gsm_api_get(&g.gsm, (struct t32_gsm_api *)(void *)oa);
		cmp_mem("gsm_api_get", "out", a, oa, sizeof(struct t32_gsm_api));
		cmp_state("api get");
		frames++;
	}
}

static u32 bin_sizes[4][2] = { { 1920, 1080 }, { 2560, 1440 }, { 1280, 720 }, { 640, 360 } };

static int bin_cases(const char *dir)
{
	DIR *d = dir ? opendir(dir) : NULL;
	struct dirent *e;
	int n = 0;

	if (!d)
		return 0;
	while ((e = readdir(d))) {
		static u8 b[1u << 20];
		char path[1024];
		size_t l = strlen(e->d_name), bytes;
		u32 off, len = 0, k;
		FILE *f;

		if (l < 4 || strcmp(e->d_name + l - 4, ".bin"))
			continue;
		snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
		f = fopen(path, "rb");
		if (!f)
			continue;
		bytes = fread(b, 1, sizeof(b), f);
		fclose(f);
		off = t32_iq_section(b, (u32)bytes, 24, &len);	/* tuning base + 60464, 800 B (t32_iq_oracle) */
		if (t32_afg_af_section(b, (u32)bytes) != (off && len >= PB ? b + off : NULL))
			DIE("section", "%.64s: AF section of ours differs from section 24", e->d_name);
		if (!off || len < PB)
			continue;
		n++;
		for (k = 0; k < 4; k++)
			one_case(b + off, bin_sizes[k][0], bin_sizes[k][1], 24);
	}
	closedir(d);
	return n;
}

int main(void)
{
	const char *dir = getenv("T32_IQ_DIR");
	int strict = getenv("T40_ORACLE_STRICT") && atoi(getenv("T40_ORACLE_STRICT"));
	unsigned int i, nr = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 400;
	u8 sec[PB];

	for (i = 0; i < nr; i++) {
		u32 w = (i & 1) ? bin_sizes[rnd() % 4][0] : 64 + rnd() % 4000;
		u32 h = (i & 1) ? bin_sizes[rnd() % 4][1] : 64 + rnd() % 3000;

		gen_sec(sec, (i % 3) == 2);
		one_case(sec, w, h, 12);
	}
	bins = (unsigned int)bin_cases(dir);
	printf("t32_afgsm_oracle: %u cases (%u IQ bins x 4 sizes), %u frames, %u compared, 0 mismatches\n",
	       cases, bins, frames, compared);
	if (!bins) {
		printf("t32_afgsm_oracle: IQ-bin part: SKIP (set T32_IQ_DIR to a directory of T32 *.bin)\n");
		return strict ? 2 : 0;
	}
	return 0;
}
