// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our readable LDC port (driver/t32/tx_isp_t32_ldc.h)
 * against the unmodified vendor functions (Matt Davis' recovered C, PR #15 @
 * 2e2e06bc).  Both sides start from the same random image (contexts inside
 * canary frames, memory slots, channel table, tuning areas, API structs); we
 * compare every register write (address, value, order), the status read, the
 * dma callbacks, every changed byte of all frames and tables, global state
 * words and return values.  Decoy slots/channels catch wrong indexing
 * (channel 0 quirks of mem_cfg, dma_cfg, reg_reflash and init are kept).
 *
 * The oracle defines the LDC data objects and stubs itself as weak symbols
 * (env.c does not provide them); a strong definition in env.c would win.
 * Exit 0 = all equal, 1 = first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_ldc.h"

#define WEAK __attribute__((weak))
#define NSL	4
#define CTXB	3240u
#define MEMB	76u
#define CHB	1808u
#define TUNB	0xed00u
#define CFGB	48u
#define BUFB	1076u
#define FR	64u
#define CAN	0xc5
#define MAXW	2048

_Static_assert(sizeof(struct t32_ldc_ctx) == CTXB, "ctx");
_Static_assert(sizeof(struct t32_ldc_mem) == MEMB, "mem");
_Static_assert(sizeof(struct t32_ldc_chan) == 904, "chan");
_Static_assert(sizeof(struct t32_ldc_param) == BUFB, "param");
_Static_assert(sizeof(struct t32_ldc_cfg) <= CFGB, "cfg");

/* data objects of the reference (weak) */
WEAK u8 t32_data_dc074[NSL * MEMB];
WEAK u32 t32_data_e3614[NSL];
WEAK u32 t32_data_e3a70, t32_data_e3a6c, t32_data_e3610;
/* format/file strings of the (stubbed) isp_printf call in tisp_ldc_init; never dereferenced */
WEAK u8 t32_data_ca6b0[64];
WEAK u8 t32_data_c2af8[64];

struct glog {
	u32 wn, rdn, rdreg, dcn, dsn, kmbad, kmn, kms, kmf, kfn, kfi, kfp;
	u32 w[MAXW][2];
	u32 dc[4][4];
	u32 ds[4];
};
static struct glog G;
static u32 g_rdval;

WEAK u64 system_reg_ldc_write(u32 reg, u32 val, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	if (G.wn < MAXW) {
		G.w[G.wn][0] = reg;
		G.w[G.wn][1] = val;
	}
	G.wn++;
	return 0;
}
WEAK u64 system_reg_ldc_read(u32 reg, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	G.rdn++;
	G.rdreg = reg;
	return g_rdval;
}
WEAK u64 tisp_func_ldc_dma_cfg(u32 a0, u32 a1, u32 a2, u32 a3)
{
	if (G.dcn < 4) {
		G.dc[G.dcn][0] = a0; G.dc[G.dcn][1] = a1;
		G.dc[G.dcn][2] = a2; G.dc[G.dcn][3] = a3;
	}
	G.dcn++;
	return 0;
}
WEAK u64 tisp_func_ldc_dma_stride(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	if (G.dsn < 4)
		G.ds[G.dsn] = a0;
	G.dsn++;
	return 0;
}
WEAK u64 private_kmalloc(u32 size, u32 flags, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	G.kms = size;
	G.kmf = flags;
	G.kmn++;
	return T32O_PTR(t32o_alloc(size));
}
WEAK u64 private_kfree(u32 p, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	G.kfn++;
	G.kfp = p;
	return 0;
}
WEAK u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a1; (void)a2; (void)a3;
	return 0;
}

u64 lib_tisp_ldc_get_mem_size(u32, u32, u32, u32);
u64 tisp_ldc_mem_cfg(u32, u32, u32, u32);
u64 tisp_ldc_reg_cfg(u32, u32, u32, u32);
u64 tisp_ldc_init(u32, u32, u32, u32);
u64 tisp_ldc_deinit(u32, u32, u32, u32);
u64 tisp_ldc_dma_cfg(u32, u32, u32, u32);
u64 tisp_ldc_dma_stride(u32, u32, u32, u32);
u64 tisp_ldc_load_reg_cfg(u32, u32, u32, u32);
u64 tisp_ldc_reg_reflash(u32, u32, u32, u32);
u64 tisp_ldc_reg_all_reflash(u32, u32, u32, u32);
u64 lib_tisp_ldc_intp_handle(u32, u32, u32, u32);
u64 tisp_ldc_param_array_get(u32, u32, u32, u32);
u64 tisp_ldc_param_array_set(u32, u32, u32, u32);
u64 tisp_ldc_wake_up(u32, u32, u32, u32);

#define FN(x) ((t32o_fn)(void (*)(void))(x))

enum { F_MEMSZ, F_MEMCFG, F_REGCFG, F_INIT, F_DEINIT, F_DMACFG, F_DMASTR, F_LOAD,
       F_REFLASH, F_ALLREFLASH, F_INTP, F_GET, F_SET, F_WAKE, F_N };
static const char *const fname[F_N] = {
	"get_mem_size", "mem_cfg", "reg_cfg", "init", "deinit", "dma_cfg", "dma_stride",
	"load_reg_cfg", "reg_reflash", "reg_all_reflash", "intp_handle", "param_array_get",
	"param_array_set", "wake_up"
};

static u32 rs = 0x1dc0a5u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}
static void fill(u8 *p, u32 n)
{
	u32 i;

	for (i = 0; i < n; i++)
		p[i] = (u8)(rnd() >> 11);
}
static void put16(u8 *p, u32 v) { u16 x = (u16)v; memcpy(p, &x, 2); }
static void put32(u8 *p, u32 v) { memcpy(p, &v, 4); }

struct img {
	u8 ctx[NSL][CTXB];
	u8 has[NSL];
	u8 mem[NSL * MEMB];
	u8 chan[CHB];
	u32 loaded, status, irqc, rdval;
	u8 tun[NSL][TUNB];
	u8 cfg[CFGB];
	u8 src[BUFB];
	u8 dst[BUFB];
	u32 len0;
};
static struct img IM;

struct W {
	u8 *ctxf[NSL], *tunf[NSL], *cfgf, *srcf, *dstf;
	u32 *lenf;
};

struct res {
	u8 ctxf[NSL][CTXB + 2 * FR];
	u8 newc[NSL][CTXB];
	u8 slot[NSL];
	u8 mem[NSL * MEMB];
	u8 chan[CHB];
	u32 loaded, status, irqc;
	u8 tunok, cfgok, srcok;
	u8 dstf[BUFB + 2 * FR];
	u32 len3[3];
	u64 ret;
	struct glog g;
};
static struct res RV, RO;

static u8 octx[NSL][CTXB + 2 * FR] __attribute__((aligned(8)));
static u8 otun[NSL][TUNB + 2 * FR] __attribute__((aligned(8)));
static u8 ocfg[CFGB + 2 * FR] __attribute__((aligned(8)));
static u8 osrc[BUFB + 2 * FR] __attribute__((aligned(8)));
static u8 odst[BUFB + 2 * FR] __attribute__((aligned(8)));
static u32 olen[3];
static u8 ournew[NSL][CTXB] __attribute__((aligned(8)));

static int frame_ok(const u8 *f, const u8 *inner, u32 n)
{
	u32 i;

	for (i = 0; i < FR; i++)
		if (f[i] != CAN || f[FR + n + i] != CAN)
			return 0;
	return !memcmp(f + FR, inner, n);
}

static void setup_world(struct W *w, const struct img *im)
{
	unsigned int i;

	for (i = 0; i < NSL; i++) {
		memset(w->ctxf[i], CAN, CTXB + 2 * FR);
		memcpy(w->ctxf[i] + FR, im->ctx[i], CTXB);
		memset(w->tunf[i], CAN, TUNB + 2 * FR);
		memcpy(w->tunf[i] + FR, im->tun[i], TUNB);
	}
	memset(w->cfgf, CAN, CFGB + 2 * FR);
	memcpy(w->cfgf + FR, im->cfg, CFGB);
	memset(w->srcf, CAN, BUFB + 2 * FR);
	memcpy(w->srcf + FR, im->src, BUFB);
	memset(w->dstf, CAN, BUFB + 2 * FR);
	memcpy(w->dstf + FR, im->dst, BUFB);
	w->lenf[0] = 0xc5c5c5c5u;
	w->lenf[1] = im->len0;
	w->lenf[2] = 0xc5c5c5c5u;
}

static void snap_common(struct res *r, const struct W *w, const struct img *im)
{
	unsigned int i;

	r->tunok = 1;
	for (i = 0; i < NSL; i++) {
		memcpy(r->ctxf[i], w->ctxf[i], CTXB + 2 * FR);
		r->tunok &= (u8)frame_ok(w->tunf[i], im->tun[i], TUNB);
	}
	r->cfgok = (u8)frame_ok(w->cfgf, im->cfg, CFGB);
	r->srcok = (u8)frame_ok(w->srcf, im->src, BUFB);
	memcpy(r->dstf, w->dstf, BUFB + 2 * FR);
	memcpy(r->len3, w->lenf, 12);
}

/* ---- vendor side ---- */
static u64 callv(int f, u32 ch, u32 x0, u32 x1, const struct W *w)
{
	u32 cfg = T32O_PTR(w->cfgf + FR);

	switch (f) {
	case F_MEMSZ: return t32o_call(FN(lib_tisp_ldc_get_mem_size), x0, x1, 0, 0, 0);
	case F_MEMCFG: return t32o_call(FN(tisp_ldc_mem_cfg), ch, 0, 0, 0, 0);
	case F_REGCFG: return t32o_call(FN(tisp_ldc_reg_cfg), ch, 0, 0, 0, 0);
	case F_INIT: return t32o_call(FN(tisp_ldc_init), ch, cfg, 0, 0, 0);
	case F_DEINIT: return t32o_call(FN(tisp_ldc_deinit), ch, 0, 0, 0, 0);
	case F_DMACFG: return t32o_call(FN(tisp_ldc_dma_cfg), ch, 0, 0, 0, 0);
	case F_DMASTR: return t32o_call(FN(tisp_ldc_dma_stride), ch, 0, 0, 0, 0);
	case F_LOAD: return t32o_call(FN(tisp_ldc_load_reg_cfg), ch, cfg, 0, 0, 0);
	case F_REFLASH: return t32o_call(FN(tisp_ldc_reg_reflash), ch, 0, 0, 0, 0);
	case F_ALLREFLASH: return t32o_call(FN(tisp_ldc_reg_all_reflash), ch, 0, 0, 0, 0);
	case F_INTP: return t32o_call(FN(lib_tisp_ldc_intp_handle), 0, 0, 0, 0, 0);
	case F_GET: return t32o_call(FN(tisp_ldc_param_array_get), ch,
				     T32O_PTR(w->dstf + FR), T32O_PTR(w->lenf + 1), 0, 0);
	case F_SET: return t32o_call(FN(tisp_ldc_param_array_set), ch,
				     T32O_PTR(w->srcf + FR), 0, 0, 0);
	default: return t32o_call(FN(tisp_ldc_wake_up), ch, 0, 0, 0, 0);
	}
}

static void run_vendor(int f, u32 ch, u32 x0, u32 x1, const struct img *im)
{
	struct W w;
	unsigned int i;
	u64 ret;

	t32o_reset();
	for (i = 0; i < NSL; i++) {
		w.ctxf[i] = t32o_alloc(CTXB + 2 * FR);
		w.tunf[i] = t32o_alloc(TUNB + 2 * FR);
	}
	w.cfgf = t32o_alloc(CFGB + 2 * FR);
	w.srcf = t32o_alloc(BUFB + 2 * FR);
	w.dstf = t32o_alloc(BUFB + 2 * FR);
	w.lenf = t32o_alloc(16);
	setup_world(&w, im);
	for (i = 0; i < NSL; i++) {
		t32_data_e3614[i] = im->has[i] ? T32O_PTR(w.ctxf[i] + FR) : 0;
		t32_data_dd0a4[i] = T32O_PTR(w.tunf[i] + FR);
	}
	memcpy(t32_data_dc074, im->mem, sizeof(t32_data_dc074));
	memcpy(t32_data_dc284, im->chan, CHB);
	t32_data_e3a70 = im->loaded;
	t32_data_e3a6c = im->status;
	t32_data_e3610 = im->irqc;
	g_rdval = im->rdval;
	memset(&G, 0, sizeof(G));
	ret = callv(f, ch, x0, x1, &w);
	if (G.kfn) {
		G.kfi = 99;
		for (i = 0; i < NSL; i++)
			if (G.kfp == T32O_PTR(w.ctxf[i] + FR))
				G.kfi = i;
	}
	G.kfp = 0;

	memset(&RV, 0, sizeof(RV));
	RV.g = G;
	RV.ret = (f == F_MEMSZ) ? ret : (f == F_DEINIT || f == F_WAKE) ? 0 : (u32)ret;
	snap_common(&RV, &w, im);
	for (i = 0; i < NSL; i++) {
		u32 p = t32_data_e3614[i];

		if (!p)
			RV.slot[i] = 0;
		else if (p == T32O_PTR(w.ctxf[i] + FR))
			RV.slot[i] = 1;
		else {
			RV.slot[i] = 2;
			memcpy(RV.newc[i], T32O_HOST(p), CTXB);
		}
	}
	memcpy(RV.mem, t32_data_dc074, sizeof(t32_data_dc074));
	memcpy(RV.chan, t32_data_dc284, CHB);
	RV.loaded = t32_data_e3a70;
	RV.status = t32_data_e3a6c;
	RV.irqc = t32_data_e3610;
}

/* ---- our side ---- */
static struct t32_ldc_dev dev;
static u32 o_newidx;

static void *o_kmalloc(u32 size, u32 flags)
{
	G.kms = size;
	G.kmf = flags;
	G.kmn++;
	return ournew[o_newidx];
}
static void o_kfree(void *p)
{
	unsigned int i;

	G.kfn++;
	G.kfi = 99;
	for (i = 0; i < NSL; i++)
		if ((u8 *)p == octx[i] + FR)
			G.kfi = i;
}
static u32 o_read(u32 reg)
{
	return (u32)system_reg_ldc_read(reg, 0, 0, 0);
}
static void o_dma_cfg(u32 a0, u32 a1, u32 a2, u32 a3)
{
	system_reg_ldc_write(0, 0, 0, 0);	/* placeholder removed below */
	(void)a0; (void)a1; (void)a2; (void)a3;
}
static void o_dma_cfg_real(u32 a0, u32 a1, u32 a2, u32 a3)
{
	tisp_func_ldc_dma_cfg(a0, a1, a2, a3);
}
static void o_dma_stride(u32 w)
{
	tisp_func_ldc_dma_stride(w, 0, 0, 0);
}
static const struct t32_ldc_ops o_ops = { o_kmalloc, o_kfree, o_dma_cfg_real, o_dma_stride, o_read };

static void run_ours(int f, u32 ch, u32 x0, u32 x1, const struct img *im)
{
	struct W w;
	struct t32_front_write ow[MAXW];
	unsigned int n = 0, i;
	u64 ret = 0;
	const struct t32_ldc_cfg *cfg;

	(void)o_dma_cfg;
	for (i = 0; i < NSL; i++) {
		w.ctxf[i] = octx[i];
		w.tunf[i] = otun[i];
	}
	w.cfgf = ocfg; w.srcf = osrc; w.dstf = odst; w.lenf = olen;
	setup_world(&w, im);
	memset(&dev, 0, sizeof(dev));
	for (i = 0; i < NSL; i++) {
		dev.ctx[i] = im->has[i] ? (struct t32_ldc_ctx *)(octx[i] + FR) : NULL;
		dev.tuning[i] = otun[i] + FR;
	}
	memcpy(dev.mem, im->mem, sizeof(dev.mem));
	memcpy(dev.chan, im->chan, CHB);
	dev.loaded = im->loaded;
	dev.last_status = im->status;
	dev.irq_count = im->irqc;
	dev.ops = &o_ops;
	memset(ournew, 0x5a, sizeof(ournew));
	memset(&G, 0, sizeof(G));
	g_rdval = im->rdval;
	cfg = (const struct t32_ldc_cfg *)(ocfg + FR);
	o_newidx = ch & 0xff;

	switch (f) {
	case F_MEMSZ: ret = t32_ldc_get_mem_size(&dev, x0, x1); break;
	case F_MEMCFG: t32_ldc_mem_cfg(&dev, ch, ow, &n); break;
	case F_REGCFG: t32_ldc_reg_cfg(&dev, ch, ow, &n); break;
	case F_INIT: ret = (u32)t32_ldc_init(&dev, ch, cfg); break;
	case F_DEINIT:
		t32_ldc_deinit(&dev, ch, ow, &n);
		break;
	case F_DMACFG: ret = (u32)t32_ldc_dma_cfg(&dev, ch); break;
	case F_DMASTR: ret = (u32)t32_ldc_dma_stride(&dev, ch); break;
	case F_LOAD: ret = (u32)t32_ldc_load_reg_cfg(&dev, ch, cfg, ow, &n); break;
	case F_REFLASH: ret = (u32)t32_ldc_reg_reflash(&dev, ch, ow, &n); break;
	case F_ALLREFLASH: ret = (u32)t32_ldc_reg_all_reflash(&dev, ch, ow, &n); break;
	case F_INTP:
		ret = t32_ldc_intp_handle(&dev, ow, &n);
		break;
	case F_GET: ret = (u32)t32_ldc_param_array_get(&dev, ch, odst + FR, olen + 1); break;
	case F_SET: ret = (u32)t32_ldc_param_array_set(&dev, ch, osrc + FR, ow, &n); break;
	default: t32_ldc_wake_up(&dev, ch, ow, &n); break;
	}
	G.wn = n;
	for (i = 0; i < n && i < MAXW; i++) {
		G.w[i][0] = ow[i].reg;
		G.w[i][1] = ow[i].val;
	}

	memset(&RO, 0, sizeof(RO));
	RO.g = G;
	RO.ret = ret;
	snap_common(&RO, &w, im);
	for (i = 0; i < NSL; i++) {
		if (!dev.ctx[i])
			RO.slot[i] = 0;
		else if ((u8 *)dev.ctx[i] == octx[i] + FR)
			RO.slot[i] = 1;
		else {
			RO.slot[i] = 2;
			memcpy(RO.newc[i], dev.ctx[i], CTXB);
		}
	}
	memcpy(RO.mem, dev.mem, sizeof(dev.mem));
	memcpy(RO.chan, dev.chan, CHB);
	RO.loaded = dev.loaded;
	RO.status = dev.last_status;
	RO.irqc = dev.irq_count;
}

static const char *diff(void)
{
	const struct res *a = &RV, *b = &RO;
	unsigned int i, wn;

	if (a->ret != b->ret) return "return value";
	if (a->g.wn != b->g.wn) return "register write count";
	wn = a->g.wn < MAXW ? a->g.wn : MAXW;
	for (i = 0; i < wn; i++)
		if (a->g.w[i][0] != b->g.w[i][0] || a->g.w[i][1] != b->g.w[i][1]) {
			printf("  write #%u vendor %05x=%08x ours %05x=%08x\n", i, a->g.w[i][0],
			       a->g.w[i][1], b->g.w[i][0], b->g.w[i][1]);
			return "register write";
		}
	if (a->g.rdn != b->g.rdn || a->g.rdreg != b->g.rdreg) return "register read";
	if (a->g.dcn != b->g.dcn || memcmp(a->g.dc, b->g.dc, sizeof(a->g.dc))) return "dma_cfg call";
	if (a->g.dsn != b->g.dsn || memcmp(a->g.ds, b->g.ds, sizeof(a->g.ds))) return "dma_stride call";
	if (a->g.kms != b->g.kms || a->g.kmf != b->g.kmf) {
		printf("  kmalloc vendor size %u flags %x ours size %u flags %x\n", a->g.kms,
		       a->g.kmf, b->g.kms, b->g.kmf);
		return "kmalloc args";
	}
	if (a->g.kmn != b->g.kmn) return "kmalloc count";
	if (a->g.kfn != b->g.kfn || a->g.kfi != b->g.kfi) return "kfree";
	if (memcmp(a->slot, b->slot, sizeof(a->slot))) return "ctx slot";
	if (memcmp(a->ctxf, b->ctxf, sizeof(a->ctxf))) return "ctx frames";
	if (memcmp(a->newc, b->newc, sizeof(a->newc))) return "new ctx";
	if (memcmp(a->mem, b->mem, sizeof(a->mem))) return "mem table";
	if (memcmp(a->chan, b->chan, sizeof(a->chan))) return "channel table";
	if (a->loaded != b->loaded) return "loaded flag";
	if (a->status != b->status) return "last status";
	if (a->irqc != b->irqc) return "irq count";
	if (a->tunok != b->tunok || a->cfgok != b->cfgok || a->srcok != b->srcok) return "input frames";
	if (memcmp(a->dstf, b->dstf, sizeof(a->dstf))) return "dst frame";
	if (memcmp(a->len3, b->len3, sizeof(a->len3))) return "len frame";
	return NULL;
}

static u32 interesting(u32 ref)
{
	switch (rnd() % 10) {
	case 0: return 0;
	case 1: return ref;
	case 2: return ref + 1;
	case 3: return ref - 1;
	case 4: return 0xffff;
	case 5: return 0x10000;
	case 6: return 0x10001;
	case 7: return rnd() & 0xffff;
	case 8: return rnd();
	default: return 640 + (rnd() & 1) * 1280;
	}
}
static u32 pickst(void)
{
	switch (rnd() % 8) {
	case 0: return 0;
	case 1: return 1;
	case 2: return 2;
	case 3: return 3;
	case 4: return rnd();
	case 5: return 0x7fffffffu;
	case 6: return 0x80000000u;
	default: return 0xffffffffu;
	}
}
static u32 pickmode(void)
{
	switch (rnd() % 5) {
	case 0: case 1: return 1;
	case 2: return 0;
	case 3: return 2;
	default: return rnd() & 0xffff;
	}
}

static void gen_img(struct img *im, int f, u32 *ch, u32 *x0, u32 *x1)
{
	unsigned int i;
	u32 c, w0, k;

	memset(im, 0, sizeof(*im));
	for (i = 0; i < NSL; i++) {
		fill(im->ctx[i], CTXB);
		put16(im->ctx[i] + 3228, pickmode());
		put16(im->ctx[i] + 3230, (rnd() % 5 < 2) ? 1 : (rnd() % 3));
		put16(im->ctx[i] + 3236, interesting(640));
		put16(im->ctx[i] + 3238, interesting(480));
		im->has[i] = 1;
	}
	fill(im->mem, sizeof(im->mem));
	fill(im->chan, CHB);
	for (k = 0; k < T32_LDC_CHANS; k++) {
		u32 e = rnd() % 5;

		im->chan[k * 904 + 40] = e == 0 ? 0 : e == 1 ? 2 : e == 2 ? (u8)rnd() : 1;
		put32(im->chan + k * 904 + 900, pickst());
	}
	im->loaded = pickst();
	im->status = rnd();
	im->irqc = pickst();
	im->rdval = rnd();
	fill(im->cfg, CFGB);
	fill(im->src, BUFB);
	fill(im->dst, BUFB);
	im->len0 = rnd();

	switch (f) {
	case F_MEMCFG: case F_REGCFG: case F_WAKE: c = rnd() % 4; break;
	case F_INIT: c = rnd() % 4; break;
	case F_GET: case F_SET: c = rnd() % 2; break;
	case F_MEMSZ: case F_INTP: c = 0; break;
	default: c = rnd() % 2; break;
	}
	if ((f == F_INIT || f == F_DEINIT || f == F_DMACFG || f == F_DMASTR || f == F_LOAD ||
	     f == F_REFLASH || f == F_ALLREFLASH) && (rnd() & 1))
		c |= (rnd() & 0xff) << 8;
	*ch = c;

	if (f == F_INIT) {
		im->has[c & 0xff] = (rnd() & 1);
		im->has[0] = (c & 0xff) ? 1 : im->has[0];
	}
	if (f == F_DEINIT)
		im->has[c & 0xff] = (rnd() % 4) != 0;

	w0 = 0;
	memcpy(&w0, im->ctx[0] + 3236, 2);
	{
		u32 h0 = 0, ww, hh;

		memcpy(&h0, im->ctx[0] + 3238, 2);
		ww = interesting(w0);
		hh = interesting(h0);
		put32(im->cfg, ww);
		put32(im->cfg + 4, hh);
		im->cfg[40] = (rnd() % 5) ? 1 : (rnd() & 1) ? 0 : (u8)rnd();
	}
	for (i = 0; i < NSL; i++)
		memset(im->tun[i], 0x11 * (i + 1), TUNB);
	if (f == F_LOAD)
		fill(im->tun[c & 0xff], TUNB);

	*x0 = (rnd() & 1) ? (rnd() & 0xfff) : rnd();
	*x1 = (rnd() & 1) ? (rnd() & 0xfff) : rnd();
}

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 3000;
	unsigned int f, c, compared = 0;

	for (f = 0; f < F_N; f++)
		for (c = 0; c < n; c++) {
			u32 ch, x0, x1;
			const char *d;

			gen_img(&IM, (int)f, &ch, &x0, &x1);
			run_vendor((int)f, ch, x0, x1, &IM);
			run_ours((int)f, ch, x0, x1, &IM);
			compared++;
			d = diff();
			if (d) {
				printf("MISMATCH %s case %u (ch %x): %s (writes vendor %u ours %u)\n",
				       fname[f], c, ch, d, RV.g.wn, RO.g.wn);
				return 1;
			}
		}
	printf("t32_ldc_oracle: %u cases compared (14 functions), 0 mismatches\n", compared);
	return 0;
}
