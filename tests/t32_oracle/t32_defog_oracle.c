// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our readable DEFOG port (driver/t32/tx_isp_t32_defog.h)
 * against the unmodified vendor functions (Matt Davis' recovered C, PR #15
 * @ 2e2e06bc, ref/).  Each case builds the same random initial state for 4
 * channels (decoys on the other three), runs the vendor function on the low
 * stack and our port, and compares every register write (address, value,
 * order; d03a0 = 1 runs compare the shadow memory), return values and the
 * whole context / section / parameter block / register block / DMA buffer
 * of every channel, with canary frames around the vendor buffers.
 *
 * Callees without a ref (count_weight, soft, dfg_array_cmp, allocators,
 * event push, ...) are weak stand-ins defined here; both sides run the same
 * code.  tisp_defog_interp_trans_k (9 arguments) is exercised through its
 * callers.  Exit 0 = all equal, 1 = mismatch.
 */
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-prototypes"
#pragma GCC diagnostic ignored "-Wmissing-declarations"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_defog.h"

#define WEAK	__attribute__((weak))
#define NCH	4
#define SECN	704u
#define PARN	3948u
#define P2N	220u
#define BUFN	8192u
#define SHW	4096u
#define CAN	32u
#define MAXW	2048u

/* data objects of the reference that env.c may not define */
WEAK u32 t32_data_e2130[NCH];
WEAK u32 t32_data_e20e0[NCH];
WEAK u32 t32_data_e211c[NCH];
WEAK u32 t32_data_e212c[NCH];
WEAK u32 t32_data_e20f0[16];
WEAK u32 t32_data_dd6c0[128];
/* format string the reference hands to isp_printf; never read */
WEAK u8 t32_data_c99fc[64];
WEAK u8 t32_data_c9a2c[64];
WEAK u8 t32_data_c9998[64];
WEAK u8 t32_data_c99cc[64];
WEAK u8 t32_data_c9980[64];
WEAK u8 t32_data_c9958[64];
WEAK u8 t32_data_bfae0[64];

extern u64 tisp_defog_max_filter(u32, u32, u32, u32);
extern u64 tisp_defog_img_filter9(u32, u32, u32, u32);
extern u64 tisp_defog_img_filter25(u32, u32, u32, u32);
extern u64 tisp_defog_update_trans_k(u32, u32, u32, u32);
extern u64 tisp_defog_write_reg_para1(u32, u32, u32, u32);
extern u64 tisp_defog_write_reg_para2(u32, u32, u32, u32);
extern u64 tisp_defog_addr_write_reg_para3(u32, u32, u32, u32);
extern u64 tisp_defog_write_reg_para3(u32, u32, u32, u32);
extern u64 tisp_defog_update_reg_para2(u32, u32, u32, u32);
extern u64 tisp_defog_par(u32, u32, u32, u32);
extern u64 tisp_defog_get_data(u32, u32, u32, u32);
extern u64 tisp_defog_interrupt_static(u32, u32, u32, u32);
extern u64 tisp_defog_init(u32, u32, u32, u32);
extern u64 tisp_defog_deinit(u32, u32, u32, u32);
extern u64 tisp_defog_ev_update(u32, u32, u32, u32, u32, u32);
extern u64 tisp_defog_wdr_en(u32, u32, u32, u32);
extern u64 tisp_defog_dn_params_refresh(u32, u32, u32, u32);
extern u64 tisp_defog_param_array_get(u32, u32, u32, u32);
extern u64 tisp_defog_param_array_set(u32, u32, u32, u32);
extern int tisp_defog_api_set_strength(void);
extern int tisp_defog_api_get_strength(void);
extern u64 tisp_defog_strength_itp(u32, u32, u32, u32);
extern u64 tisp_defog_wake_up(u32, u32, u32, u32);

#define VC(f, a, b, c, d, e) \
	t32o_call((t32o_fn)(void (*)(void))(f), (u32)(a), (u32)(b), (u32)(c), (u32)(d), (u32)(e))

static u32 rs = 0x5eed1e7u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static u32 rd32(const u8 *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24;
}

static void wr32(u8 *p, u32 v)
{
	p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
}

/* ---- stand-ins for callees without a ref (both sides use the same code) ---- */
enum { C_DMA, C_EV, C_IRQ, C_KF, C_VF, NC };
static unsigned cnt[2][NC];
static u32 fr[2][2][16];
static u32 g_status;
static int g_mode;
static u32 g_saved[11];
static int o_evcb[NCH];

static void *df_zalloc(u32 n)
{
	void *p = t32o_alloc(n ? n : 4);

	if (!p) {
		fprintf(stderr, "t32_defog_oracle: allocation pool exhausted\n");
		exit(2);
	}
	memset(p, 0, n ? n : 4);
	return p;
}

static void cw_core(u32 ch, u32 h, u32 w, u32 g, u8 *p[7])
{
	static const unsigned sz[7] = { 48, 48, 32, 32, 32, 32, 32 };
	unsigned i, j;

	for (i = 0; i < 7; i++)
		for (j = 0; j < sz[i]; j++)
			p[i][j] = (u8)(ch * 31 + h * 7 + w * 13 + g * 5 + i * 17 + j * 3);
}

static void soft_core(u8 *par)
{
	par[2421] = (u8)((par[2420] * 3 + 1) ^ par[2422]);
	par[2425] = par[2421] ^ 0x5a;
}

WEAK u64 defog_count_weight35abc(u32 a0, u32 a1, u32 a2, u32 a3, u32 s0, u32 s1, u32 s2,
				 u32 s3, u32 s4, u32 s5, u32 s6)
{
	u8 *p[7] = { T32O_HOST(s0), T32O_HOST(s1), T32O_HOST(s2), T32O_HOST(s3),
		     T32O_HOST(s4), T32O_HOST(s5), T32O_HOST(s6) };

	cw_core(a0, a1, a2, a3, p);
	return 0;
}

WEAK u64 Tiziano_defog_soft(u32 a0, u32 a1, u32 a2, u32 a3)
{
	soft_core(T32O_HOST(a0));
	return 0;
}

WEAK u64 dfg_array_cmp(u32 a0, u32 a1, u32 a2, u32 a3)
{
	return memcmp(T32O_HOST(a0), T32O_HOST(a1), a2) == 0;
}

WEAK u64 private_vmalloc(u32 a0, u32 a1, u32 a2, u32 a3)
{
	return T32O_PTR(df_zalloc(a0));
}

WEAK u64 private_kmalloc(u32 a0, u32 a1, u32 a2, u32 a3)
{
	return T32O_PTR(df_zalloc(a0));
}

WEAK u64 private_kfree(u32 a0, u32 a1, u32 a2, u32 a3)
{
	if (cnt[0][C_KF] < 16)
		fr[0][0][cnt[0][C_KF]] = a0;
	cnt[0][C_KF]++;
	return 0;
}

WEAK u64 private_vfree(u32 a0, u32 a1, u32 a2, u32 a3)
{
	if (cnt[0][C_VF] < 16)
		fr[0][1][cnt[0][C_VF]] = a0;
	cnt[0][C_VF]++;
	return 0;
}

WEAK u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3)
{
	return 0;
}

WEAK u64 system_irq_func_set(u32 a0, u32 a1, u32 a2, u32 a3)
{
	cnt[0][C_IRQ]++;
	return 0;
}

WEAK u64 private_dma_cache_sync(u32 a0, u32 a1, u32 a2, u32 a3)
{
	cnt[0][C_DMA]++;
	return 0;
}

WEAK u64 tisp_event_push(u32 a0, u32 a1, u32 a2, u32 a3)
{
	cnt[0][C_EV]++;
	return 0;
}

/* our side of the callbacks */
static void o_cw(u32 ch, u32 h, u32 w, u32 g, u8 *a, u8 *b, u8 *c, u8 *d, u8 *e, u8 *f, u8 *g2)
{
	u8 *p[7];

	p[0] = a; p[1] = b; p[2] = c; p[3] = d; p[4] = e; p[5] = f; p[6] = g2;
	cw_core(ch, h, w, g, p);
}

static void o_soft(struct t32_defog_par *p) { soft_core(p->b); }
static u32 o_rd(u32 r) { (void)r; return g_status; }
static void o_dma(const u8 *b, u32 n) { (void)b; (void)n; cnt[1][C_DMA]++; }
static void o_evp(u32 ch, const u32 *ev) { (void)ch; (void)ev; cnt[1][C_EV]++; }
static void *o_vm(u32 n) { return df_zalloc(n); }
static void *o_km(u32 n, u32 g) { (void)g; return df_zalloc(n); }
static void o_vf(void *p)
{
	if (cnt[1][C_VF] < 16)
		fr[1][1][cnt[1][C_VF]] = (u32)(uintptr_t)p;
	cnt[1][C_VF]++;
}
static void o_kf(void *p)
{
	if (cnt[1][C_KF] < 16)
		fr[1][0][cnt[1][C_KF]] = (u32)(uintptr_t)p;
	cnt[1][C_KF]++;
}
static void o_irq(u32 ch, u32 id) { (void)ch; (void)id; cnt[1][C_IRQ]++; }
static void o_cb(u32 ch, u32 ev) { if (ev == 3 && ch < NCH) o_evcb[ch] = 1; }

static const struct t32_defog_ops ops = {
	o_cw, o_soft, o_rd, o_dma, o_evp, o_vm, o_km, o_vf, o_kf, o_irq, o_cb
};

/* ---- canary frames ---- */
static struct { u8 *p; u32 n; } cl[512];
static int ncl;

static void cpush(u8 *p, u32 n)
{
	cl[ncl].p = p;
	cl[ncl].n = n;
	ncl++;
}

static u8 *cmem(u32 n)
{
	u8 *p = t32o_alloc(n + 2 * CAN);

	if (!p || ncl > 500) {
		fprintf(stderr, "t32_defog_oracle: allocation pool exhausted\n");
		exit(2);
	}
	memset(p, 0xc7, n + 2 * CAN);
	cpush(p, CAN);
	cpush(p + CAN + n, CAN);
	return p + CAN;
}

static int canaries_ok(void)
{
	int i;
	u32 j;

	for (i = 0; i < ncl; i++)
		for (j = 0; j < cl[i].n; j++)
			if (cl[i].p[j] != 0xc7)
				return 0;
	return 1;
}

static u8 *cbase(void)
{
	u8 *b = cmem(T32_DEFOG_SEC_OFF + SECN);

	cpush(b, T32_DEFOG_SEC_OFF);
	return b;
}

/* ---- comparable state of one channel ---- */
enum { FI_NBUF, FI_PHYS, FI_EV, FI_EV2, FI_H, FI_W, FI_MODE, FI_IRQC, FI_GAIN, FI_STR,
       FI_HBUF, FI_HPAR, FI_HP2, FI_HCTX, FI_VSIZE, FI_VBUF, FI_VZERO, FI_IRQT, NF };

struct img {
	u32 f[20];
	u8 sec[SECN];
	u8 par[PARN];
	u8 p2[P2N];
	u8 buf[BUFN];
};

static u32 poff(u32 v, u32 base) { return v ? v - base : 0xffffffffu; }
static u32 ooff(const u8 *p, const u8 *base) { return p ? (u32)(p - base) : 0xffffffffu; }

static void snap_v(struct img *im, u32 k, int isinit)
{
	u32 cp = t32_data_e2130[k], sb, i;
	const u8 *c;

	memset(im, 0, sizeof *im);
	if (t32_data_e20e0[k]) {
		im->f[FI_VBUF] = 1;
		im->f[FI_VSIZE] = t32_data_e211c[k];
		if (isinit) {
			const u8 *v = T32O_HOST(t32_data_e20e0[k]);

			im->f[FI_VZERO] = 1;
			for (i = 0; i < t32_data_e211c[k]; i++)
				if (v[i])
					im->f[FI_VZERO] = 0;
		}
	}
	if (!cp)
		return;
	c = T32O_HOST(cp);
	im->f[FI_NBUF] = rd32(c);
	im->f[FI_PHYS] = isinit ? rd32(c + 8) - rd32(c + 4) : rd32(c + 8);
	im->f[FI_EV] = rd32(c + 24);
	im->f[FI_EV2] = rd32(c + 28);
	im->f[FI_H] = rd32(c + 32);
	im->f[FI_W] = rd32(c + 36);
	im->f[FI_MODE] = rd32(c + 40);
	im->f[FI_IRQC] = rd32(c + 44);
	im->f[FI_GAIN] = c[48];
	im->f[FI_STR] = rd32(c + 52);
	im->f[FI_HBUF] = rd32(c + 4) != 0;
	im->f[FI_HPAR] = rd32(c + 16) != 0;
	im->f[FI_HP2] = rd32(c + 20) != 0;
	im->f[FI_HCTX] = 1;
	sb = rd32(c + 12);
	memcpy(im->sec, T32O_HOST(sb), SECN);
	if (rd32(c + 16)) {
		memcpy(im->par, T32O_HOST(rd32(c + 16)), PARN);
		wr32(im->par + 2176, poff(rd32(im->par + 2176), sb));
		wr32(im->par + 2180, poff(rd32(im->par + 2180), sb));
	}
	if (rd32(c + 20)) {
		memcpy(im->p2, T32O_HOST(rd32(c + 20)), P2N);
		for (i = 0; i < 3; i++)
			wr32(im->p2 + 68 + 4 * i, poff(rd32(im->p2 + 68 + 4 * i), sb));
	}
	if (rd32(c + 4) && !isinit)
		memcpy(im->buf, T32O_HOST(rd32(c + 4)), BUFN);
}

static void snap_o(struct img *im, const struct t32_defog_chan *c, const struct img *ini, int isinit)
{
	const struct t32_defog_ctx *x = c->ctx;
	u32 i;

	memset(im, 0, sizeof *im);
	if (c->vbuf) {
		im->f[FI_VBUF] = 1;
		im->f[FI_VSIZE] = c->vsize;
		if (isinit) {
			im->f[FI_VZERO] = 1;
			for (i = 0; i < c->vsize; i++)
				if (c->vbuf[i])
					im->f[FI_VZERO] = 0;
		}
	}
	if (!x)
		return;
	im->f[FI_NBUF] = x->nbuf;
	im->f[FI_PHYS] = isinit ? x->buf_phys - (u32)(uintptr_t)x->buf : x->buf_phys;
	im->f[FI_EV] = x->ev;
	im->f[FI_EV2] = x->ev2;
	im->f[FI_H] = x->height;
	im->f[FI_W] = x->width;
	im->f[FI_MODE] = x->mode;
	im->f[FI_IRQC] = x->irq_cnt;
	im->f[FI_GAIN] = x->gain_mode;
	im->f[FI_STR] = x->strength;
	im->f[FI_HBUF] = x->buf != NULL;
	im->f[FI_HPAR] = x->par != NULL;
	im->f[FI_HP2] = x->p2 != NULL;
	im->f[FI_HCTX] = 1;
	memcpy(im->sec, x->sec, SECN);
	if (x->par) {
		memcpy(im->par, x->par->b, PARN);
		wr32(im->par + 2176, ooff(x->par->tbl_a, x->sec));
		wr32(im->par + 2180, ooff(x->par->tbl_b, x->sec));
	}
	if (x->p2) {
		const struct t32_defog_p2 *p = x->p2;

		if (!isinit)
			memcpy(im->p2 + 116, ini->p2 + 116, P2N - 116);
		for (i = 0; i < 17; i++)
			wr32(im->p2 + 4 * i, p->w[i]);
		wr32(im->p2 + 68, ooff(p->pa, x->sec));
		wr32(im->p2 + 72, ooff(p->pb, x->sec));
		wr32(im->p2 + 76, ooff(p->pc, x->sec));
		memcpy(im->p2 + 80, p->flag, 4);
		memcpy(im->p2 + 84, p->tbl, 32);
	}
	if (x->buf && !isinit)
		memcpy(im->buf, x->buf, BUFN);
}

/* ---- builders ---- */
static struct img ini[NCH];
static u32 g_sh[NCH][SHW];
static u32 *vsh[NCH];
static u32 o_sh[NCH][SHW];
static struct t32_defog_chan oc[NCH];
static u8 *o_base;
static u8 x_in[SECN];

static void bld_v(u32 k, const struct img *im, int isinit)
{
	u8 *sec, *par, *p2, *buf, *c;
	u32 i;

	t32_data_e2130[k] = 0;
	t32_data_e20e0[k] = 0;
	t32_data_e211c[k] = 0;
	t32_data_dd0a4[k] = 0;
	if (isinit) {
		u8 *b = cbase();

		memcpy(b + T32_DEFOG_SEC_OFF, im->sec, SECN);
		t32_data_dd0a4[k] = T32O_PTR(b);
		return;
	}
	sec = cmem(SECN);
	memcpy(sec, im->sec, SECN);
	par = cmem(PARN);
	memcpy(par, im->par, PARN);
	wr32(par + 2176, 0);
	wr32(par + 2180, 0);
	p2 = cmem(P2N);
	memcpy(p2, im->p2, P2N);
	for (i = 0; i < 3; i++)
		wr32(p2 + 68 + 4 * i, T32O_PTR(sec) + rd32(im->p2 + 68 + 4 * i));
	buf = cmem(BUFN);
	memcpy(buf, im->buf, BUFN);
	c = cmem(56);
	memset(c, 0, 56);
	wr32(c, im->f[FI_NBUF]);
	wr32(c + 4, T32O_PTR(buf));
	wr32(c + 8, im->f[FI_PHYS]);
	wr32(c + 12, T32O_PTR(sec));
	wr32(c + 16, T32O_PTR(par));
	wr32(c + 20, T32O_PTR(p2));
	wr32(c + 24, im->f[FI_EV]);
	wr32(c + 28, im->f[FI_EV2]);
	wr32(c + 32, im->f[FI_H]);
	wr32(c + 36, im->f[FI_W]);
	wr32(c + 40, im->f[FI_MODE]);
	wr32(c + 44, im->f[FI_IRQC]);
	c[48] = (u8)im->f[FI_GAIN];
	wr32(c + 52, im->f[FI_STR]);
	t32_data_e2130[k] = T32O_PTR(c);
	if (im->f[FI_VBUF]) {
		u8 *v = cmem(im->f[FI_VSIZE]);

		memset(v, 0x5a, im->f[FI_VSIZE]);
		t32_data_e20e0[k] = T32O_PTR(v);
		t32_data_e211c[k] = im->f[FI_VSIZE];
	}
}

static void bld_o(u32 k, const struct img *im, int isinit)
{
	struct t32_defog_chan *c = &oc[k];
	struct t32_defog_ctx *x;
	struct t32_defog_par *par;
	struct t32_defog_p2 *p2;
	u32 i;

	memset(c, 0, sizeof *c);
	c->ops = &ops;
	c->shadow = (u8)(g_mode == 1);
	memcpy(c->saved, g_saved, sizeof c->saved);
	if (isinit) {
		o_base = cbase();
		memcpy(o_base + T32_DEFOG_SEC_OFF, im->sec, SECN);
		return;
	}
	x = (struct t32_defog_ctx *)cmem(sizeof(*x));
	memset(x, 0, sizeof(*x));
	x->sec = cmem(SECN);
	memcpy(x->sec, im->sec, SECN);
	par = (struct t32_defog_par *)cmem(sizeof(*par));
	memset(par, 0, sizeof(*par));
	memcpy(par->b, im->par, PARN);
	p2 = (struct t32_defog_p2 *)cmem(sizeof(*p2));
	memset(p2, 0, sizeof(*p2));
	for (i = 0; i < 17; i++)
		p2->w[i] = rd32(im->p2 + 4 * i);
	p2->pa = x->sec + rd32(im->p2 + 68);
	p2->pb = x->sec + rd32(im->p2 + 72);
	p2->pc = x->sec + rd32(im->p2 + 76);
	memcpy(p2->flag, im->p2 + 80, 4);
	memcpy(p2->tbl, im->p2 + 84, 32);
	x->buf = cmem(BUFN);
	memcpy(x->buf, im->buf, BUFN);
	x->par = par;
	x->p2 = p2;
	x->nbuf = im->f[FI_NBUF];
	x->buf_phys = im->f[FI_PHYS];
	x->ev = im->f[FI_EV];
	x->ev2 = im->f[FI_EV2];
	x->height = im->f[FI_H];
	x->width = im->f[FI_W];
	x->mode = im->f[FI_MODE];
	x->irq_cnt = im->f[FI_IRQC];
	x->gain_mode = (u8)im->f[FI_GAIN];
	x->strength = im->f[FI_STR];
	c->ctx = x;
	if (im->f[FI_VBUF]) {
		c->vbuf = cmem(im->f[FI_VSIZE]);
		memset(c->vbuf, 0x5a, im->f[FI_VSIZE]);
		c->vsize = im->f[FI_VSIZE];
	}
}

static void sh_v(void)
{
	u8 *shbase = cmem(NCH * 256 + 256);
	u32 k;

	for (k = 0; k < NCH; k++) {
		u32 *s = (u32 *)cmem(SHW * 4);

		memcpy(s, g_sh[k], SHW * 4);
		wr32(shbase + 200 + k * 256, T32O_PTR(s));
		vsh[k] = s;
	}
	t32_data_da844[0] = T32O_PTR(shbase);
}

/* ---- case description ---- */
enum { F_TRANSK, F_PAR, F_PARA1, F_PARA2, F_ADDR3, F_PARA3, F_UPD2, F_GETD, F_IRQ, F_INIT,
       F_DEINIT, F_EV, F_WDR, F_DN, F_PGET, F_PSET, F_WAKE, NFN };
static const char *const fname[NFN] = {
	"update_trans_k", "par", "write_reg_para1", "write_reg_para2", "addr_write_reg_para3",
	"write_reg_para3", "update_reg_para2", "get_data", "interrupt_static", "init", "deinit",
	"ev_update", "wdr_en", "dn_params_refresh", "param_array_get", "param_array_set", "wake_up"
};
static const int fmodes[NFN] = { 3, 3, 3, 3, 3, 1, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
static const int fret[NFN] = { -1, 0, -1, -1, -1, -1, -1, -1, 1, 0, 0, 0, 0, 0, 0, 0, -1 };

static struct img v_im[NCH], o_im[NCH], vpre[NCH], opre[NCH];
static u32 v_sh[NCH][SHW];
struct vw { u32 reg, val; };
static struct vw v_w[MAXW];
static struct t32_front_write o_w[MAXW];
static unsigned v_n, o_n;
static u8 v_x[SECN], o_x[SECN];
static u32 v_len, o_len;
static unsigned ncases;

static void fail(int fn, u32 ch, const char *what, u32 idx)
{
	printf("MISMATCH %s ch %u: %s (#%u)\n", fname[fn], ch, what, idx);
	exit(1);
}

static void cmp_img(int fn, const char *who, u32 k, const struct img *a, const struct img *b)
{
	u32 i;

	for (i = 0; i < NF; i++)
		if (a->f[i] != b->f[i]) {
			printf("MISMATCH %s ch %u (%s): ctx field %u a %08x b %08x\n", fname[fn], k, who,
			       i, a->f[i], b->f[i]);
			exit(1);
		}
#define CB(nm, arr, N) \
	for (i = 0; i < (N); i++) \
		if (a->arr[i] != b->arr[i]) { \
			printf("MISMATCH %s ch %u (%s): %s[%u] a %02x b %02x\n", fname[fn], k, who, nm, i, \
			       a->arr[i], b->arr[i]); \
			exit(1); \
		}
	CB("sec", sec, SECN)
	CB("par", par, PARN)
	CB("p2", p2, P2N)
	CB("buf", buf, BUFN)
#undef CB
}

static int kind(u32 p, const u32 *objs)
{
	int i;

	for (i = 0; i < 5; i++)
		if (objs[i] && objs[i] == p)
			return i + 1;
	return 9;
}

static void run_one(int fn, u32 ch, int mode, const u32 *a)
{
	int isinit = fn == F_INIT;
	u32 k, i, vphys = 0, ophys = 0, vptr[5] = { 0, 0, 0, 0, 0 }, optr[5] = { 0, 0, 0, 0, 0 };
	u32 e212c0 = rnd(), vcb = 0;
	u64 vrv = 0;
	int orv = 0;
	struct t32_defog_chan *c;
	u8 *vin, *vout, *vlen, *vsz;

	/* ---- vendor ---- */
	t32o_reset();
	ncl = 0;
	memset(cnt, 0, sizeof cnt);
	memset(fr, 0, sizeof fr);
	g_mode = mode;
	t32_data_d03a0[0] = (u8)mode;
	t32o_rd_hook = o_rd;
	memset(t32_data_dd6c0, 0, sizeof t32_data_dd6c0);
	memcpy(t32_data_e20f0, g_saved, sizeof g_saved);
	t32_data_e212c[0] = e212c0;
	for (k = 0; k < NCH; k++)
		bld_v(k, &ini[k], isinit && k == ch);
	sh_v();
	vin = cmem(SECN);
	vout = cmem(SECN);
	vlen = cmem(4);
	vsz = cmem(8);
	memcpy(vin, x_in, SECN);
	memset(vout, 0x77, SECN);
	wr32(vlen, 0xdeadbeefu);
	wr32(vsz, a[0]);
	wr32(vsz + 4, a[1]);
	for (k = 0; k < NCH; k++)
		snap_v(&vpre[k], k, 0);
	if (!isinit && t32_data_e2130[ch]) {
		const u8 *cx = T32O_HOST(t32_data_e2130[ch]);

		vptr[0] = rd32(cx + 4);
		vptr[1] = rd32(cx + 16);
		vptr[2] = rd32(cx + 20);
		vptr[3] = t32_data_e2130[ch];
		vptr[4] = t32_data_e20e0[ch];
	}
	switch (fn) {
	case F_TRANSK: vrv = VC(tisp_defog_update_trans_k, ch, 0, 0, 0, 0); break;
	case F_PAR: vrv = VC(tisp_defog_par, ch, 0, 0, 0, 0); break;
	case F_PARA1: vrv = VC(tisp_defog_write_reg_para1, ch, 0, 0, 0, 0); break;
	case F_PARA2: vrv = VC(tisp_defog_write_reg_para2, ch, 0, 0, 0, 0); break;
	case F_ADDR3: vrv = VC(tisp_defog_addr_write_reg_para3, ch, 0, 0, 0, 0); break;
	case F_PARA3: vrv = VC(tisp_defog_write_reg_para3, ch, 0, 0, 0, 0); break;
	case F_UPD2: vrv = VC(tisp_defog_update_reg_para2, ch, 0, 0, 0, 0); break;
	case F_GETD:
		vrv = VC(tisp_defog_get_data, ch,
			 rd32((const u8 *)T32O_HOST(t32_data_e2130[ch]) + 4) + a[0], 0, 0, 0);
		break;
	case F_IRQ:
		g_status = a[2];
		vrv = VC(tisp_defog_interrupt_static, ch, 0, 0, 0, 0);
		break;
	case F_INIT: vrv = VC(tisp_defog_init, ch, T32O_PTR(vsz), 0, 0, 0); break;
	case F_DEINIT: vrv = VC(tisp_defog_deinit, ch, 0, 0, 0, 0); break;
	case F_EV:
		t32o_arg5 = a[3];
		vrv = VC(tisp_defog_ev_update, ch, 0, a[0], a[1], a[2]);
		break;
	case F_WDR: vrv = VC(tisp_defog_wdr_en, ch, a[0], 0, 0, 0); break;
	case F_DN: vrv = VC(tisp_defog_dn_params_refresh, ch, a[0], 0, 0, 0); break;
	case F_PGET:
		vrv = VC(tisp_defog_param_array_get, ch, T32O_PTR(vout), T32O_PTR(vlen), 0, 0);
		break;
	case F_PSET: vrv = VC(tisp_defog_param_array_set, ch, T32O_PTR(vin), 0, 0, 0); break;
	case F_WAKE: vrv = VC(tisp_defog_wake_up, ch, 0, 0, 0, 0); break;
	}
	v_n = t32o_log.n;
	if (v_n > MAXW)
		fail(fn, ch, "too many vendor writes", v_n);
	for (i = 0; i < v_n; i++) {
		v_w[i].reg = t32o_log.w[i].reg;
		v_w[i].val = t32o_log.w[i].val;
	}
	if (!canaries_ok())
		fail(fn, ch, "vendor canary overwritten", 0);
	for (k = 0; k < NCH; k++) {
		snap_v(&v_im[k], k, isinit && k == ch);
		memcpy(v_sh[k], vsh[k], SHW * 4);
	}
	if (isinit && t32_data_e2130[ch])
		vphys = rd32((const u8 *)T32O_HOST(t32_data_e2130[ch]) + 8);
	v_im[ch].f[FI_IRQT] = t32_data_e212c[0];
	memcpy(v_x, vout, SECN);
	v_len = rd32(vlen);
	vcb = t32_data_dd6c0[ch * 18 + 3];

	/* ---- ours ---- */
	t32o_reset();
	ncl = 0;
	memset(cnt[1], 0, sizeof cnt[1]);
	memset(fr[1], 0, sizeof fr[1]);
	memset(o_evcb, 0, sizeof o_evcb);
	t32o_rd_hook = o_rd;
	for (k = 0; k < NCH; k++)
		bld_o(k, &ini[k], isinit && k == ch);
	memcpy(o_sh, g_sh, sizeof o_sh);
	for (k = 0; k < NCH; k++)
		oc[k].irq_total = e212c0;
	memcpy(o_x, vout, 0);
	memset(o_x, 0x77, SECN);
	o_len = 0xdeadbeefu;
	for (k = 0; k < NCH; k++)
		snap_o(&opre[k], &oc[k], &ini[k], 0);
	c = &oc[ch];
	if (!isinit && c->ctx) {
		optr[0] = (u32)(uintptr_t)c->ctx->buf;
		optr[1] = (u32)(uintptr_t)c->ctx->par;
		optr[2] = (u32)(uintptr_t)c->ctx->p2;
		optr[3] = (u32)(uintptr_t)c->ctx;
		optr[4] = (u32)(uintptr_t)c->vbuf;
	}
	o_n = 0;
	switch (fn) {
	case F_TRANSK: t32_defog_update_trans_k(c, ch); break;
	case F_PAR: orv = t32_defog_par(c, ch, o_w, &o_n); break;
	case F_PARA1: t32_defog_write_reg_para1(c, ch, o_w, &o_n); break;
	case F_PARA2: t32_defog_write_reg_para2(c, ch, o_w, &o_n); break;
	case F_ADDR3: t32_defog_addr_write_reg_para3(c, ch, o_w, &o_n); break;
	case F_PARA3: t32_defog_write_reg_para3(c, ch, o_w, &o_n); break;
	case F_UPD2: t32_defog_update_reg_para2(c, ch); break;
	case F_GETD: t32_defog_get_data(c->ctx->par, c->ctx->buf + a[0]); break;
	case F_IRQ:
		g_status = a[2];
		orv = t32_defog_interrupt_static(c, ch);
		break;
	case F_INIT: {
		u32 sz[2];

		sz[0] = a[0];
		sz[1] = a[1];
		orv = t32_defog_init(c, ch, sz, o_base, o_w, &o_n);
		break;
	}
	case F_DEINIT: orv = t32_defog_deinit(c); break;
	case F_EV: orv = t32_defog_ev_update(c, ch, a[0], a[1], a[2], a[3]); break;
	case F_WDR: orv = t32_defog_wdr_en(c, ch, a[0], o_w, &o_n); break;
	case F_DN: orv = t32_defog_dn_params_refresh(c, ch, a[0], o_w, &o_n); break;
	case F_PGET: orv = t32_defog_param_array_get(c, o_x, &o_len); break;
	case F_PSET: orv = t32_defog_param_array_set(c, ch, x_in, o_w, &o_n); break;
	case F_WAKE: t32_defog_wake_up(c, ch, o_w, &o_n); break;
	}
	if (o_n > MAXW)
		fail(fn, ch, "too many port writes", o_n);
	if (!canaries_ok())
		fail(fn, ch, "port canary overwritten", 0);
	for (k = 0; k < NCH; k++)
		snap_o(&o_im[k], &oc[k], &ini[k], isinit && k == ch);
	if (isinit && c->ctx)
		ophys = c->ctx->buf_phys;
	o_im[ch].f[FI_IRQT] = oc[ch].irq_total;

	/* ---- compare ---- */
	ncases++;
	if (fret[fn] >= 0 && ((u32)vrv != (u32)fret[fn] || orv != fret[fn]))
		fail(fn, ch, "return value", (u32)vrv);
	if (mode == 1) {
		if (v_n)
			fail(fn, ch, "vendor logged writes in shadow mode", v_n);
		for (i = 0; i < o_n; i++) {
			if (o_w[i].reg >= SHW)
				fail(fn, ch, "port shadow index out of range", i);
			o_sh[ch][o_w[i].reg] = o_w[i].val;
		}
		for (k = 0; k < NCH; k++)
			for (i = 0; i < SHW; i++)
				if (v_sh[k][i] != o_sh[k][i]) {
					printf("MISMATCH %s ch %u: shadow[%u][%u] vendor %08x ours %08x\n",
					       fname[fn], ch, k, i, v_sh[k][i], o_sh[k][i]);
					exit(1);
				}
	} else {
		if (v_n != o_n) {
			printf("MISMATCH %s ch %u: writes vendor %u ours %u\n", fname[fn], ch, v_n, o_n);
			exit(1);
		}
		for (i = 0; i < v_n; i++) {
			u32 vv = v_w[i].val, ov = o_w[i].val;

			if (isinit && v_w[i].reg >= 0xa210 && v_w[i].reg <= 0xa21c) {
				vv -= vphys;
				ov -= ophys;
			}
			if (v_w[i].reg != o_w[i].reg || vv != ov) {
				printf("MISMATCH %s ch %u: write #%u vendor %05x=%08x ours %05x=%08x\n",
				       fname[fn], ch, i, v_w[i].reg, v_w[i].val, o_w[i].reg, o_w[i].val);
				exit(1);
			}
		}
		if (memcmp(v_sh, g_sh, sizeof v_sh))
			fail(fn, ch, "vendor shadow changed in direct mode", 0);
	}
	if (isinit) {
		v_im[ch].f[FI_EV2] = o_im[ch].f[FI_EV2] = 0;
		v_im[ch].f[FI_STR] = o_im[ch].f[FI_STR] = 0;
	}
	for (k = 0; k < NCH; k++) {
		cmp_img(fn, "vendor vs port", k, &v_im[k], &o_im[k]);
		if (k != ch) {
			cmp_img(fn, "vendor decoy", k, &v_im[k], &vpre[k]);
			cmp_img(fn, "port decoy", k, &o_im[k], &opre[k]);
		}
	}
	if (memcmp(v_x, o_x, SECN))
		fail(fn, ch, "output buffer", 0);
	if (v_len != o_len)
		fail(fn, ch, "output length", v_len);
	for (i = 0; i < NC; i++)
		if (cnt[0][i] && cnt[0][i] != cnt[1][i])
			fail(fn, ch, "callback count", i);
	for (i = 0; i < 2; i++) {
		unsigned j, m = cnt[0][C_KF + i];

		for (j = 0; j < m && j < 16; j++)
			if (kind(fr[0][i][j], vptr) != kind(fr[1][i][j], optr))
				fail(fn, ch, "freed object", j);
	}
	if (isinit && vcb == (u32)(uintptr_t)tisp_defog_par && !o_evcb[ch])
		fail(fn, ch, "event callback not registered", 0);
}

/* ---- generators ---- */
static u32 pk(const u32 *a, int n)
{
	return a[rnd() % n];
}

static void gen_thr(u8 *sec)
{
	u32 i, v;
	int srt = rnd() % 10 < 7;
	u32 sc = (rnd() % 10 == 0) ? 0x10000000u : 1u;

	v = rnd() % 300;
	for (i = 0; i < 9; i++) {
		v += rnd() % 200;
		wr32(sec + 8 + 4 * i, (srt ? v : rnd() % 2000) * sc);
	}
	v = 5000 + rnd() % 300;
	for (i = 0; i < 9; i++) {
		v -= rnd() % 200;
		wr32(sec + 44 + 4 * i, (srt ? v : rnd() % 2000) * sc);
	}
}

static u32 near_thr(const u8 *sec)
{
	u32 v = rd32(sec + (rnd() & 1 ? 44 : 8) + 4 * (rnd() % 9));

	switch (rnd() % 10) {
	case 0: return v;
	case 1: return v + 1;
	case 2: return v - 1;
	case 3: return 0;
	case 4: return 0xffffffffu;
	case 5: return rnd() & 0xfff;
	default: return rnd();
	}
}

static void pick_dims(u32 *h, u32 *w)
{
	switch (rnd() % 6) {
	case 0: *h = 1080; *w = 1920; break;
	case 1: *h = 720; *w = 1280; break;
	case 2: *h = rnd() % 64; *w = rnd() % 64; break;
	case 3: *h = 1 + rnd() % 1500; *w = 1 + rnd() % 2200; break;
	default: *h = rnd() % 400; *w = rnd() % 700; break;
	}
}

static void gen_img(struct img *im)
{
	static const u32 b393[] = { 0, 1, 63, 64, 65, 66, 127, 128, 129, 200, 255 };
	static const u32 m191[] = { 0, 0, 1, 1, 2, 0xff };
	static const u32 irqc[] = { 0, 1, 2, 3, 0x3fffffffu, 0x40000000u, 0x40000001u, 0x7fffffffu,
				    254, 255, 0xffffffffu };
	u32 i, h, w;

	memset(im, 0, sizeof *im);
	for (i = 0; i < SECN; i++)
		im->sec[i] = (u8)rnd();
	for (i = 0; i < PARN; i++)
		im->par[i] = (u8)rnd();
	for (i = 0; i < P2N; i++)
		im->p2[i] = (u8)rnd();
	for (i = 0; i < BUFN; i++)
		im->buf[i] = (u8)rnd();
	gen_thr(im->sec);
	im->sec[191] = (u8)pk(m191, 6);
	im->sec[590] = (rnd() % 5 == 0) ? (u8)(rnd() | 1) : 0;
	im->sec[429] = (rnd() % 3 == 0) ? 128 : (u8)rnd();
	im->sec[393] = (u8)pk(b393, 11);
	im->sec[411] = (u8)pk(b393, 11);
	pick_dims(&h, &w);
	im->f[FI_H] = h;
	im->f[FI_W] = w;
	if (rnd() & 1) {
		im->sec[592] = (u8)w;
		im->sec[593] = (u8)(w >> 8);
		im->sec[594] = (u8)h;
		im->sec[595] = (u8)(h >> 8);
	}
	im->f[FI_EV] = near_thr(im->sec);
	im->f[FI_EV2] = near_thr(im->sec);
	if (rnd() % 4 == 0)
		wr32(im->sec + 4, im->f[FI_EV]);
	im->f[FI_NBUF] = 4;
	im->f[FI_PHYS] = rnd();
	im->f[FI_MODE] = rnd() % 3;
	im->f[FI_IRQC] = (rnd() % 4 == 0) ? rnd() : pk(irqc, 11);
	im->f[FI_GAIN] = (rnd() & 1) ? im->sec[429] : (u8)rnd();
	im->f[FI_STR] = rnd();
	im->f[FI_HBUF] = im->f[FI_HPAR] = im->f[FI_HP2] = im->f[FI_HCTX] = 1;
	im->f[FI_VBUF] = 1;
	im->f[FI_VSIZE] = 16 + rnd() % 48;
	for (i = 0; i < 3; i++)
		wr32(im->p2 + 68 + 4 * i, rnd() % 500);
	wr32(im->par + 2176, 0);
	wr32(im->par + 2180, 0);
}

/* make the interpolated bytes at sec+256 equal to what the update will produce */
static void bias_equal(struct img *im)
{
	t32o_reset();
	ncl = 0;
	g_mode = 0;
	bld_o(0, im, 0);
	t32_defog_interp_sec(oc[0].ctx, oc[0].ctx->ev);
	memcpy(im->sec + 256, oc[0].ctx->sec + 256, 5);
}

static void run_filters(unsigned n)
{
	unsigned it, i;

	for (it = 0; it < n; it++) {
		u8 in[180], k[25], o1[180], o2[180];
		u8 *vi, *vo, *vk;
		int f;

		for (i = 0; i < 180; i++)
			in[i] = (it % 4 == 1) ? 0 : (it % 4 == 2) ? 255 : (it % 4 == 3) ? ((rnd() % 9) ? 0 : (u8)rnd()) : (u8)rnd();
		for (i = 0; i < 25; i++)
			k[i] = (it % 5 == 1) ? 0 : (it % 5 == 2) ? ((i == 12) ? 7 : 0) : (it % 5 == 3) ? 255 : (u8)rnd();
		for (f = 0; f < 3; f++) {
			t32o_reset();
			ncl = 0;
			vi = cmem(180);
			vo = cmem(180);
			vk = cmem(25);
			memcpy(vi, in, 180);
			memset(vo, 0x33, 180);
			memcpy(vk, k, 25);
			memset(o2, 0x33, 180);
			if (f == 0) {
				VC(tisp_defog_max_filter, T32O_PTR(vi), T32O_PTR(vo), 0, 0, 0);
				t32_defog_max_filter(in, o2);
			} else if (f == 1) {
				VC(tisp_defog_img_filter9, T32O_PTR(vi), T32O_PTR(vo), T32O_PTR(vk), 0, 0);
				t32_defog_img_filter9(in, o2, k);
			} else {
				VC(tisp_defog_img_filter25, T32O_PTR(vi), T32O_PTR(vo), T32O_PTR(vk), 0, 0);
				t32_defog_img_filter25(in, o2, k);
			}
			memcpy(o1, vo, 180);
			if (!canaries_ok() || memcmp(vi, in, 180) || memcmp(vk, k, 25)) {
				printf("MISMATCH filter %d: vendor changed its input or canary\n", f);
				exit(1);
			}
			for (i = 0; i < 180; i++)
				if (o1[i] != o2[i]) {
					printf("MISMATCH filter %d: out[%u] vendor %02x ours %02x\n", f, i, o1[i], o2[i]);
					exit(1);
				}
			ncases++;
		}
	}
}

static void run_strength(unsigned n)
{
	static const u32 sp[] = { 0, 1, 127, 128, 129, 254, 255 };
	unsigned it, i;

	for (it = 0; it < n; it++) {
		u8 in[9], o2[9];
		u32 s = (it % 3 == 0) ? pk(sp, 7) : (rnd() & 0xff);
		u8 *vi, *vo;

		for (i = 0; i < 9; i++)
			in[i] = (u8)rnd();
		t32o_reset();
		ncl = 0;
		vi = cmem(9);
		vo = cmem(9);
		memcpy(vi, in, 9);
		memset(vo, 0x33, 9);
		memset(o2, 0x33, 9);
		VC(tisp_defog_strength_itp, s, T32O_PTR(vi), T32O_PTR(vo), 0, 0);
		t32_defog_strength_itp(s, in, o2);
		if (!canaries_ok() || memcmp(vo, o2, 9)) {
			printf("MISMATCH strength_itp %u\n", s);
			exit(1);
		}
		ncases++;
	}
	if (tisp_defog_api_set_strength() != t32_defog_api_set_strength() ||
	    tisp_defog_api_get_strength() != t32_defog_api_get_strength()) {
		printf("MISMATCH api_set/get_strength\n");
		exit(1);
	}
}

int main(void)
{
	unsigned n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 1500;
	int fn;
	unsigned it;
	u32 k, i;

	run_filters(n);
	run_strength(n);
	for (fn = 0; fn < NFN; fn++)
		for (it = 0; it < n; it++) {
			u32 ch = rnd() & 3, a[4] = { 0, 0, 0, 0 };
			int mode = (fmodes[fn] & 2) && (rnd() & 1) ? 1 : 0;

			for (k = 0; k < NCH; k++)
				gen_img(&ini[k]);
			for (i = 0; i < 11; i++)
				g_saved[i] = rnd();
			for (k = 0; k < NCH; k++)
				for (i = 0; i < SHW; i++)
					g_sh[k][i] = rnd();
			for (i = 0; i < SECN; i++)
				x_in[i] = (u8)rnd();
			if (rnd() & 1)
				x_in[429] = (u8)ini[ch].f[FI_GAIN];
			if (rnd() % 10 < 3)
				bias_equal(&ini[ch]);
			switch (fn) {
			case F_GETD: a[0] = 2048 * (rnd() & 3); break;
			case F_IRQ:
				a[2] = (rnd() % 10 < 6) ? ini[ch].f[FI_PHYS] + 2048 * (rnd() & 3) : rnd();
				break;
			case F_INIT:
				a[0] = ini[ch].f[FI_W];
				a[1] = ini[ch].f[FI_H];
				break;
			case F_EV:
				if (rnd() % 10 < 3) {
					u32 e = ini[ch].f[FI_EV];

					a[0] = e << 10 | (rnd() & 0x3ff);
					a[1] = e >> 22;
				} else {
					a[0] = rnd();
					a[1] = rnd() & 0x3ff;
				}
				a[2] = (rnd() % 10 < 3) ? ini[ch].f[FI_EV2] : rnd();
				a[3] = (rnd() % 3 == 0) ? rnd() : 0;
				break;
			case F_WDR:
			case F_DN:
				a[0] = rnd();
				break;
			}
			run_one(fn, ch, mode, a);
		}
	printf("t32_defog_oracle: %u cases compared, 0 mismatches\n", ncases);
	return 0;
}
