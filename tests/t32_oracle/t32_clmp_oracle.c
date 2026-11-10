// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: CLM parameter accessors (tx_isp_t32_clm.h):
 * param_array_get, param_array_set, deinit.  Runs the unmodified vendor
 * functions (Matt Davis' recovered C, PR #15 @ 2e2e06bc) and our port from the
 * same state of four channels (the other three are decoys that must stay
 * unchanged) and compares every register write (address, value, order), the
 * section copies, the colour temperature / zone code / both tables / range
 * ratios of the block state, the zone array A and the byte tables B (whole
 * allocation), their sizes, the allocation sizes (t32_data_dc1a4), the two
 * globals t32_data_da850 + 242 / + 244, the output buffer with a guard behind
 * it, the length word, the order of the allocate / free calls and the returns.
 * Vendor bytes of the block state that we do not model (+40 .. +95, +2200,
 * +3261 ..) must not change at all.  Every set case also runs the vendor with
 * t32_data_d03a0 = 1 (shadow addresses): only the addresses may differ,
 * through a one to one mapping.  The callees (ct_interp, write_lut /
 * lut_write_lut, calc_para_refresh, write_reg) are the vendor's own on the
 * vendor side and our ports on ours.
 */
#include <signal.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_front.h"
#include "tx_isp_t32_clm.h"

#define SECB	T32_CLM_SEC_BYTES
#define ZB	T32_CLM_A_BYTES
#define TB	T32_CLM_TABLE
#define MAXN	64
#define MAXA	(MAXN * 2102 + 6000)
#define MAXB	(MAXN * 1050 + 6000)
#define MAXOUT	(SECB + MAXN * 3152 + 16)
#define MAXW	2048
#define CSC_OFF	27488u
#define CTXB	3300

extern u32 t32_data_e28d0[4];
extern u32 t32_data_dc274[4];
extern u64 tisp_clm_param_array_get(u32, u32, u32, u32);
extern u64 tisp_clm_param_array_set(u32, u32, u32, u32);
extern u64 tisp_clm_deinit(u32, u32, u32, u32);
extern u64 tisp_csc_api_get(u32, u32, u32, u32);

/* the allocate / free observer (the env.c stubs are weak) */
static u32 ev[16], kn;
static u32 g_a[4], g_b[4], g_cx[4];	/* vendor initial A / B / block state addresses */
static const void *p_a[4], *p_b[4], *p_cx[4];
static u8 ppool[4u << 20];
static u32 ppool_used;

static void note(const void *p, int port)
{
	u32 k, kind = 4, c = 9;

	for (k = 0; k < 4; k++) {
		if (p == (port ? p_a[k] : (const void *)(uintptr_t)g_a[k]) && p) {
			kind = 1; c = k;
		}
		if (p == (port ? p_b[k] : (const void *)(uintptr_t)g_b[k]) && p) {
			kind = 2; c = k;
		}
		if (p == (port ? p_cx[k] : (const void *)(uintptr_t)g_cx[k]) && p) {
			kind = 5; c = k;
		}
	}
	if (kn < 16)
		ev[kn] = kind << 28 | c << 24;
	kn++;
}

u64 private_vfree(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	note((const void *)(uintptr_t)a0, 0);
	return 0;
}

u64 private_vmalloc(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	if (kn < 16)
		ev[kn] = 3u << 28 | (a0 & 0xffffff);
	kn++;
	return a0 ? (u32)(uintptr_t)t32o_alloc(a0) : 0;
}

static void *port_valloc(unsigned long sz)
{
	void *p = ppool + ppool_used;

	if (kn < 16)
		ev[kn] = 3u << 28 | ((u32)sz & 0xffffff);
	kn++;
	ppool_used += ((u32)sz + 63u) & ~63u;
	if (ppool_used > sizeof(ppool)) {
		fprintf(stderr, "port pool exhausted\n");
		exit(2);
	}
	memset(p, 0, sz);
	return p;
}

static void port_vfree(void *p)
{
	note(p, 1);
}

/* tisp_reg_write replaced as in t32_mscac_oracle / t32_dpcp_oracle */
u64 system_reg_write(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_reg_write(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a3;
	if (t32_data_d03a0[0] != 1)
		return system_reg_write(a1, a2, 0, 0);
	if (t32o_log.n < T32O_MAX_WRITES) {
		t32o_log.w[t32o_log.n].reg = a1;
		t32o_log.w[t32o_log.n].val = a2;
		t32o_log.n++;
	}
	return 0;
}

/* tisp_reg_lut_write (fn_1c6fc): the same, with an index offset a3 */
u64 tisp_reg_lut_write(u32 a0, u32 a1, u32 a2, u32 a3)
{
	return tisp_reg_write(a0, a1 + a3, a2, 0);
}

static u32 rs = 0x3a91c7u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

struct st {
	u8 sec[4][64];
	u32 ct[4];
	u8 zone[4];
	s16 tab[4][TB];
	u8 tb[4][TB];
	u16 ratio[4][2], shift[4][2];
	u8 present[4];
	u16 asz[4], bsz[4];
	u32 acap[4], bcap[4];
	u8 anull[4], bnull[4], achg[4], bchg[4];
	u16 glob[2];
};

struct bufs {
	u8 a[4][MAXA];
	u8 b[4][MAXB];
};

struct res {
	struct st s;
	unsigned int n;
	struct t32_front_write w[MAXW];
	u8 out[MAXOUT];
	u32 len, ret, kn, kev[16];
	u8 unmapped_bad;
};

static unsigned int compared, skipped, seen[3][2];
static u32 idx2reg[0x10000], reg2idx[0x40000];
static struct t32_clm_ctx pc[4];
static struct t32_clm_info pi[4];
static u8 ps[4][64], pa[4][MAXA], pb[4][MAXB];
static sigjmp_buf fault_env;

static void fault_handler(int sig)
{
	(void)sig;
	siglongjmp(fault_env, 1);
}

static void put16(u8 *p, int v)
{
	p[0] = (u8)v; p[1] = (u8)(v >> 8);
}

/* returns 1 when the vendor faulted (memory the oracle cannot provide) */
static int run(int which, u32 d03, u32 ch, const struct st *in, const struct bufs *ib, const u8 *buf, u32 buflen,
	       const u8 *csc, int ref, struct res *r, struct bufs *ob)
{
	static u8 pre[CTXB];
	unsigned int i, mode = csc[0x5b];
	u8 *sec[4], *cx[4], *info[4], *a[4], *b[4], *base, *out, *lp, *bp;
	struct t32_clm_ctx *slot[4];
	const int *m1 = t32_csc_modes[mode].m;

	memset(r, 0, sizeof(*r));
	kn = 0;
	t32o_reset();
	ppool_used = 0;
	t32_data_d03a0[0] = (u8)d03;
	base = t32o_alloc(0x20000);
	out = t32o_alloc(MAXOUT);
	lp = t32o_alloc(4);
	bp = t32o_alloc(buflen + 64);
	memset(out, 0xa5, MAXOUT);
	memcpy(bp, buf, buflen);
	r->len = 0x77777777u;
	memcpy(lp, &r->len, 4);
	memcpy(base + CSC_OFF, csc, 104);
	for (i = 0; i < 4; i++) {
		sec[i] = t32o_alloc(64);
		cx[i] = t32o_alloc(CTXB);
		info[i] = t32o_alloc(64);
		a[i] = t32o_alloc(in->acap[i] + 1);
		b[i] = t32o_alloc(in->bcap[i] + 1);
		memcpy(sec[i], in->sec[i], 64);
		memcpy(a[i], ib->a[i], in->acap[i]);
		memcpy(b[i], ib->b[i], in->bcap[i]);
		if (ref) {
			u32 *c32 = (u32 *)cx[i];
			unsigned int k;

			c32[0] = T32O_PTR(sec[i]);
			for (k = 0; k < 9; k++)
				c32[1 + k] = 0x9e3779b1u * (i + k + 1);
			c32[24] = in->ct[i];
			memcpy(cx[i] + 100, in->tab[i], 2 * TB);
			memcpy(cx[i] + 2202, in->tb[i], TB);
			put16(cx[i] + 3252, in->ratio[i][0]);
			put16(cx[i] + 3254, in->ratio[i][1]);
			put16(cx[i] + 3256, in->shift[i][0]);
			put16(cx[i] + 3258, in->shift[i][1]);
			cx[i][3260] = in->zone[i];
			memset(cx[i] + 40, 0xc3, 56);	/* fields we do not model */
			put16(cx[i] + 2200, 0x1234);
			memset(cx[i] + 3261, 0x5c, CTXB - 3261);
			put16(info[i] + 10, in->asz[i]);
			put16(info[i] + 12, in->bsz[i]);
			*(u32 *)(info[i] + 36) = in->anull[i] ? 0 : T32O_PTR(a[i]);
			*(u32 *)(info[i] + 40) = in->bnull[i] ? 0 : T32O_PTR(b[i]);
			memset(info[i] + 44, 0x77, 4);
			*(u32 *)(t32_data_dc1a4 + 28 * i + 20) = in->acap[i];
			*(u32 *)(t32_data_dc1a4 + 28 * i + 24) = in->bcap[i];
			t32_data_e28d0[i] = in->present[i] ? T32O_PTR(cx[i]) : 0;
			t32_data_dc274[i] = T32O_PTR(info[i]);
			t32_data_dd0a4[i] = T32O_PTR(base);
			g_a[i] = in->anull[i] ? 0 : T32O_PTR(a[i]);
			g_b[i] = in->bnull[i] ? 0 : T32O_PTR(b[i]);
			g_cx[i] = T32O_PTR(cx[i]);
		} else {
			memcpy(ps[i], in->sec[i], 64);
			memcpy(pa[i], ib->a[i], in->acap[i]);
			memcpy(pb[i], ib->b[i], in->bcap[i]);
			memset(&pc[i], 0, sizeof(pc[i]));
			memset(&pi[i], 0, sizeof(pi[i]));
			pc[i].sec = ps[i];
			pc[i].ct = in->ct[i];
			memcpy(pc[i].tab, in->tab[i], 2 * TB);
			memcpy(pc[i].tb, in->tb[i], TB);
			pc[i].ratio[0] = in->ratio[i][0];
			pc[i].ratio[1] = in->ratio[i][1];
			pc[i].shift[0] = in->shift[i][0];
			pc[i].shift[1] = in->shift[i][1];
			pc[i].zone = in->zone[i];
			pc[i].mode = mode;
			pi[i].a = in->anull[i] ? NULL : pa[i];
			pi[i].b = in->bnull[i] ? NULL : pb[i];
			pi[i].asz = in->asz[i];
			pi[i].bsz = in->bsz[i];
			pi[i].a_cap = in->acap[i];
			pi[i].b_cap = in->bcap[i];
			slot[i] = in->present[i] ? &pc[i] : NULL;
			p_a[i] = pi[i].a;
			p_b[i] = pi[i].b;
			p_cx[i] = &pc[i];
		}
	}
	if (ref) {

		/* the csc module of the channel, the matrix the block state keeps */
		t32_data_d03a0[0] = 0;
		t32o_call((t32o_fn)(void (*)(void))tisp_csc_init, ch, 0, 0, 0, 0);
		t32o_call((t32o_fn)(void (*)(void))tisp_csc_api_get, ch, T32O_PTR(cx[ch] + 4), 0, 0, 0);
		t32_data_d03a0[0] = (u8)d03;
		t32o_log.n = 0;
		kn = 0;
		memcpy(pre, cx[ch], CTXB);
		if (sigsetjmp(fault_env, 1))
			return 1;
		switch (which) {
		case 0: r->ret = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_clm_param_array_get, ch, T32O_PTR(out), T32O_PTR(lp), 0, 0); break;
		case 1: r->ret = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_clm_param_array_set, ch, T32O_PTR(bp), 0, 0, 0); break;
		default: r->ret = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_clm_deinit, ch, 0, 0, 0, 0); break;
		}
		r->n = t32o_log.n;
		if (r->n > MAXW) {
			fprintf(stderr, "ref wrote %u > MAXW\n", r->n);
			exit(2);
		}
		for (i = 0; i < r->n; i++) {
			r->w[i].reg = t32o_log.w[i].reg;
			r->w[i].val = t32o_log.w[i].val;
		}
		for (i = 0; i < 4; i++) {
			u32 *c32 = (u32 *)cx[i];

			r->s.present[i] = t32_data_e28d0[i] ? 1 : 0;
			memcpy(r->s.sec[i], sec[i], 64);
			r->s.ct[i] = c32[24];
			memcpy(r->s.tab[i], cx[i] + 100, 2 * TB);
			memcpy(r->s.tb[i], cx[i] + 2202, TB);
			r->s.ratio[i][0] = (u16)(cx[i][3252] | cx[i][3253] << 8);
			r->s.ratio[i][1] = (u16)(cx[i][3254] | cx[i][3255] << 8);
			r->s.shift[i][0] = (u16)(cx[i][3256] | cx[i][3257] << 8);
			r->s.shift[i][1] = (u16)(cx[i][3258] | cx[i][3259] << 8);
			r->s.zone[i] = cx[i][3260];
			r->s.asz[i] = (u16)(info[i][10] | info[i][11] << 8);
			r->s.bsz[i] = (u16)(info[i][12] | info[i][13] << 8);
			r->s.acap[i] = *(u32 *)(t32_data_dc1a4 + 28 * i + 20);
			r->s.bcap[i] = *(u32 *)(t32_data_dc1a4 + 28 * i + 24);
			r->s.anull[i] = *(u32 *)(info[i] + 36) ? 0 : 1;
			r->s.bnull[i] = *(u32 *)(info[i] + 40) ? 0 : 1;
			r->s.achg[i] = *(u32 *)(info[i] + 36) != g_a[i];
			r->s.bchg[i] = *(u32 *)(info[i] + 40) != g_b[i];
			if (!r->s.anull[i])
				memcpy(ob->a[i], (void *)(uintptr_t)*(u32 *)(info[i] + 36),
				       r->s.acap[i] < MAXA ? r->s.acap[i] : MAXA);
			if (!r->s.bnull[i])
				memcpy(ob->b[i], (void *)(uintptr_t)*(u32 *)(info[i] + 40),
				       r->s.bcap[i] < MAXB ? r->s.bcap[i] : MAXB);
		}
		r->s.glob[0] = (u16)(t32_data_da850[242] | t32_data_da850[243] << 8);
		r->s.glob[1] = (u16)(t32_data_da850[244] | t32_data_da850[245] << 8);
		{	/* the bytes we do not model must not change */
			static const struct { int lo, hi; } mapped[] = { { 96, 2200 }, { 2202, 3261 } };
			unsigned int j, m;

			for (j = 0; j < CTXB; j++) {
				int skip = 0;

				for (m = 0; m < 2; m++)
					if ((int)j >= mapped[m].lo && (int)j < mapped[m].hi)
						skip = 1;
				if (!skip && pre[j] != cx[ch][j])
					r->unmapped_bad = 1;
			}
		}
		for (i = 0; i < kn && i < 16; i++)
			r->kev[i] = ev[i];
		r->kn = kn;
		memcpy(&r->len, lp, 4);
		memcpy(r->out, out, MAXOUT);
	} else {
		static struct t32_front_write w[MAXW];
		int rc = 0;

		switch (which) {
		case 0: r->ret = (u32)t32_clm_param_array_get(slot[ch], &pi[ch], out, &r->len); break;
		case 1:
			/* the vendor's csc module state of the channel: the matrix of the mode */
			t32_csc_inverse(mode, pc[ch].csc);
			rc = t32_clm_param_array_set(slot[ch], &pi[ch], r->s.glob, bp, port_valloc, port_vfree, m1, w);
			r->n = (unsigned int)rc;
			break;
		default: r->ret = (u32)t32_clm_deinit(port_vfree, &slot[ch]); break;
		}
		if (which != 1)
			t32_csc_inverse(mode, pc[ch].csc);
		memcpy(r->w, w, r->n * sizeof(w[0]));
		for (i = 0; i < 4; i++) {
			r->s.present[i] = slot[i] ? 1 : 0;
			memcpy(r->s.sec[i], ps[i], 64);
			r->s.ct[i] = pc[i].ct;
			memcpy(r->s.tab[i], pc[i].tab, 2 * TB);
			memcpy(r->s.tb[i], pc[i].tb, TB);
			r->s.ratio[i][0] = (u16)pc[i].ratio[0];
			r->s.ratio[i][1] = (u16)pc[i].ratio[1];
			r->s.shift[i][0] = (u16)pc[i].shift[0];
			r->s.shift[i][1] = (u16)pc[i].shift[1];
			r->s.zone[i] = pc[i].zone;
			r->s.asz[i] = pi[i].asz;
			r->s.bsz[i] = pi[i].bsz;
			r->s.acap[i] = pi[i].a_cap;
			r->s.bcap[i] = pi[i].b_cap;
			r->s.anull[i] = pi[i].a ? 0 : 1;
			r->s.bnull[i] = pi[i].b ? 0 : 1;
			r->s.achg[i] = pi[i].a != p_a[i];
			r->s.bchg[i] = pi[i].b != p_b[i];
			if (pi[i].a)
				memcpy(ob->a[i], pi[i].a, pi[i].a_cap < MAXA ? pi[i].a_cap : MAXA);
			if (pi[i].b)
				memcpy(ob->b[i], pi[i].b, pi[i].b_cap < MAXB ? pi[i].b_cap : MAXB);
		}
		for (i = 0; i < kn && i < 16; i++)
			r->kev[i] = ev[i];
		r->kn = kn;
		memcpy(r->out, out, MAXOUT);
	}
	return 0;
}

/* mode 1 must differ from mode 0 only in the addresses */
static int transport_same(const char *what, const struct res *a, const struct res *b, const struct bufs *x,
			  const struct bufs *y)
{
	unsigned int i;
	u32 lastp[2] = { 0, 0 };

	if (a->n != b->n || memcmp(&a->s, &b->s, sizeof(a->s)) || memcmp(a->out, b->out, sizeof(a->out)) ||
	    a->len != b->len || a->ret != b->ret || a->kn != b->kn || memcmp(x, y, sizeof(*x))) {
		printf("MISMATCH %s: shadow mode (d03a0 1) changes more than the addresses\n", what);
		return 1;
	}
	for (i = 0; i < a->n; i++) {
		u32 reg = a->w[i].reg, v = b->w[i].reg;

		if (a->w[i].val != b->w[i].val) {
			printf("MISMATCH %s: shadow mode write #%u value %08x / %08x\n", what, i, a->w[i].val, b->w[i].val);
			return 1;
		}
		if (v == reg)
			continue;
		if (reg == 0x320c0 || reg == 0x320c4 || reg == 0x32100 || reg == 0x32104) {
			/* the LUT ports: the shadow holds the table in increasing slots */
			if (lastp[reg >= 0x32100] && v <= lastp[reg >= 0x32100]) {
				printf("MISMATCH %s: shadow table slot %u not increasing\n", what, v);
				return 1;
			}
			lastp[reg >= 0x32100] = v;
			if (v >= 0x10000 || (idx2reg[v] && idx2reg[v] != reg)) {
				printf("MISMATCH %s: shadow index %u <-> register %05x\n", what, v, reg);
				return 1;
			}
			idx2reg[v] = reg;
			continue;
		}
		if (v >= 0x10000 || reg >= 0x40000 || (idx2reg[v] && idx2reg[v] != reg) ||
		    (reg2idx[reg] && reg2idx[reg] != v + 1)) {
			printf("MISMATCH %s: shadow index %u <-> register %05x not one to one\n", what, v, reg);
			return 1;
		}
		idx2reg[v] = reg;
		reg2idx[reg] = v + 1;
	}
	return 0;
}

static void fill_zones(u8 *a, u8 *b, unsigned int n, unsigned int style)
{
	unsigned int z, j;
	int ct = (int)(rnd() % 40) - 20;

	for (z = 0; z < n; z++) {
		put16(a + z * ZB, style ? (int)(rnd() & 0xffff) - 32768 : ct);
		for (j = 0; j < TB; j++)
			put16(a + z * ZB + 2 + 2 * j, (int)(rnd() % 4096) - 2048);
		for (j = 0; j < TB; j++)
			b[z * TB + j] = (u8)rnd();
		if (rnd() % 3 == 0)
			b[z * TB] = (u8)((int)(rnd() % 40) - 20);
		ct += (int)(rnd() % 60) + (int)(rnd() % 3 ? 0 : 1);
	}
}

int main(void)
{
	static const char *const names[3] = { "param_array_get", "param_array_set", "deinit" };
	unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 1500;
	unsigned int c, i, k;
	struct sigaction sa;
	static struct st in;
	static struct bufs ib, ob_r, ob_o, ob_1;
	static struct res r, o, r1;
	static u8 buf[SECB + MAXN * 3152 + 64];

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = fault_handler;
	sigaction(SIGSEGV, &sa, NULL);
	sigaction(SIGBUS, &sa, NULL);

	/* a zero zone count: the port stops before the tables (the vendor reads before its zone array) */
	{
		static struct t32_clm_ctx zc;
		static struct t32_clm_info zi;
		static u8 zs[64], zb[SECB + 64], za[8];
		static struct t32_front_write zw[MAXW];
		u16 g[2];

		zc.sec = zs;
		zi.a = za;
		zi.b = za;
		zi.a_cap = zi.b_cap = 8;
		if (t32_clm_param_array_set(&zc, &zi, g, zb, port_valloc, port_vfree, t32_csc_modes[0].m, zw) != -1) {
			printf("MISMATCH set with zone count 0 does not stop\n");
			return 1;
		}
	}

	for (c = 0; c < n; c++)
		for (k = 0; k < 3; k++) {
			u8 csc[104];
			u32 ch = rnd() & 3, nn, blen = 0, nnew;

			if (k == 1)
				ch = 0;	/* the vendor writes other registers for a channel > 0 */

			memset(&in, 0, sizeof(in));
			for (i = 0; i < 104; i++)
				csc[i] = (u8)rnd();
			csc[0x5b] = (u8)(rnd() % 6);
			nnew = rnd() % 4 == 0 ? rnd() % (MAXN + 1) : 1 + rnd() % 6;
			if (k == 1 && nnew == 0)
				nnew = 1;
			for (i = 0; i < 4; i++) {
				unsigned int j;

				for (j = 0; j < SECB; j++)
					in.sec[i][j] = (u8)rnd();
				memset(in.sec[i] + SECB, 0xa5, 24);
				nn = rnd() % 4 == 0 ? rnd() % (MAXN + 1) : 1 + rnd() % 6;
				if (i == ch && k == 0)
					nn = nnew;
				in.sec[i][0x23] = (u8)nn;
				in.sec[i][0x24] = (u8)(rnd() % 3 ? rnd() % 30 : rnd() % 256);
				in.ct[i] = rnd() % 4 ? (u32)((int)(rnd() % 400) - 40) : rnd();
				for (j = 0; j < TB; j++) {
					in.tab[i][j] = (s16)rnd();
					in.tb[i][j] = (u8)rnd();
				}
				for (j = 0; j < 2; j++) {
					in.ratio[i][j] = (u16)rnd();
					in.shift[i][j] = (u16)rnd();
				}
				in.zone[i] = (u8)rnd();
				in.present[i] = i == ch ? (k == 2 ? rnd() % 4 != 0 : 1) : (u8)(rnd() % 4 != 0);
				in.asz[i] = (u16)rnd();
				in.bsz[i] = (u16)rnd();
				if (k == 1 && i == ch) {	/* capacities around the new need */
					static const int dl[6] = { 0, -1, 1, 0, 2000, -7 };
					u32 na = nnew * 2102, nb = nnew * 1050;

					in.acap[i] = rnd() % 4 == 0 ? rnd() % (na + 1) : na + (u32)dl[rnd() % 6];
					in.bcap[i] = rnd() % 4 == 0 ? rnd() % (nb + 1) : nb + (u32)dl[rnd() % 6];
					if ((int)in.acap[i] < 0)
						in.acap[i] = 0;
					if ((int)in.bcap[i] < 0)
						in.bcap[i] = 0;
					if (in.acap[i] > MAXA)
						in.acap[i] = MAXA;
					if (in.bcap[i] > MAXB)
						in.bcap[i] = MAXB;
				} else {
					in.acap[i] = nn * 2102 + (rnd() % 3 ? 0 : rnd() % 3000);
					in.bcap[i] = nn * 1050 + (rnd() % 3 ? 0 : rnd() % 3000);
				}
				in.anull[i] = in.acap[i] == 0 ? rnd() % 2 : (rnd() % 12 == 0);
				in.bnull[i] = in.bcap[i] == 0 ? rnd() % 2 : (rnd() % 12 == 0);
				if (k == 0 && i == ch)
					in.anull[i] = in.bnull[i] = 0;
				for (j = 0; j < in.acap[i]; j++)
					ib.a[i][j] = (u8)rnd();
				for (j = 0; j < in.bcap[i]; j++)
					ib.b[i][j] = (u8)rnd();
				if (in.acap[i] >= nn * 2102 && in.bcap[i] >= nn * 1050 && rnd() % 2)
					fill_zones(ib.a[i], ib.b[i], nn, rnd() % 8 == 0);
			}
			if (k == 1) {	/* the new parameter block */
				for (i = 0; i < SECB; i++)
					buf[i] = (u8)rnd();
				buf[0x23] = (u8)nnew;
				buf[0x24] = (u8)(rnd() % 3 ? rnd() % 30 : rnd() % 256);
				blen = SECB + nnew * 3152;
				fill_zones(buf + SECB, buf + SECB + nnew * 2102, nnew, rnd() % 8 == 0);
				if (rnd() % 3 == 0)
					in.ct[ch] = (u32)((int)(rnd() % 400) - 40);
			}
			memset(&ob_r, 0, sizeof(ob_r));
			memset(&ob_o, 0, sizeof(ob_o));
			if (run((int)k, 0, ch, &in, &ib, buf, blen, csc, 1, &r, &ob_r)) {
				skipped++;
				if (getenv("DBG"))
					printf("skip k %u ch %u n %u\n", k, ch, in.sec[ch][0x23]);
				continue;
			}
			run((int)k, 0, ch, &in, &ib, buf, blen, csc, 0, &o, &ob_o);
			compared++;
			seen[k][in.present[ch]]++;
			if (r.unmapped_bad) {
				printf("MISMATCH %s: the vendor changed bytes of the block state we do not model\n", names[k]);
				return 1;
			}
			if (r.n != o.n || memcmp(&r.s, &o.s, sizeof(r.s)) || memcmp(r.out, o.out, sizeof(r.out)) ||
			    r.len != o.len || r.ret != o.ret || r.kn != o.kn || memcmp(r.kev, o.kev, sizeof(r.kev)) ||
			    memcmp(&ob_r, &ob_o, sizeof(ob_r))) {
				printf("MISMATCH %s ch %u: writes %u/%u len %u/%u ret %u/%u events %u/%u or state\n", names[k], ch,
				       r.n, o.n, r.len, o.len, r.ret, o.ret, r.kn, o.kn);
				for (i = 0; i < sizeof(r.s); i++)
					if (((u8 *)&r.s)[i] != ((u8 *)&o.s)[i]) {
						printf("  state byte %u differs (vendor %02x ours %02x)\n", i, ((u8 *)&r.s)[i],
						       ((u8 *)&o.s)[i]);
						break;
					}
				for (i = 0; i < sizeof(ob_r); i++)
					if (((u8 *)&ob_r)[i] != ((u8 *)&ob_o)[i]) {
						printf("  buffer byte %u differs (vendor %02x ours %02x)\n", i, ((u8 *)&ob_r)[i],
						       ((u8 *)&ob_o)[i]);
						break;
					}
				return 1;
			}
			for (i = 0; i < r.n; i++)
				if (r.w[i].reg != o.w[i].reg || r.w[i].val != o.w[i].val) {
					printf("MISMATCH %s: write #%u vendor %08x=%08x ours %08x=%08x\n", names[k], i,
					       r.w[i].reg, r.w[i].val, o.w[i].reg, o.w[i].val);
					return 1;
				}
			if (k == 1) {
				memset(&ob_1, 0, sizeof(ob_1));
				if (run((int)k, 1, ch, &in, &ib, buf, blen, csc, 1, &r1, &ob_1)) {
					printf("MISMATCH %s: vendor faults in shadow mode only\n", names[k]);
					return 1;
				}
				if (transport_same(names[k], &r, &r1, &ob_r, &ob_1))
					return 1;
			}
		}
	for (k = 0; k < 3; k++)
		if (!seen[k][1] || (k == 2 && !seen[k][0])) {
			printf("t32_clmp_oracle: %s never ran both ways\n", names[k]);
			return 1;
		}
	printf("t32_clmp_oracle: %u compared, %u skipped, 0 mismatches\n", compared, skipped);
	return 0;
}
