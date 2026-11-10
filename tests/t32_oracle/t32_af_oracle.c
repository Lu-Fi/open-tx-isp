// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: AF block (driver/t32/tx_isp_t32_af.h) against Matt Davis'
 * recovered tisp_af_* functions (ref/, PR #15 @ 2e2e06bc).
 * Per case ref and port start from the same state (frames with canaries,
 * decoy channels); compared: register writes (address, value, order), every
 * byte of par/blk/regs of all channels, globals (tgain, last focus, attr
 * table), outputs and return values.
 * env.c lacks t32_data_e1600/e1610/e1620/e15f0/d5c60/d6014/d5c90/dd6c0 and
 * some stubs: the oracle defines weak versions; d6014/d5c90 get random data.
 * Event/irq/dma/alloc comparisons only run when the weak stubs are active.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_af.h"

#define WEAK __attribute__((weak))
#define NCH	T32_AF_CHANNELS
#define SB	T32_AF_REGS_BYTES
#define BB	T32_AF_BLK_BYTES
#define PB	T32_AF_PAR_BYTES
#define MAXW	96
#define DBUF	20480U
#define TROWS	300U
#define CAN	32U
#define TUNB	(T32_AF_PAR_IQ_OFF + PB + 64U)

WEAK u32 t32_data_e1600[8];
WEAK u32 t32_data_e1610[8];
WEAK u32 t32_data_e1620[8];
WEAK u32 t32_data_e15f0[8];
WEAK u8 t32_data_d5c60[4 * 24 + 32];
WEAK u8 t32_data_d6014[TROWS * 48U];
WEAK u8 t32_data_d5c90[TROWS * 20U];
WEAK u32 t32_data_dd6c0[128];
WEAK u8 t32_data_bf410[16];
WEAK u8 t32_data_c8728[16];

extern u64 tisp_af_malloc_cfg(u32, u32, u32, u32);
extern u64 tisp_af_get_statistics(u32, u32, u32, u32);
extern u64 tisp_af_interrupt_static(u32, u32, u32, u32);
extern u64 tisp_af_tgain_update(u32, u32, u32, u32);
extern u64 tisp_af_par_init(u32, u32, u32, u32);
extern u64 tisp_af_all_reg_write(u32, u32, u32, u32);
extern u64 tisp_af_alogrithm(u32, u32, u32, u32);
extern u64 tisp_af_calc_process(u32, u32, u32, u32);
extern u64 tisp_af_init(u32, u32, u32, u32);
extern u64 tisp_af_deinit(u32, u32, u32, u32);
extern u64 tisp_af_param_array_get(u32, u32, u32, u32);
extern u64 tisp_af_param_array_set(u32, u32, u32, u32);
extern u64 tisp_af_api_set_weight(u32, u32, u32, u32);
extern u64 tisp_af_api_get_weight(u32, u32, u32, u32);
extern u64 tisp_af_api_get_metric(u32, u32, u32, u32);
extern u64 tisp_af_api_get_attr(u32, u32, u32, u32);
extern u64 tisp_af_api_set_attr(u32, u32, u32, u32);
extern u64 tisp_af_api_get_statis_info(u32, u32, u32, u32);
extern u64 tisp_af_wake_up(u32, u32, u32, u32);
extern u64 fix_point_mult2_32(u32, u32, u32, u32);

#define RC(f, a, b, c, d) t32o_call((t32o_fn)(void (*)(void))(f), (u32)(a), (u32)(b), (u32)(c), (u32)(d), 0)

struct rec {
	unsigned int vm_n, km_n, vf_n, kf_n, dma_n, ev_n, irq_n, rd_n, cb_n;
	u32 vm_size[16], km_size[4], km_flag[4], vf_ptr[16], kf_ptr[8];
	u32 dma_a[4], ev_ch, ev_w[3], irq_a[3], rd_reg, cb_a[2];
	void *irq_fn, *cb_fn;
};
static struct rec R_ref, R_our;
static u32 rd_val;

/* weak vendor glue (used only when env.c has no strong version) */
WEAK u64 private_vmalloc(u32 size, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	if (R_ref.vm_n < 16)
		R_ref.vm_size[R_ref.vm_n] = size;
	R_ref.vm_n++;
	return T32O_PTR(t32o_alloc(size));
}
WEAK u64 private_kmalloc(u32 size, u32 flags, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	if (R_ref.km_n < 4) {
		R_ref.km_size[R_ref.km_n] = size;
		R_ref.km_flag[R_ref.km_n] = flags;
	}
	R_ref.km_n++;
	return T32O_PTR(t32o_alloc(size));
}
WEAK u64 private_vfree(u32 p, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	if (R_ref.vf_n < 16)
		R_ref.vf_ptr[R_ref.vf_n] = p;
	R_ref.vf_n++;
	return 0;
}
WEAK u64 private_kfree(u32 p, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	if (R_ref.kf_n < 8)
		R_ref.kf_ptr[R_ref.kf_n] = p;
	R_ref.kf_n++;
	return 0;
}
WEAK u64 private_dma_cache_sync(u32 a0, u32 a1, u32 a2, u32 a3)
{
	R_ref.dma_a[0] = a0; R_ref.dma_a[1] = a1; R_ref.dma_a[2] = a2; R_ref.dma_a[3] = a3;
	R_ref.dma_n++;
	return 0;
}
WEAK u64 tisp_event_push(u32 ch, u32 ev, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	R_ref.ev_ch = ch;
	memcpy(R_ref.ev_w, T32O_HOST(ev), 12);
	R_ref.ev_n++;
	return 0;
}
WEAK u64 system_irq_func_set(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a3;
	R_ref.irq_a[0] = a0; R_ref.irq_a[1] = a1; R_ref.irq_a[2] = a2;
	R_ref.irq_n++;
	return 0;
}
WEAK u64 system_reg_read(u32 reg, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	return t32o_rd_hook ? t32o_rd_hook(reg) : 0;
}
WEAK u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a1; (void)a2; (void)a3;
	return 0;
}

static u32 rs = 0x5af0c3u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}
static void rfill(u8 *p, u32 n)
{
	u32 i;

	for (i = 0; i < n; i++)
		p[i] = (u8)rnd();
}
static void put32(u8 *p, u32 o, u32 v) { memcpy(p + o, &v, 4); }
static u32 get32(const u8 *p, u32 o) { u32 v; memcpy(&v, p + o, 4); return v; }
static void put16(u8 *p, u32 o, u32 v) { p[o] = (u8)v; p[o + 1] = (u8)(v >> 8); }
static u32 get16(const u8 *p, u32 o) { return (u32)p[o] | ((u32)p[o + 1] << 8); }

static unsigned int compared;
static char dmsg[200];
static void die(const char *what)
{
	printf("MISMATCH %s: %s\n", what, dmsg);
	printf("t32_af_oracle: %u compared, 1 mismatches\n", compared);
	exit(1);
}
#define DIE(what, ...) do { snprintf(dmsg, sizeof(dmsg), __VA_ARGS__); die(what); } while (0)

static u8 *frame(u32 n)
{
	u8 *raw = t32o_alloc(n + 2U * CAN + 16U);
	u8 *p = (u8 *)((((uintptr_t)raw + 15U) & ~(uintptr_t)15U) + CAN);

	memset(p - CAN, 0xc3, CAN);
	memset(p + n, 0x3c, CAN);
	return p;
}
static int frame_ok(const u8 *p, u32 n)
{
	u32 i;

	for (i = 0; i < CAN; i++)
		if (p[i] != p[i] && 0)
			return 0;
	for (i = 0; i < CAN; i++)
		if (*(p - CAN + i) != 0xc3 || p[n + i] != 0x3c)
			return 0;
	return 1;
}
static void chk(const char *what, const u8 *p, u32 n)
{
	if (!frame_ok(p, n))
		DIE(what, "canary damaged");
}

static u8 *fS[NCH], *fB[NCH], *fP[NCH], *vctx[NCH], *vdma[NCH], *dbuf[NCH];
static u8 iS[NCH][SB], iB[NCH][BB], iP[NCH][PB];
static u8 rS[NCH][SB], rB[NCH][BB], rP[NCH][PB];
static struct t32_af_ctx octx[NCH];
static struct t32_af_dma odma[NCH];
static struct t32_af_state ost;
static struct t32_front_write ow[MAXW];
static u8 npool[32][128];
static unsigned int nidx;
static u8 *rp_S, *rp_B, *rp_P, *rp_buf;

static void *o_vmalloc(u32 size)
{
	u8 *q;

	if (R_our.vm_n < 16)
		R_our.vm_size[R_our.vm_n] = size;
	R_our.vm_n++;
	if (size == SB && rp_S)
		return rp_S;
	if (size == BB && rp_B)
		return rp_B;
	if (size == PB && rp_P)
		return rp_P;
	q = npool[nidx++ & 31U];
	memset(q, 0xee, 128);
	return q;
}
static void *o_kmalloc(u32 size, u32 flags)
{
	if (R_our.km_n < 4) {
		R_our.km_size[R_our.km_n] = size;
		R_our.km_flag[R_our.km_n] = flags;
	}
	R_our.km_n++;
	return rp_buf;
}
static void o_vfree(void *p)
{
	if (R_our.vf_n < 16)
		R_our.vf_ptr[R_our.vf_n] = (u32)(uintptr_t)p;
	R_our.vf_n++;
}
static void o_kfree(void *p)
{
	if (R_our.kf_n < 8)
		R_our.kf_ptr[R_our.kf_n] = (u32)(uintptr_t)p;
	R_our.kf_n++;
}
static u32 o_reg_read(u32 reg)
{
	R_our.rd_reg = reg;
	R_our.rd_n++;
	return rd_val;
}
static u32 hook_rd(u32 reg)
{
	R_ref.rd_reg = reg;
	R_ref.rd_n++;
	return rd_val;
}
static void o_dma_sync(u32 dev, void *addr, u32 size, u32 dir)
{
	R_our.dma_a[0] = dev; R_our.dma_a[1] = (u32)(uintptr_t)addr; R_our.dma_a[2] = size; R_our.dma_a[3] = dir;
	R_our.dma_n++;
}
static void o_event_push(u32 ch, const u32 *ev)
{
	R_our.ev_ch = ch;
	memcpy(R_our.ev_w, ev, 12);
	R_our.ev_n++;
}
static void o_irq_set(u32 ch, u32 irq, void *fn)
{
	R_our.irq_a[0] = ch; R_our.irq_a[1] = irq; R_our.irq_fn = fn;
	R_our.irq_n++;
}
static void o_event_set_cb(u32 ch, u32 ev, void *cb)
{
	R_our.cb_a[0] = ch; R_our.cb_a[1] = ev; R_our.cb_fn = cb;
	R_our.cb_n++;
}
static const struct t32_af_ops ops = {
	o_vmalloc, o_kmalloc, o_vfree, o_kfree, o_reg_read, o_dma_sync,
	o_event_push, o_irq_set, o_event_set_cb
};

static void setup(void)
{
	unsigned int ch, i;

	t32o_reset();
	memset(&R_ref, 0, sizeof(R_ref));
	memset(&R_our, 0, sizeof(R_our));
	nidx = 0;
	rp_S = rp_B = rp_P = rp_buf = NULL;
	t32_data_d03a0[0] = 0;
	t32o_rd_hook = hook_rd;
	for (ch = 0; ch < NCH; ch++) {
		fS[ch] = frame(SB); rfill(fS[ch], SB);
		fB[ch] = frame(BB); rfill(fB[ch], BB);
		fP[ch] = frame(PB); rfill(fP[ch], PB);
		dbuf[ch] = frame(DBUF); rfill(dbuf[ch], DBUF);
		vctx[ch] = frame(20); rfill(vctx[ch], 20);
		put32(vctx[ch], 0, T32O_PTR(fB[ch]));
		put32(vctx[ch], 4, T32O_PTR(fS[ch]));
		put32(vctx[ch], 12, T32O_PTR(fP[ch]));
		put16(vctx[ch], 16, (rnd() & 1) ? rnd() % 200 : rnd() & 0xffff);
		put16(vctx[ch], 18, (rnd() & 1) ? rnd() % 200 : rnd() & 0xffff);
		vdma[ch] = frame(12); rfill(vdma[ch], 12);
		put32(vdma[ch], 4, T32O_PTR(dbuf[ch]));
		t32_data_e1620[ch] = T32O_PTR(vctx[ch]);
		t32_data_e1610[ch] = T32O_PTR(vdma[ch]);
		t32_data_e1600[ch] = rnd();
		t32_data_e15f0[ch] = (rnd() & 3) ? ((rnd() & 1) ? rnd() : rnd() & 0xff) : 0;
	}
	rfill(t32_data_d5c60, sizeof(t32_data_d5c60));
	rfill(t32_data_d6014, sizeof(t32_data_d6014));
	rfill(t32_data_d5c90, sizeof(t32_data_d5c90));
	for (i = 0; i < 128; i++)
		t32_data_dd6c0[i] = rnd();
}

static void arm(void)
{
	unsigned int ch;

	for (ch = 0; ch < NCH; ch++) {
		memcpy(iS[ch], fS[ch], SB);
		memcpy(iB[ch], fB[ch], BB);
		memcpy(iP[ch], fP[ch], PB);
		octx[ch].blk = T32O_HOST(get32(vctx[ch], 0));
		octx[ch].regs = T32O_HOST(get32(vctx[ch], 4));
		octx[ch].rsvd = get32(vctx[ch], 8);
		octx[ch].par = T32O_HOST(get32(vctx[ch], 12));
		octx[ch].width = (u16)get16(vctx[ch], 16);
		octx[ch].height = (u16)get16(vctx[ch], 18);
		odma[ch].count = get32(vdma[ch], 0);
		odma[ch].buf = T32O_HOST(get32(vdma[ch], 4));
		odma[ch].phys = get32(vdma[ch], 8);
		ost.ctx[ch] = t32_data_e1620[ch] ? &octx[ch] : NULL;
		ost.dma[ch] = t32_data_e1610[ch] ? &odma[ch] : NULL;
		ost.tgain[ch] = t32_data_e1600[ch];
		ost.last_focus[ch] = t32_data_e15f0[ch];
		memcpy(ost.attr[ch], t32_data_d5c60 + ch * 24U, 24);
	}
	ost.ops = &ops;
	ost.tbl_row12 = t32_data_d6014;
	ost.tbl_row5 = t32_data_d5c90;
}

static void snap(const char *what)
{
	unsigned int ch;

	for (ch = 0; ch < NCH; ch++) {
		chk(what, fS[ch], SB); chk(what, fB[ch], BB); chk(what, fP[ch], PB); chk(what, dbuf[ch], DBUF);
		memcpy(rS[ch], fS[ch], SB);
		memcpy(rB[ch], fB[ch], BB);
		memcpy(rP[ch], fP[ch], PB);
		memcpy(fS[ch], iS[ch], SB);
		memcpy(fB[ch], iB[ch], BB);
		memcpy(fP[ch], iP[ch], PB);
	}
}

static void memdiff(const char *what, const char *nm, unsigned int ch, const u8 *a, const u8 *b, u32 n)
{
	u32 i;

	for (i = 0; i < n; i++)
		if (a[i] != b[i])
			DIE(what, "%s ch%u +%u vendor %02x ours %02x", nm, ch, i, a[i], b[i]);
}

static void cmp_state(const char *what)
{
	unsigned int ch;

	for (ch = 0; ch < NCH; ch++) {
		memdiff(what, "regs", ch, rS[ch], fS[ch], SB);
		memdiff(what, "blk", ch, rB[ch], fB[ch], BB);
		memdiff(what, "par", ch, rP[ch], fP[ch], PB);
		chk(what, fS[ch], SB); chk(what, fB[ch], BB); chk(what, fP[ch], PB); chk(what, dbuf[ch], DBUF);
		if (t32_data_e1600[ch] != ost.tgain[ch])
			DIE(what, "tgain ch%u %08x vs %08x", ch, t32_data_e1600[ch], ost.tgain[ch]);
		if (t32_data_e15f0[ch] != ost.last_focus[ch])
			DIE(what, "focus ch%u %08x vs %08x", ch, t32_data_e15f0[ch], ost.last_focus[ch]);
		memdiff(what, "attr", ch, t32_data_d5c60 + ch * 24U, ost.attr[ch], 24);
	}
	compared++;
}

static void cmp_writes(const char *what, int on)
{
	unsigned int i;

	if (on < 0 || t32o_log.n != (unsigned int)on)
		DIE(what, "writes vendor %u ours %d", t32o_log.n, on);
	for (i = 0; i < t32o_log.n; i++)
		if (!t32o_reg_eq(t32o_log.w[i].reg, ow[i].reg) || t32o_log.w[i].val != ow[i].val)
			DIE(what, "write #%u vendor %05x=%08x ours %05x=%08x", i, t32o_log.w[i].reg,
			    t32o_log.w[i].val, ow[i].reg, ow[i].val);
}

static u32 dim(void)
{
	static const u32 b[] = { 0, 1, 7, 8, 9, 15 };

	return (rnd() & 1) ? b[rnd() % 6] : rnd() % 16;
}

static void gen_par(u8 *p, u32 ch, int modeb)
{
	put16(p, T32_AF_P_Y0, rnd() % 40);
	put16(p, T32_AF_P_X0, rnd() % 40);
	put16(p, T32_AF_P_ROWS, dim());
	put16(p, T32_AF_P_COLS, dim());
	p[T32_AF_P_MODE] = modeb ? (u8)(1 + rnd() % 3) : 0;
	put16(p, T32_AF_P_RNG0_LO, rnd() % 10);
	put16(p, T32_AF_P_RNG0_HI, rnd() % 10);
	put16(p, T32_AF_P_RNG1_LO, rnd() % 10);
	put16(p, T32_AF_P_RNG1_HI, rnd() % 10);
	put16(p, T32_AF_P_RNG2_LO, rnd() % 13);
	put16(p, T32_AF_P_RNG2_HI, rnd() % 13);
	put16(p, T32_AF_P_RNG3_LO, rnd() % 13);
	put16(p, T32_AF_P_RNG3_HI, rnd() % 13);
	if (modeb)
		t32_data_e1600[ch] = (rnd() & 0xffffU) | ((rnd() % 14U) << 16);
}

static void ref_fix(void)
{
	unsigned int i;

	for (i = 0; i < 400; i++) {
		u32 x = rnd(), y = (i & 3) ? rnd() >> (rnd() & 31) : 0, sh = rnd() % 33;
		u32 yr = y, xr = x;

		if ((i & 7) == 5) {		/* remainder equals half the divisor */
			yr = (rnd() >> (4 + (rnd() % 20))) & ~1U;
			xr = yr ? (rnd() % 1000) * yr + yr / 2 : x;
		}
		if ((u32)RC(fix_point_div_32, 16, xr, yr, 0) != t32_af_div16(xr, yr))
			DIE("fixdiv", "x=%08x y=%08x", xr, yr);
		if ((u32)RC(fix_point_mult2_32, sh, x, y, 0) != t32_af_mult2(sh, x, y))
			DIE("fixmult", "sh=%u x=%08x y=%08x", sh, x, y);
		compared++;
	}
}

static void t_tgain(void)
{
	u32 ch = rnd() & 3, g = rnd();

	setup(); arm();
	RC(tisp_af_tgain_update, ch, g, 0, 0);
	snap("tgain");
	t32_af_tgain_update(&ost, ch, g);
	cmp_state("tgain");
}

static void t_all_reg(void)
{
	u32 ch = rnd() & 3;
	int on;

	setup();
	if ((rnd() & 7) == 0)
		memset(fS[ch], (rnd() & 1) ? 0xff : 0, SB);
	arm();
	RC(tisp_af_all_reg_write, ch, T32O_PTR(fS[ch]), 0, 0);
	snap("all_reg_write");
	on = t32_af_all_reg_write(fS[ch], ow);
	cmp_writes("all_reg_write", on);
	cmp_state("all_reg_write");
}

static void t_par(void)
{
	u32 ch = rnd() & 3;

	setup();
	gen_par(fP[ch], ch, rnd() & 1);
	arm();
	RC(tisp_af_par_init, ch, 0, 0, 0);
	snap("par_init");
	t32_af_par_init(&ost, ch);
	cmp_state("par_init");
}

static void t_stat(int fc, int fr)
{
	u32 ch = rnd() & 3, chv = ch | ((rnd() & 1) ? 0x100U : 0);
	u8 *src;

	setup();
	fS[ch][0] = fc >= 0 ? (u8)fc : (u8)dim();
	fS[ch][1] = fr >= 0 ? (u8)fr : (u8)dim();
	src = frame(8192); rfill(src, 8192);
	arm();
	RC(tisp_af_get_statistics, chv, T32O_PTR(src), 0, 0);
	snap("get_statistics");
	t32_af_get_statistics(&ost, chv, src);
	cmp_state("get_statistics");
	chk("get_statistics", src, 8192);
}

static void t_alog(void)
{
	u32 ch = rnd() & 3, chv = ch | ((rnd() & 1) ? 0x100U : 0), calc = rnd() & 1;
	u8 *p = fP[ch], *b = fB[ch];
	u64 r;
	u32 o;

	setup();
	put16(p, T32_AF_P_ROWS, dim());
	put16(p, T32_AF_P_COLS, dim());
	put16(p, T32_AF_P_PARM_SH, rnd() % 33);
	if (rnd() % 5 == 0)
		memset(p + T32_AF_P_WEIGHT, 0, 225);
	put16(b, 6428, rnd() % 40);
	b[6431] = (u8)(rnd() % 6);
	arm();
	r = calc ? RC(tisp_af_calc_process, chv, 0, 0, 0) : RC(tisp_af_alogrithm, chv, 0, 0, 0);
	snap("alogrithm");
	if (calc) {
		if ((u32)t32_af_calc_process(&ost, chv) != 0 || (u32)r != 0)
			DIE("calc_process", "ret %08x", (u32)r);
	} else {
		o = t32_af_alogrithm(&ost, chv);
		if ((u32)r != o)
			DIE("alogrithm", "ret %08x vs %08x", (u32)r, o);
	}
	cmp_state("alogrithm");
}

static void t_irq(void)
{
	u32 ch = rnd() & 3;
	u64 r;
	int o;

	setup();
	fS[ch][0] = (u8)dim();
	fS[ch][1] = (u8)dim();
	rd_val = rnd() & 3;
	arm();
	r = RC(tisp_af_interrupt_static, ch, 0, 0, 0);
	snap("interrupt_static");
	o = t32_af_interrupt_static(&ost, ch);
	if ((u32)r != 1 || o != 1)
		DIE("interrupt_static", "ret %u/%d", (u32)r, o);
	if (R_ref.rd_n != 1 || R_our.rd_n != 1 || R_ref.rd_reg != R_our.rd_reg ||
	    R_our.rd_reg != (ch ? 18314U : 0x1811cU))
		DIE("interrupt_static", "read reg %x/%x n=%u", R_ref.rd_reg, R_our.rd_reg, R_ref.rd_n);
	if (R_our.dma_a[1] != T32O_PTR(dbuf[0]) + (rd_val << 12) || R_our.dma_a[2] != 4096 || R_our.dma_a[0] || R_our.dma_a[3])
		DIE("interrupt_static", "dma sync ours %x %x", R_our.dma_a[1], R_our.dma_a[2]);
	if (R_ref.dma_n && memcmp(R_ref.dma_a, R_our.dma_a, 16))
		DIE("interrupt_static", "dma sync args");
	if (R_ref.ev_n && (R_ref.ev_ch != R_our.ev_ch || memcmp(R_ref.ev_w, R_our.ev_w, 12)))
		DIE("interrupt_static", "event %u %u", R_ref.ev_w[2], R_our.ev_w[2]);
	if (R_our.ev_w[2] != 15 || R_our.ev_n != 1)
		DIE("interrupt_static", "ours event");
	cmp_state("interrupt_static");
}

static void t_malloc(void)
{
	u32 ch = rnd() & 3, o;
	u32 s1610[NCH];
	u8 *vd;
	int on;

	setup(); arm();
	for (o = 0; o < NCH; o++)
		s1610[o] = t32_data_e1610[o];
	RC(tisp_af_malloc_cfg, ch, 0, 0, 0);
	snap("malloc_cfg");
	vd = T32O_HOST(t32_data_e1610[ch]);
	rp_buf = T32O_HOST(get32(vd, 4));
	on = t32_af_malloc_cfg(&ost, ch, ow);
	cmp_writes("malloc_cfg", on);
	if (ost.dma[ch]->count != get32(vd, 0) || ost.dma[ch]->count != 4 || ost.dma[ch]->buf != rp_buf ||
	    ost.dma[ch]->phys != get32(vd, 8))
		DIE("malloc_cfg", "dma struct %u %08x vs %08x", ost.dma[ch]->count, ost.dma[ch]->phys, get32(vd, 8));
	for (o = 0; o < NCH; o++)
		if (o != ch && (t32_data_e1610[o] != s1610[o] || ost.dma[o] != &odma[o]))
			DIE("malloc_cfg", "decoy slot %u", o);
	if (R_ref.km_n && (R_ref.km_size[0] != R_our.km_size[0] || R_ref.km_flag[0] != R_our.km_flag[0]))
		DIE("malloc_cfg", "kmalloc args");
	cmp_state("malloc_cfg");
}

static void t_init(void)
{
	u32 ch = rnd() & 3, o, w = rnd(), h = rnd();
	u32 s1620[NCH], s1610[NCH];
	u8 *tun, *wh, *vc, *vd, sS[SB], sB[BB], sP[PB], vcs[20], vds[12];
	u8 *S_r, *B_r, *P_r;
	u32 cbv;
	int on;
	unsigned int i, j;
	u32 rl[3], ol[3];

	setup();
	tun = frame(TUNB); rfill(tun, TUNB);
	gen_par(tun + T32_AF_PAR_IQ_OFF, ch, rnd() & 1);
	t32_data_dd0a4[ch] = T32O_PTR(tun);
	wh = frame(8); put32(wh, 0, w); put32(wh, 4, h);
	arm();
	for (o = 0; o < NCH; o++) {
		s1620[o] = t32_data_e1620[o];
		s1610[o] = t32_data_e1610[o];
	}
	RC(tisp_af_init, ch, T32O_PTR(wh), 0, 0);
	vc = T32O_HOST(t32_data_e1620[ch]);
	vd = T32O_HOST(t32_data_e1610[ch]);
	S_r = T32O_HOST(get32(vc, 4)); B_r = T32O_HOST(get32(vc, 0)); P_r = T32O_HOST(get32(vc, 12));
	rp_S = S_r; rp_B = B_r; rp_P = P_r; rp_buf = T32O_HOST(get32(vd, 4));
	memcpy(sS, S_r, SB); memcpy(sB, B_r, BB); memcpy(sP, P_r, PB);
	memcpy(vcs, vc, 20); memcpy(vds, vd, 12);
	cbv = t32_data_dd6c0[ch * 18U + 15U];
	snap("init");
	on = t32_af_init(&ost, ch, w, h, tun, ow);
	cmp_writes("init", on);
	memdiff("init", "S", ch, sS, S_r, SB);
	memdiff("init", "B", ch, sB, B_r, BB);
	memdiff("init", "P", ch, sP, P_r, PB);
	if (ost.ctx[ch]->blk != B_r || ost.ctx[ch]->regs != S_r || ost.ctx[ch]->par != P_r ||
	    ost.ctx[ch]->width != get16(vcs, 16) || ost.ctx[ch]->height != get16(vcs, 18) ||
	    ost.ctx[ch]->rsvd != get32(vcs, 8))
		DIE("init", "ctx fields");
	if (ost.dma[ch]->count != get32(vds, 0) || ost.dma[ch]->phys != get32(vds, 8) || ost.dma[ch]->buf != rp_buf)
		DIE("init", "dma fields");
	for (o = 0; o < NCH; o++)
		if (o != ch && (t32_data_e1620[o] != s1620[o] || t32_data_e1610[o] != s1610[o] ||
				ost.ctx[o] != &octx[o] || ost.dma[o] != &odma[o]))
			DIE("init", "decoy slot %u", o);
	if (cbv != (u32)(uintptr_t)tisp_af_calc_process || R_our.cb_n != 1 || R_our.cb_a[0] != ch ||
	    R_our.cb_a[1] != 15 || R_our.cb_fn != (void *)t32_af_calc_process)
		DIE("init", "event cb");
	if (R_our.irq_n != 1 || R_our.irq_a[0] != (ch & 0xff) || R_our.irq_a[1] != 6 ||
	    R_our.irq_fn != (void *)t32_af_interrupt_static)
		DIE("init", "irq ours");
	if (R_ref.irq_n && (R_ref.irq_a[0] != (ch & 0xff) || R_ref.irq_a[1] != 6 ||
			    R_ref.irq_a[2] != (u32)(uintptr_t)tisp_af_interrupt_static))
		DIE("init", "irq ref");
	if (R_ref.km_n && (R_ref.km_size[0] != R_our.km_size[0] || R_ref.km_flag[0] != R_our.km_flag[0]))
		DIE("init", "kmalloc args");
	if (R_ref.vm_n) {
		for (i = 0, j = 0; i < R_ref.vm_n && i < 16; i++)
			if (R_ref.vm_size[i] == SB || R_ref.vm_size[i] == BB || R_ref.vm_size[i] == PB)
				rl[j++ % 3] = R_ref.vm_size[i];
		for (i = 0, j = 0; i < R_our.vm_n && i < 16; i++)
			if (R_our.vm_size[i] == SB || R_our.vm_size[i] == BB || R_our.vm_size[i] == PB)
				ol[j++ % 3] = R_our.vm_size[i];
		if (memcmp(rl, ol, sizeof(rl)))
			DIE("init", "vmalloc sizes");
	}
	cmp_state("init");
}

static u32 tok_r(u32 p, u32 ch)
{
	if (p == T32O_PTR(fS[ch])) return 1;
	if (p == T32O_PTR(fB[ch])) return 2;
	if (p == T32O_PTR(fP[ch])) return 3;
	if (p == T32O_PTR(vctx[ch])) return 4;
	if (p == T32O_PTR(vdma[ch])) return 5;
	if (p == T32O_PTR(dbuf[ch])) return 6;
	return 99;
}
static u32 tok_o(u32 p, u32 ch)
{
	if (p == (u32)(uintptr_t)fS[ch]) return 1;
	if (p == (u32)(uintptr_t)fB[ch]) return 2;
	if (p == (u32)(uintptr_t)fP[ch]) return 3;
	if (p == (u32)(uintptr_t)&octx[ch]) return 4;
	if (p == (u32)(uintptr_t)&odma[ch]) return 5;
	if (p == (u32)(uintptr_t)dbuf[ch]) return 6;
	return 99;
}

static void t_deinit(void)
{
	u32 ch = rnd() & 3, o, s1620[NCH], s1610[NCH];
	unsigned int i;

	setup();
	if (rnd() % 5 == 0) put32(vctx[ch], 4, 0);
	if (rnd() % 5 == 0) put32(vctx[ch], 0, 0);
	if (rnd() % 5 == 0) put32(vctx[ch], 12, 0);
	if (rnd() % 5 == 0) put32(vdma[ch], 4, 0);
	if (rnd() % 6 == 0) t32_data_e1620[ch] = 0;
	if (rnd() % 6 == 0) t32_data_e1610[ch] = 0;
	arm();
	for (o = 0; o < NCH; o++) {
		s1620[o] = t32_data_e1620[o];
		s1610[o] = t32_data_e1610[o];
	}
	RC(tisp_af_deinit, ch, 0, 0, 0);
	snap("deinit");
	t32_af_deinit(&ost, ch);
	if (t32_data_e1620[ch] || t32_data_e1610[ch] || ost.ctx[ch] || ost.dma[ch])
		DIE("deinit", "slots not cleared");
	for (o = 0; o < NCH; o++)
		if (o != ch && (t32_data_e1620[o] != s1620[o] || t32_data_e1610[o] != s1610[o] ||
				(ost.ctx[o] != NULL) != (s1620[o] != 0) || (ost.dma[o] != NULL) != (s1610[o] != 0)))
			DIE("deinit", "decoy slot %u", o);
	if (get32(vctx[ch], 0) != (u32)(uintptr_t)octx[ch].blk || get32(vctx[ch], 4) != (u32)(uintptr_t)octx[ch].regs ||
	    get32(vctx[ch], 12) != (u32)(uintptr_t)octx[ch].par || get32(vdma[ch], 4) != (u32)(uintptr_t)odma[ch].buf)
		DIE("deinit", "freed pointer fields");
	if (R_ref.vf_n || R_ref.kf_n) {
		if (R_ref.vf_n != R_our.vf_n || R_ref.kf_n != R_our.kf_n)
			DIE("deinit", "free counts %u/%u vs %u/%u", R_ref.vf_n, R_ref.kf_n, R_our.vf_n, R_our.kf_n);
		for (i = 0; i < R_ref.vf_n && i < 16; i++)
			if (tok_r(R_ref.vf_ptr[i], ch) != tok_o(R_our.vf_ptr[i], ch))
				DIE("deinit", "vfree #%u", i);
		for (i = 0; i < R_ref.kf_n && i < 8; i++)
			if (tok_r(R_ref.kf_ptr[i], ch) != tok_o(R_our.kf_ptr[i], ch))
				DIE("deinit", "kfree #%u", i);
	}
	cmp_state("deinit");
}

static void t_param(void)
{
	u32 ch = rnd() & 3;
	u8 *out, *out2, *len, *len2, *in;
	int on;

	setup(); arm();
	out = frame(PB + BB); rfill(out, PB + BB);
	out2 = frame(PB + BB); memcpy(out2, out, PB + BB);
	len = frame(4); rfill(len, 4);
	len2 = frame(4); memcpy(len2, len, 4);
	RC(tisp_af_param_array_get, ch, T32O_PTR(out), T32O_PTR(len), 0);
	snap("param_get");
	t32_af_param_array_get(&ost, ch, out2, (u32 *)len2);
	memdiff("param_get", "out", ch, out, out2, PB + BB);
	memdiff("param_get", "len", ch, len, len2, 4);
	chk("param_get", out, PB + BB); chk("param_get", out2, PB + BB);
	cmp_state("param_get");

	setup();
	in = frame(PB + BB); rfill(in, PB + BB);
	gen_par(in, ch, rnd() & 1);
	arm();
	RC(tisp_af_param_array_set, ch, T32O_PTR(in), 0, 0);
	snap("param_set");
	on = t32_af_param_array_set(&ost, ch, in, ow);
	cmp_writes("param_set", on);
	cmp_state("param_set");
}

static void t_small(void)
{
	u32 ch = rnd() & 3;
	u8 *a, *b, *c, *d, *e, *f;
	int on;

	setup(); arm();
	a = frame(225); rfill(a, 225);
	RC(tisp_af_api_set_weight, ch, T32O_PTR(a), 0, 0);
	snap("set_weight");
	t32_af_api_set_weight(&ost, ch, a);
	cmp_state("set_weight");

	setup(); arm();
	a = frame(225); rfill(a, 225);
	b = frame(225); memcpy(b, a, 225);
	RC(tisp_af_api_get_weight, ch, T32O_PTR(a), 0, 0);
	snap("get_weight");
	t32_af_api_get_weight(&ost, ch, b);
	memdiff("get_weight", "out", ch, a, b, 225);
	chk("get_weight", a, 225); chk("get_weight", b, 225);
	cmp_state("get_weight");

	setup(); arm();
	a = frame(4); b = frame(4); c = frame(1); d = frame(4); e = frame(4); f = frame(1);
	rfill(a, 4); rfill(b, 4); rfill(c, 1);
	memcpy(d, a, 4); memcpy(e, b, 4); memcpy(f, c, 1);
	if ((u32)RC(tisp_af_api_get_metric, ch, T32O_PTR(a), T32O_PTR(b), T32O_PTR(c)) != 0)
		DIE("get_metric", "ret");
	snap("get_metric");
	t32_af_api_get_metric(&ost, ch, (u32 *)d, (u32 *)e, f);
	memdiff("get_metric", "m1", ch, a, d, 4);
	memdiff("get_metric", "m2", ch, b, e, 4);
	memdiff("get_metric", "st", ch, c, f, 1);
	cmp_state("get_metric");

	setup(); arm();
	a = frame(24); rfill(a, 24);
	b = frame(24); memcpy(b, a, 24);
	RC(tisp_af_api_get_attr, ch, T32O_PTR(a), 0, 0);
	snap("get_attr");
	t32_af_api_get_attr(&ost, ch, b);
	memdiff("get_attr", "out", ch, a, b, 24);
	chk("get_attr", a, 24); chk("get_attr", b, 24);
	cmp_state("get_attr");

	setup();
	gen_par(fP[ch], ch, rnd() & 1);
	arm();
	a = frame(24); rfill(a, 24);
	a[18] = (u8)(rnd() % 16); a[19] = (u8)(rnd() % 16); a[20] = (u8)dim(); a[21] = (u8)dim();
	if ((u32)RC(tisp_af_api_set_attr, ch, T32O_PTR(a), 0, 0) != 0)
		DIE("set_attr", "ret");
	snap("set_attr");
	on = t32_af_api_set_attr(&ost, ch, a, ow);
	cmp_writes("set_attr", on);
	cmp_state("set_attr");

	setup(); arm();
	a = frame(T32_AF_STATIS_BYTES); rfill(a, T32_AF_STATIS_BYTES);
	b = frame(T32_AF_STATIS_BYTES); memcpy(b, a, T32_AF_STATIS_BYTES);
	RC(tisp_af_api_get_statis_info, ch, T32O_PTR(a), 0, 0);
	snap("statis_info");
	t32_af_api_get_statis_info(&ost, ch, b);
	memdiff("statis_info", "out", ch, a, b, T32_AF_STATIS_BYTES);
	cmp_state("statis_info");

	setup(); arm();
	RC(tisp_af_wake_up, ch, 0, 0, 0);
	snap("wake_up");
	on = t32_af_wake_up(&ost, ch, ow);
	cmp_writes("wake_up", on);
	cmp_state("wake_up");
}

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 2000;
	unsigned int c, a, b;
	static const int dd[] = { 0, 1, 7, 8, 9, 15 };

	setup();
	ref_fix();
	for (c = 0; c < n; c++) {
		t_tgain();
		t_all_reg();
		t_par();
		t_stat(-1, -1);
		t_alog();
		t_irq();
		t_malloc();
		t_init();
		t_deinit();
		t_param();
		t_small();
	}
	for (a = 0; a < 6; a++)
		for (b = 0; b < 6; b++)
			t_stat(dd[a], dd[b]);
	printf("t32_af_oracle: %u compared, 0 mismatches\n", compared);
	return 0;
}
