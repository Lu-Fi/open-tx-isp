// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: MDNS control / memory / parameter paths.  Runs the
 * unmodified vendor functions (Matt Davis' recovered C, PR #15 @ 2e2e06bc)
 * tisp_mdns_{buf_clc,wdr_en,get_mem_size,set_mem_cfg,malloc_cfg,deinit,
 * dn_params_api,dn_params_api2,param_array_get,param_array_set,addr_alloc,
 * addr_free,sleep} and our readable t32_mdns_* of driver/t32/tx_isp_t32_mdns.h
 * from the same initial state and compares every register write (address,
 * value, order), the return values and every context / memory field the
 * reference changes.  Fixed seed; T32O_NRAND overrides the number of rounds
 * (each round runs every function once).
 *
 * Data objects and kernel stubs that env.c may not provide are defined here as
 * WEAK symbols (env.c wins when it has them).  The checks that need to steer
 * a stub (addr_alloc/free, printk, kfree log) run only when the probes show
 * that our weak stub is the one linked; otherwise they print SKIP.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_mdns.h"

#define WK		__attribute__((weak))
#define CTXSZ		((u32)sizeof(struct t32_mdns_ctx))
typedef u32 __attribute__((may_alias)) au32;
#define W32(p, off)	(*(au32 *)((u8 *)(p) + (off)))
#define REF(fn, a, b, c, d, e) t32o_call((t32o_fn)(void (*)(void))(fn), (a), (b), (c), (d), (e))
#define UNUSED4		(void)a0; (void)a1; (void)a2; (void)a3

/* vendor data objects (weak: env.c may define them) */
WK u32 t32_data_e22c8[4] = {0};
WK u32 t32_data_e22b8[4] = {0};
/* markers: tell at start-up whether these weak fallbacks are the linked objects */
WK u8 t32_data_dc074[4 * 76] = { [150] = 0x5a, [151] = 0xa5 };
WK u8 t32_data_f0900[4] = {0};
WK u8 t32_data_e4100[12800 * 2 + 64] = { [12616] = 0xa5, [12617] = 0x5a, [12618] = 0xc3, [12619] = 0x3c };
static int own_e4100, own_dc074;
static u32 dcm = 1;	/* channel mask for the dc074 records */
WK u32 t32_data_e227c[15] = {0};
WK u32 t32_data_e21d4[6] = {0};
WK u32 t32_data_e226c[4] = {0};
WK u32 t32_data_e2228[17] = {0};
WK u32 t32_data_e21ec[15] = {0};
/* message strings / file names the refs pass to printk and isp_printf (address only) */
WK u8 t32_data_c9d94[64] = {0};
WK u8 t32_data_c9dd0[64] = {0};
WK u8 t32_data_bfb20[64] = {0};
WK u8 t32_data_bfb34[64] = {0};
WK u8 t32_data_c9d14[64] = {0};
WK u8 t32_data_c9d54[64] = {0};
WK u8 t32_data_bfb5c[64] = {0};
WK u8 t32_data_c9c80[64] = {0};
WK u8 t32_data_c9c98[64] = {0};
WK u8 t32_data_bfb74[64] = {0};
WK u8 t32_data_c9bdc[64] = {0};
WK u8 t32_data_c9c28[64] = {0};
WK u8 t32_data_bfb8c[64] = {0};
WK u8 t32_data_c9b9c[64] = {0};
WK u8 t32_data_c9bd8[64] = {0};

/* steering variables of the weak stubs */
static u32 xo_sysreg, xo_ctrl, xo_dn, xo_ctrl_set, xo_ctrl_sets, xo_lock_n, xo_unlock_n;
static u32 xo_pk_n, xo_pk_a2, xo_cbuf, xo_salt;
struct frec { u32 kind, ptr; };
static struct frec xo_fr[64], ours_fr[64], ref_fr[64];
static unsigned int xo_fr_n, ours_fr_n, ref_fr_n;

WK u64 private_mutex_lock(u32 a0, u32 a1, u32 a2, u32 a3) { UNUSED4; xo_lock_n++; return 0; }
WK u64 private_mutex_unlock(u32 a0, u32 a1, u32 a2, u32 a3) { UNUSED4; xo_unlock_n++; return 0; }
WK u64 private_kmalloc(u32 a0, u32 a1, u32 a2, u32 a3) { UNUSED4; return T32O_PTR(t32o_alloc(a0)); }
WK u64 private_kfree(u32 a0, u32 a1, u32 a2, u32 a3)
{
	UNUSED4;
	if (xo_fr_n < 64) {
		xo_fr[xo_fr_n].kind = 1;
		xo_fr[xo_fr_n].ptr = a0;
		xo_fr_n++;
	}
	return 0;
}
WK u64 private_vfree(u32 a0, u32 a1, u32 a2, u32 a3)
{
	UNUSED4;
	if (xo_fr_n < 64) {
		xo_fr[xo_fr_n].kind = 2;
		xo_fr[xo_fr_n].ptr = a0;
		xo_fr_n++;
	}
	return 0;
}
WK u64 printk(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a3; xo_pk_n++; xo_pk_a2 = a2; return 0; }
WK u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3) { UNUSED4; return 0; }
WK u64 system_reg_read(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return xo_sysreg; }
WK u64 tisp_g_module_control(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a2; (void)a3;
	*(au32 *)T32O_HOST(a1) = xo_ctrl;
	return 0;
}
WK u64 tisp_s_module_control(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a2; (void)a3;
	xo_ctrl_set = a1;
	xo_ctrl_sets++;
	return 0;
}
WK u64 tisp_day_or_night_g_ctrl(u32 a0, u32 a1, u32 a2, u32 a3) { UNUSED4; return xo_dn; }
WK u64 tisp_reg_read(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a2; (void)a3;
	return t32o_rd_hook ? t32o_rd_hook(a1) : 0;
}

/* unmodified vendor functions from ref/ */
extern u64 tisp_mdns_buf_clc(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_mdns_wdr_en(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_mdns_get_mem_size(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4);
extern u64 tisp_mdns_set_mem_cfg(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_mdns_malloc_cfg(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_mdns_deinit(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_mdns_dn_params_api(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_mdns_dn_params_api2(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_mdns_param_array_get(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_mdns_param_array_set(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_mdns_addr_alloc(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_mdns_addr_free(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_mdns_sleep(u32 a0, u32 a1, u32 a2, u32 a3);

static u32 rs = 0x6d646e78u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static unsigned int compared, cs, refused;
static int act_pk, act_fr, act_addr;
static struct t32_front_write rw[T32O_MAX_WRITES];
static unsigned int rn;
static struct t32_front_write ow[T32_MDNS_MAX_WRITES];

static void die(const char *what)
{
	printf("MISMATCH %s (case %u), vendor writes %u\n", what, cs, rn);
	printf("t32_mdnsx_oracle: %u compared, 1 mismatches\n", compared);
	exit(1);
}
#define CHECK(cond, what) do { compared++; if (!(cond)) die(what); } while (0)

static void snap(void)
{
	rn = t32o_log.n;
	memcpy(rw, t32o_log.w, rn * sizeof(rw[0]));
}

static int same_writes(int on)
{
	unsigned int i, shown = 0;

	if (on < 0 || (unsigned int)on != rn) {
		printf("  write count vendor %u ours %d\n", rn, on);
		for (i = 0; i < rn && i < 12; i++)
			printf("  vendor #%u %05x=%08x\n", i, rw[i].reg, rw[i].val);
		for (i = 0; on > 0 && i < (unsigned int)on && i < 12; i++)
			printf("  ours   #%u %05x=%08x\n", i, ow[i].reg, ow[i].val);
		return 0;
	}
	for (i = 0; i < rn; i++)
		if (rw[i].reg != ow[i].reg || rw[i].val != ow[i].val) {
			if (shown++ < 8)
				printf("  #%u vendor %05x=%08x ours %05x=%08x\n", i, rw[i].reg, rw[i].val,
				       ow[i].reg, ow[i].val);
		}
	return shown == 0;
}

static void fill(void *p, unsigned int n)
{
	u8 *q = p;

	while (n--)
		*q++ = (u8)rnd();
}

static u16 pick_memopt(void)
{
	switch (rnd() % 8) {
	case 0: return 0;
	case 1: return 1000;
	case 2: return 1001;
	case 3: return 2000;
	case 4: return 2001;
	case 5: return (u16)(rnd() % 3000);
	case 6: return (u16)rnd();
	default: return (u16)(1100 + 100 * (rnd() % 10));
	}
}

/* ---- buf_clc, wdr_en ---- */
static void t_wdr(void)
{
	u32 ch = rnd() & 3, val = rnd();
	u8 init[sizeof(struct t32_mdns_ctx)], ours[sizeof(struct t32_mdns_ctx)];
	u8 *ctx;
	u64 r;
	int ret;

	fill(init, sizeof(init));
	t32o_reset();
	ctx = t32o_alloc(CTXSZ);
	memcpy(ctx, init, CTXSZ);
	t32_data_e22c8[ch] = T32O_PTR(ctx);
	r = REF(tisp_mdns_wdr_en, ch, val, 0, 0, 0);
	memcpy(ours, init, CTXSZ);
	ret = t32_mdns_wdr_en((u32 *)(void *)(ours + 16), val);
	CHECK((u32)r == 0 && ret == 0 && !memcmp(ctx, ours, CTXSZ) && t32o_log.n == 0, "wdr_en");

	t32o_reset();
	r = REF(tisp_mdns_buf_clc, rnd(), rnd(), rnd(), rnd(), 0);
	CHECK((u32)r == 0 && t32o_log.n == 0 && t32_mdns_buf_clc() == 0, "buf_clc");
}

/* ---- get_mem_size ---- */
static void t_getmem(void)
{
	u32 ch = rnd() & 1 & dcm, w = rnd(), h = rnd(), sel = rnd(), hi = rnd();
	u16 mo = pick_memopt();
	u8 rec[76];
	struct t32_mdns_mem m;
	u32 rr, ro;

	if (rnd() & 1) { w &= 0xfff; h &= 0xfff; }
	if ((rnd() & 3) == 0)
		sel = 1;
	fill(rec, 76);
	memcpy(rec + 8, &mo, 2);
	t32o_reset();
	memcpy(t32_data_dc074 + ch * 76, rec, 76);
	memcpy(&m, rec, 76);
	rr = (u32)REF(tisp_mdns_get_mem_size, ch, w, h, sel, hi);
	ro = t32_mdns_get_mem_size(&m, w, h, sel, hi);
	CHECK(rr == ro, "get_mem_size return");
	CHECK(!memcmp(t32_data_dc074 + ch * 76, &m, 76), "get_mem_size record");
}

/* ---- set_mem_cfg ---- */
static void put_cmp(u8 *ctx, u32 ch, u32 mo);
static void put_dims(u8 *ctx, u32 ch, u32 w, u32 h);

static void t_setmem(void)
{
	u32 ch = rnd() & 1 & dcm, w = 1 + rnd() % 4096, h = 1 + rnd() % 2200, base = rnd();
	u8 hold = (u8)(rnd() % 3), flag = (u8)(rnd() % 3);
	u16 mo = pick_memopt();
	u8 rec0[76], secb[0x200], *ctx, *sec;
	struct t32_mdns_mem m, d;
	struct t32_mdns_memchk chk, dc;
	u32 expb, expe, pk;
	int on;

	if ((rnd() & 3) == 0) { w = 1 + rnd() % 64; h = 1 + rnd() % 64; }
	fill(rec0, 76);
	memcpy(rec0 + 8, &mo, 2);
	rec0[11] = flag;
	fill(secb, sizeof(secb));
	memcpy(&m, rec0, 76);
	d = m;
	dc.exp_base = base;
	dc.exp_end = 0;
	t32_mdns_set_mem_cfg(&d, secb, w, h, base, 1, &dc, ow);
	expe = dc.end;
	expb = (rnd() % 4) ? base : rnd();
	if (!(rnd() % 4))
		expe = (rnd() & 1) ? expe + 1 : rnd();

	t32o_reset();
	memcpy(t32_data_dc074 + ch * 76, rec0, 76);
	W32(t32_data_dc284 + ch * 904, 0) = w;
	W32(t32_data_dc284 + ch * 904, 4) = h;
	if (!own_e4100 && hold == 1)
		hold = 0;	/* the vendor reads e4100 only with hold == 1 */
	t32_data_f0900[ch] = hold;
	if (own_e4100) {
		W32(t32_data_e4100 + ch * 12800, 12616) = expb;
		W32(t32_data_e4100 + ch * 12800, 12620) = expe;
	}
	ctx = t32o_alloc(CTXSZ);
	memset(ctx, 0, CTXSZ);
	sec = t32o_alloc(0x200);
	memcpy(sec, secb, 0x200);
	W32(ctx, 0) = T32O_PTR(sec);
	t32_data_e22c8[ch] = T32O_PTR(ctx);
	/* func_en (flag11 == 1) reads the compression setting where discover() found it */
	put_cmp(ctx, ch, mo);
	/* the vendor context carries the same frame size (func_en reads it) */
	put_dims(ctx, ch, w, h);
	W32(t32_data_dc284 + ch * 904, 0) = w;
	W32(t32_data_dc284 + ch * 904, 4) = h;
	memcpy(&m, t32_data_dc074 + ch * 76, 76);
	xo_pk_n = 0;
	REF(tisp_mdns_set_mem_cfg, ch, base, 0, 0, 0);
	snap();
	pk = xo_pk_n ? xo_pk_a2 : 0;
	chk.exp_base = expb;
	chk.exp_end = expe;
	on = t32_mdns_set_mem_cfg(&m, secb, w, h, base, hold, &chk, ow);
	if (on < 0) {
		refused++;
		return;
	}
	if (!same_writes(on))
		printf("  set_mem_cfg case: hold %u flag11 %u memopt %u w %u h %u own_e4100 %d own_dc074 %d\n",
		       hold, flag, mo, w, h, own_e4100, own_dc074);
	CHECK(same_writes(on), "set_mem_cfg writes");
	CHECK(!memcmp(t32_data_dc074 + ch * 76, &m, 76), "set_mem_cfg record");
	if (act_pk)
		CHECK(pk == (chk.warn == 1 ? 6649U : chk.warn == 2 ? 6652U : 0U), "set_mem_cfg warning");
}

/* ---- malloc_cfg ---- */
static void *x_alloc(u32 size)
{
	return T32O_HOST((u32)private_kmalloc(size, 0x2400c0, 0, 0));
}

static void t_malloc(void)
{
	u32 ch = rnd() & 3, w = 1 + rnd() % 256, h = 1 + rnd() % 128, c48 = rnd() % 2048;
	u8 *ctx;
	u32 desc, a0, a3, a4;
	struct t32_mdns_bufs *b = NULL;
	int on;

	if ((rnd() & 7) == 0) { w = 1 + rnd() % 16; h = 1 + rnd() % 16; }
	t32o_reset();
	ctx = t32o_alloc(CTXSZ);
	memset(ctx, 0, CTXSZ);
	W32(ctx, 48) = c48;
	t32_data_e22c8[ch] = T32O_PTR(ctx);
	t32_data_e22b8[ch] = 0;
	REF(tisp_mdns_malloc_cfg, ch, w, h, 0, 0);
	snap();
	desc = t32_data_e22b8[ch];
	a0 = W32(T32O_HOST(desc), 0);
	a3 = W32(T32O_HOST(desc), 12);
	a4 = W32(T32O_HOST(desc), 16);

	t32o_reset();
	ctx = t32o_alloc(CTXSZ);
	memset(ctx, 0, CTXSZ);
	on = t32_mdns_malloc_cfg(&b, c48, w, h, x_alloc, ow);
	CHECK(same_writes(on), "malloc_cfg writes");
	CHECK(b && T32O_PTR(b) == desc, "malloc_cfg descriptor");
	CHECK(b && b->addr[0] == a0 && b->addr[3] == a3 && b->addr[4] == a4, "malloc_cfg buffers");
}

/* ---- deinit ---- */
static void o_kfree(void *p)
{
	if (ours_fr_n < 64) {
		ours_fr[ours_fr_n].kind = 1;
		ours_fr[ours_fr_n].ptr = T32O_PTR(p);
		ours_fr_n++;
	}
}

static void o_vfree(void *p)
{
	if (ours_fr_n < 64) {
		ours_fr[ours_fr_n].kind = 2;
		ours_fr[ours_fr_n].ptr = T32O_PTR(p);
		ours_fr_n++;
	}
}

static void t_deinit(void)
{
	u32 ch = rnd() & 3, mode = rnd(), pass, k, s0 = 1, s1 = 1;

	for (pass = 0; pass < 2; pass++) {
		u8 *ctx, *val, *desc, *bufp[5];

		t32o_reset();
		ctx = t32o_alloc(CTXSZ);
		val = t32o_alloc(16);
		desc = t32o_alloc(20);
		for (k = 0; k < 5; k++)
			bufp[k] = t32o_alloc(16);
		memset(ctx, 0, CTXSZ);
		for (k = 0; k < 5; k++)
			W32(desc, 4 * k) = (mode & (8U << k)) ? 0 : T32O_PTR(bufp[k]);
		if (!pass) {
			W32(ctx, 4) = (mode & 2) ? 0 : T32O_PTR(val);
			t32_data_e22c8[ch] = (mode & 1) ? 0 : T32O_PTR(ctx);
			t32_data_e22b8[ch] = (mode & 4) ? 0 : T32O_PTR(desc);
			xo_fr_n = 0;
			REF(tisp_mdns_deinit, ch, 0, 0, 0, 0);
			s0 = t32_data_e22c8[ch];
			s1 = t32_data_e22b8[ch];
			ref_fr_n = xo_fr_n;
			memcpy(ref_fr, xo_fr, sizeof(ref_fr));
		} else {
			struct t32_mdns_ctx *c = (struct t32_mdns_ctx *)(void *)ctx;
			struct t32_mdns_ctx *cslot;
			struct t32_mdns_bufs *bslot;

			memset(c, 0, sizeof(*c));
			c->val = (mode & 2) ? NULL : val;
			cslot = (mode & 1) ? NULL : c;
			bslot = (mode & 4) ? NULL : (struct t32_mdns_bufs *)(void *)desc;
			ours_fr_n = 0;
			t32_mdns_deinit(&cslot, &bslot, o_vfree, o_kfree);
			CHECK(s0 == 0 && s1 == 0 && cslot == NULL && bslot == NULL, "deinit slots");
			if (act_fr) {
				CHECK(ours_fr_n == ref_fr_n, "deinit free count");
				for (k = 0; k < ours_fr_n; k++)
					CHECK(ours_fr[k].kind == ref_fr[k].kind && ours_fr[k].ptr == ref_fr[k].ptr, "deinit free order");
			}
		}
	}
}

/* ---- dn_params_api / api2 ---- */
static void t_dnapi(void)
{
	u32 ch = rnd() & 3, two = rnd() & 1;
	u8 *ctx, *sec, *val;
	int on;

	t32o_reset();
	ctx = t32o_alloc(CTXSZ);
	sec = t32o_alloc(900);
	val = t32o_alloc(200);
	fill(sec, 900);
	fill(val, 200);
	memset(ctx, 0, CTXSZ);
	W32(ctx, 0) = T32O_PTR(sec);
	W32(ctx, 4) = T32O_PTR(val);
	t32_data_e22c8[ch] = T32O_PTR(ctx);
	if (two) {
		REF(tisp_mdns_dn_params_api2, ch, 0, 0, 0, 0);
		snap();
		on = t32_mdns_dn_params_api2(sec, val, ow);
	} else {
		REF(tisp_mdns_dn_params_api, ch, 0, 0, 0, 0);
		snap();
		on = t32_mdns_dn_params_api(sec, val, ow);
	}
	CHECK(same_writes(on) && on == 1, two ? "dn_params_api2" : "dn_params_api");
}

/* ---- param_array_get ---- */
static void t_get(void)
{
	static u8 secb[T32_MDNS_SEC_BYTES], dinit[T32_MDNS_PART1_BYTES + 16], oursd[T32_MDNS_PART1_BYTES + 16];
	static const u32 parts[5] = { 0, 1, 2, 3, 0xffffffffU };
	u32 ch = rnd() & 3, part = parts[rnd() % 5], len0 = rnd(), olen;
	u8 *ctx, *sec, *dst, *lenp;
	u64 r;
	int ret;

	fill(secb, sizeof(secb));
	fill(dinit, sizeof(dinit));
	t32o_reset();
	ctx = t32o_alloc(CTXSZ);
	sec = t32o_alloc(T32_MDNS_SEC_BYTES);
	dst = t32o_alloc(sizeof(dinit));
	lenp = t32o_alloc(4);
	memset(ctx, 0, CTXSZ);
	memcpy(sec, secb, sizeof(secb));
	memcpy(dst, dinit, sizeof(dinit));
	W32(lenp, 0) = len0;
	W32(ctx, 0) = T32O_PTR(sec);
	t32_data_e22c8[ch] = T32O_PTR(ctx);
	r = REF(tisp_mdns_param_array_get, ch, part, T32O_PTR(dst), T32O_PTR(lenp), 0);
	memcpy(oursd, dinit, sizeof(dinit));
	olen = len0;
	ret = t32_mdns_param_array_get(secb, part, oursd, &olen);
	CHECK((u32)r == 0 && ret == 0 && !memcmp(dst, oursd, sizeof(dinit)) && W32(lenp, 0) == olen, "param_array_get");
}

/* ---- where the vendor keeps the frame size read by reg_cfg (found at start-up) ---- */
static int dim_reg[2] = { -1, -1 };	/* [0] width, [1] height: 0 = ctx, 1 = t32_data_dc284[ch] */
static u32 dim_off[2];
static int cmp_reg = -1;		/* where func_en finds the compression setting: 0 = ctx, 1 = dc074[ch] */
static u32 cmp_off, cmp_isflag, disc_d024;

static void put_dim(u8 *ctx, u32 ch, int which, u32 v)
{
	if (dim_reg[which] == 0)
		W32(ctx, dim_off[which]) = v;
	else
		W32(t32_data_dc284 + ch * 904, dim_off[which]) = v;
}

static void put_dims(u8 *ctx, u32 ch, u32 w, u32 h)
{
	put_dim(ctx, ch, 0, w);
	put_dim(ctx, ch, 1, h);
}

static void put_cmp(u8 *ctx, u32 ch, u32 mo)
{
	u32 v = cmp_isflag ? (mo > 1000) : mo;

	if (cmp_reg == 0) {
		W32(ctx, cmp_off) = v;
	} else if (cmp_isflag) {
		t32_data_dc074[ch * 76 + cmp_off] = (u8)v;
	} else {
		u16 s = (u16)v;

		memcpy(t32_data_dc074 + ch * 76 + cmp_off, &s, 2);
	}
}

static void disc_run(int region, u32 off, u32 v, u32 seed, u32 *d100, u32 *d108)
{
	static u8 secb[T32_MDNS_SEC_BYTES], inb[T32_MDNS_PART1_BYTES];
	u8 *ctx, *sec, *val, *in;
	u32 x = seed, i, k;

	for (i = 0; i < sizeof(secb); i++) {
		x = x * 1664525u + 1013904223u;
		secb[i] = (u8)(x >> 24);
	}
	t32o_reset();
	memset(t32_data_dc284, 0, 904);
	memset(t32_data_dc074, 0, 76);
	t32_data_f0900[0] = 0;
	ctx = t32o_alloc(CTXSZ);
	sec = t32o_alloc(T32_MDNS_SEC_BYTES);
	val = t32o_alloc(T32_MDNS_VALUES + 16);
	in = t32o_alloc(T32_MDNS_PART1_BYTES);
	memset(ctx, 0, CTXSZ);
	memcpy(sec, secb, sizeof(secb));
	memset(val, 0, T32_MDNS_VALUES + 16);
	memcpy(in, inb, sizeof(inb));
	W32(ctx, 0) = T32O_PTR(sec);
	W32(ctx, 4) = T32O_PTR(val);
	if (region == 0)
		W32(ctx, off) = v;
	else if (region == 1)
		W32(t32_data_dc284, off) = v;
	else if (region == 2)
		memcpy(t32_data_dc074 + off, &v, 4);
	t32_data_e22c8[0] = T32O_PTR(ctx);
	xo_dn = 0;
	REF(tisp_mdns_param_array_set, 0, 2, T32O_PTR(in), 0, 0);
	*d100 = *d108 = 0;
	for (k = 0; k < t32o_log.n; k++) {
		if (t32o_log.w[k].reg == 0xd100)
			*d100 = t32o_log.w[k].val;
		if (t32o_log.w[k].reg == 0xd108)
			*d108 = t32o_log.w[k].val;
		if (t32o_log.w[k].reg == 0xd024)
			disc_d024 = t32o_log.w[k].val;
	}
}

static void discover(void)
{
	u32 seed, b100 = 0, b108 = 0, d100, d108, off, lim;
	int reg;

	for (seed = 1; seed < 40; seed++) {
		disc_run(-1, 0, 0, seed, &b100, &b108);
		if (b100 >= 1 && b100 < 200)
			break;
	}
	if (b100 < 1 || b100 >= 200) {
		printf("t32_mdnsx_oracle: discover: no usable tuning seed\n");
		exit(1);
	}
	for (reg = 0; reg < 2; reg++) {
		lim = reg ? 904u : (CTXSZ < 256 ? CTXSZ : 256u);
		for (off = reg ? 0 : 12; off + 4 <= lim; off += 4) {
			u32 dd;

			disc_run(reg, off, 1000, seed, &d100, &d108);
			if (d100 != b100)
				continue;
			dd = d108 ^ b108;
			if ((dd & 0xffff) && !(dd >> 16) && dim_reg[0] < 0) {
				dim_reg[0] = reg;
				dim_off[0] = off;
			}
			if (!(dd & 0xffff) && (dd >> 16) && dim_reg[1] < 0) {
				dim_reg[1] = reg;
				dim_off[1] = off;
			}
		}
	}
	{
		static const u32 sent[2] = { 1100, 1 };
		u32 b24, si, lim2 = CTXSZ < 256 ? CTXSZ : 256u;

		disc_d024 = 0;
		disc_run(-1, 0, 0, seed, &d100, &d108);
		b24 = disc_d024;
		for (si = 0; si < 2 && cmp_reg < 0; si++) {
			for (reg = 0; reg < 2 && cmp_reg < 0; reg++) {
				for (off = reg ? 0 : 12; off < (reg ? 73u : lim2 - 3); off += reg ? 1 : 4) {
					disc_d024 = 0;
					disc_run(reg ? 2 : 0, off, sent[si], seed, &d100, &d108);
					if ((disc_d024 & 0x10000000U) && !(b24 & 0x10000000U)) {
						cmp_reg = reg;
						cmp_off = off;
						cmp_isflag = si;
						break;
					}
				}
			}
		}
		if (cmp_reg < 0) {
			printf("t32_mdnsx_oracle: discover: compression setting of func_en not found\n");
			exit(1);
		}
		printf("t32_mdnsx_oracle: func_en compression %s from %s+%u\n", cmp_isflag ? "flag" : "setting",
		       cmp_reg ? "dc074" : "ctx", cmp_off);
	}
	if (dim_reg[0] < 0 || dim_reg[1] < 0) {
		printf("t32_mdnsx_oracle: discover: frame size location not found in ctx / dc284\n");
		exit(1);
	}
	printf("t32_mdnsx_oracle: frame size read from %s+%u (width), %s+%u (height)\n",
	       dim_reg[0] ? "dc284" : "ctx", dim_off[0], dim_reg[1] ? "dc284" : "ctx", dim_off[1]);
}

/* ---- param_array_set ---- */
static void t_set(void)
{
	static u8 secb[T32_MDNS_SEC_BYTES], valb[T32_MDNS_VALUES + 16], inb[T32_MDNS_PART1_BYTES];
	static u8 osec[T32_MDNS_SEC_BYTES], oval[T32_MDNS_VALUES + 16];
	u32 ch = rnd() & 1 & dcm, part = rnd() % 3, w = 64 + rnd() % 4000, h = 64 + rnd() % 3000;
	u32 gain = (rnd() % 11) << 16 | (rnd() & 0xffff), kind = rnd() % 3, i;
	u16 mo = pick_memopt();
	u8 *ctx, *sec, *val, *in, rec[76];
	struct t32_mdns_ctx c;
	int on, dn;

	for (i = 0; i < T32_MDNS_SEC_BYTES; i++)
		secb[i] = kind ? (u8)rnd() : (u8)(rnd() & 7);
	/* zeroed like the vendor's value block: intp leaves the unmapped bytes alone */
	memset(valb, 0, sizeof(valb));
	for (i = 0; i < sizeof(inb); i++)
		inb[i] = kind == 1 ? (u8)(rnd() & 7) : (u8)rnd();
	if (rnd() % 4 == 0)
		secb[0x13a] = (u8)(64 + (rnd() & 1) + 66 * (rnd() & 1));
	xo_dn = rnd() & 1;
	memset(rec, 0, 76);
	rec[11] = (u8)(rnd() % 3);	/* flag11 / noref of the record: func_en mode field */
	memcpy(rec + 8, &mo, 2);

	t32o_reset();
	memcpy(t32_data_dc074 + ch * 76, rec, 76);
	memset(t32_data_dc284 + ch * 904, 0, 904);
	t32_data_f0900[ch] = 0;
	ctx = t32o_alloc(CTXSZ);
	sec = t32o_alloc(T32_MDNS_SEC_BYTES);
	val = t32o_alloc(T32_MDNS_VALUES + 16);
	in = t32o_alloc(T32_MDNS_PART1_BYTES);
	memset(ctx, 0, CTXSZ);
	memcpy(sec, secb, sizeof(secb));
	memcpy(val, valb, sizeof(valb));
	memcpy(in, inb, sizeof(inb));
	W32(ctx, 0) = T32O_PTR(sec);
	W32(ctx, 4) = T32O_PTR(val);
	W32(ctx, 8) = gain;
	put_dims(ctx, ch, w, h);
	put_cmp(ctx, ch, mo);
	t32_data_e22c8[ch] = T32O_PTR(ctx);
	REF(tisp_mdns_param_array_set, ch, part, T32O_PTR(in), 0, 0);
	snap();

	memcpy(osec, secb, sizeof(secb));
	memcpy(oval, valb, sizeof(valb));
	memset(&c, 0, sizeof(c));
	c.sec = osec;
	c.val = oval;
	c.gain = gain;
	c.width = w;
	c.height = h;
	c.memopt = mo;
	c.noref = rec[11];
	dn = (int)(u32)tisp_day_or_night_g_ctrl(ch, 0, 0, 0);
	on = t32_mdns_param_array_set(&c, part, inb, dn, ow);
	if (on < 0) {
		refused++;
		return;
	}
	CHECK(same_writes(on), "param_array_set writes");
	CHECK(!memcmp(sec, osec, sizeof(osec)), "param_array_set section");
	CHECK(!memcmp(val, oval, T32_MDNS_VALUES), "param_array_set values");
	CHECK(W32(ctx, 8) == c.gain, "param_array_set gain");
}

/* ---- sleep ---- */
static u32 xo_rd(u32 reg)
{
	return reg * 0x9e3779b1u ^ xo_salt;
}

static u32 o_rd(u32 reg)
{
	return (u32)tisp_reg_read(0, reg, 0, 0);
}

static void t_sleep(void)
{
	u32 ch = rnd() & 3;
	struct t32_mdns_shadow sh;

	xo_salt = rnd();
	t32o_rd_hook = xo_rd;
	t32o_reset();
	fill(t32_data_e227c, sizeof(t32_data_e227c));
	fill(t32_data_e21d4, sizeof(t32_data_e21d4));
	fill(t32_data_e226c, sizeof(t32_data_e226c));
	fill(t32_data_e2228, sizeof(t32_data_e2228));
	fill(t32_data_e21ec, sizeof(t32_data_e21ec));
	REF(tisp_mdns_sleep, ch, 0, 0, 0, 0);
	memset(&sh, 0x5a, sizeof(sh));
	t32_mdns_sleep(&sh, o_rd);
	CHECK(!memcmp(sh.a, t32_data_e227c, sizeof(sh.a)), "sleep e227c");
	CHECK(!memcmp(sh.b, t32_data_e21d4, sizeof(sh.b)), "sleep e21d4");
	CHECK(!memcmp(sh.c, t32_data_e226c, sizeof(sh.c)), "sleep e226c");
	CHECK(!memcmp(sh.d, t32_data_e2228, sizeof(sh.d)), "sleep e2228");
	CHECK(!memcmp(sh.e, t32_data_e21ec, sizeof(sh.e)), "sleep e21ec");
}

/* ---- addr_alloc / addr_free ---- */
static u32 our_lock_n, our_unlock_n, our_sets, our_set;
static void a_lock(u8 ch) { (void)ch; our_lock_n++; }
static void a_unlock(u8 ch) { (void)ch; our_unlock_n++; }
static u32 a_reg(u32 reg) { return (u32)system_reg_read(reg, 0, 0, 0); }
static u32 a_get(u8 ch)
{
	xo_cbuf = 0;
	tisp_g_module_control(ch, T32O_PTR(&xo_cbuf), 0, 0);
	return xo_cbuf;
}
static void a_set(u8 ch, u32 v) { (void)ch; our_sets++; our_set = v; }
static const struct t32_mdns_ops a_ops = { a_lock, a_unlock, a_reg, a_get, a_set };

static void t_addr(int which_free)
{
	u32 ch = rnd() & 1, mode = rnd(), pass, r0 = 0, o1 = 0, o2 = 0, pv = 0, set0 = 0, sets0 = 0, l0 = 0, u0 = 0;

	for (pass = 0; pass < 2; pass++) {
		u8 *ctx, *p, *out;
		u32 pre = (mode & 4) ? 0xffffffffU : rnd() * 0 + 0x12345678U;

		t32o_reset();
		ctx = t32o_alloc(CTXSZ);
		p = t32o_alloc(16);
		out = t32o_alloc(8);
		memset(ctx, 0, CTXSZ);
		W32(p, 0) = pre;
		W32(out, 0) = 0x11111111U;
		W32(out, 4) = 0x22222222U;
		t32_data_e22c8[ch] = T32O_PTR(ctx);
		xo_sysreg = (mode & 8) ? 0 : T32O_PTR(p) + T32_MDNS_ADDR_BASE;
		xo_ctrl = (mode & 16) ? 0x800U : 0x1U;
		xo_lock_n = xo_unlock_n = xo_ctrl_sets = xo_ctrl_set = 0;
		our_lock_n = our_unlock_n = our_sets = our_set = 0;
		if (!pass) {
			if (which_free)
				r0 = (u32)REF(tisp_mdns_addr_free, ch, 0, 0, 0, 0);
			else
				r0 = (u32)REF(tisp_mdns_addr_alloc, ch, T32O_PTR(out), T32O_PTR(out + 4), 0, 0);
			o1 = W32(out, 0);
			o2 = W32(out, 4);
			pv = W32(p, 0);
			set0 = xo_ctrl_set;
			sets0 = xo_ctrl_sets;
			l0 = xo_lock_n;
			u0 = xo_unlock_n;
		} else {
			u32 a = 0x11111111U, s = 0x22222222U;
			int ret = which_free ? t32_mdns_addr_free(&a_ops, (u8)ch) : t32_mdns_addr_alloc(&a_ops, (u8)ch, &a, &s);

			CHECK((u32)ret == r0, "addr return");
			if (!which_free)
				CHECK(a == o1 && s == o2, "addr outputs");
			CHECK(W32(p, 0) == pv, "addr marker word");
			CHECK(our_sets == sets0 && (!sets0 || our_set == set0), "addr module control");
			CHECK(our_lock_n == l0 && our_unlock_n == u0, "addr mutex");
		}
	}
}

int main(void)
{
	unsigned int rounds = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 3000, c;
	int skip_addr;

	/* probes: are our weak objects / stubs the linked ones? (before any write) */
	own_e4100 = t32_data_e4100[12616] == 0xa5 && t32_data_e4100[12617] == 0x5a &&
		    t32_data_e4100[12618] == 0xc3 && t32_data_e4100[12619] == 0x3c;
	own_dc074 = t32_data_dc074[150] == 0x5a && t32_data_dc074[151] == 0xa5;
	dcm = own_dc074 ? 1 : 0;
	if (!own_e4100)
		printf("t32_mdnsx_oracle: t32_data_e4100 from env.c (size unknown): set_mem_cfg hold == 1 path not exercised\n");
	if (!own_dc074)
		printf("t32_mdnsx_oracle: t32_data_dc074 from env.c (size unknown): channel 0 only for its records\n");
	xo_pk_n = 0;
	printk(0, 0, 0xabc, 0);
	act_pk = xo_pk_n == 1 && xo_pk_a2 == 0xabc;
	xo_fr_n = 0;
	private_kfree(0x1234, 0, 0, 0);
	act_fr = xo_fr_n == 1 && xo_fr[0].ptr == 0x1234;
	xo_fr_n = 0;
	private_vfree(0x4321, 0, 0, 0);
	act_fr = act_fr && xo_fr_n == 1 && xo_fr[0].kind == 2;
	xo_sysreg = 0x1234;
	act_addr = (u32)system_reg_read(1, 0, 0, 0) == 0x1234;
	xo_ctrl = 0x55;
	xo_cbuf = 0;
	tisp_g_module_control(0, T32O_PTR(&xo_cbuf), 0, 0);
	act_addr = act_addr && xo_cbuf == 0x55;
	xo_ctrl_sets = 0;
	tisp_s_module_control(0, 0x77, 0, 0);
	act_addr = act_addr && xo_ctrl_sets == 1 && xo_ctrl_set == 0x77;
	xo_lock_n = 0;
	private_mutex_lock(0, 0, 0, 0);
	act_addr = act_addr && xo_lock_n == 1;
	skip_addr = !act_addr;
	discover();

	for (c = 0; c < rounds; c++) {
		cs = c;
		t_wdr();
		t_getmem();
		t_setmem();
		t_malloc();
		t_deinit();
		t_dnapi();
		t_get();
		t_set();
		t_sleep();
		if (!skip_addr) {
			t_addr(0);
			t_addr(1);
		}
	}
	printf("t32_mdnsx_oracle: %u rounds, %u compared (%u refused by ours), 0 mismatches\n",
	       rounds, compared, refused);
	if (skip_addr)
		printf("t32_mdnsx_oracle: addr_alloc/addr_free: SKIP (env.c provides its own system_reg_read/module_control stubs)\n");
	if (!act_pk)
		printf("t32_mdnsx_oracle: set_mem_cfg warning check: SKIP (env.c printk)\n");
	if (!act_fr)
		printf("t32_mdnsx_oracle: deinit free order: SKIP (env.c kfree/vfree)\n");
	return 0;
}
