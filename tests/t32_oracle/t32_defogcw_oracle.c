// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: t32_defog_count_weight35abc of driver/t32/tx_isp_t32_defogcw.h against the
 * unmodified vendor defog_count_weight35abc (0x5aa34) + defog_wei_interpcot (Matt Davis' recovered C,
 * PR #15 @ 2e2e06bc).  The reference has 11 arguments, so it runs on its own low stack here.
 * Per episode (pool reset) up to 8 calls on random channels 0..3 keep the per channel scratch
 * (e20e0 / e211c) alive, so the grow / keep / clear-whole-capacity paths are all taken.
 * Compared: thr24 (48 B) and thr31 (62 B) with 16 canary bytes each, the 5 weight curves (32 B each,
 * 16 canary bytes each), the scratch size, every byte of the scratch, and the vmalloc / vfree sequence
 * (sizes, which earlier allocation is freed).  Inputs: a / b 0..2700 / 0..2000 (n = 0 included),
 * long thin grids, gain 0..255 and full 32 bit (wrap of the scale factor).  Exit 0 = all equal.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_defogcw.h"

#define NCH 4
#define CAN 16

u64 defog_count_weight35abc(u32, u32, u32, u32, u32, u32, u32, u32, u32, u32, u32);

/* reference data objects (rodata values: data.c:2735..2778; .bss pointers / sizes; printk format) */
u16 t32_data_bfa30[24] = {
	255, 521, 799, 1090, 1395, 1717, 2057, 2417, 2799, 3207, 3643, 4114,
	4624, 5180, 5792, 6471, 7237, 8111, 9133, 10359, 11896, 13953, 17075, 23792 };
u16 t32_data_bfa60[64] = {
	133, 269, 407, 547, 689, 834, 982, 1132, 1285, 1440, 1599, 1761, 1926, 2094, 2266, 2442,
	2621, 2804, 2991, 3183, 3379, 3580, 3785, 3996, 4213, 4435, 4663, 4898, 5140, 5389, 5645, 5910,
	6183, 6465, 6758, 7061, 7375, 7702, 8042, 8397, 8767, 9155, 9562, 9989, 10440, 10916, 11421, 11959,
	12534, 13152, 13819, 14544, 15338, 16216, 17197, 18130, 19595, 21114, 22974, 25371, 28750, 34526, 40368 };
u32 t32_data_e20e0[NCH];
u32 t32_data_e211c[NCH];
u8 t32_data_c9958[64];
u8 t32_data_bfaf0[64];

/* allocation logs: kind 1 = vmalloc(size), 2 = vfree(allocation number, 0 = NULL) */
#define NLOG 64
struct alog { unsigned int n; u32 k[NLOG], v[NLOG]; void *ptr[NLOG]; unsigned int na; };
static struct alog lv, lo;

static void alog_add(struct alog *l, u32 k, u32 v)
{
	if (l->n < NLOG) { l->k[l->n] = k; l->v[l->n] = v; l->n++; }
}

static u32 alog_find(struct alog *l, void *p)
{
	unsigned int i;

	if (!p)
		return 0;
	for (i = 0; i < l->na; i++)
		if (l->ptr[i] == p)
			return i + 1;
	return 0xffffffffu;
}

u64 private_vmalloc(u32 a0, u32 a1, u32 a2, u32 a3)
{
	void *p = a0 ? t32o_alloc(a0) : NULL;

	(void)a1; (void)a2; (void)a3;
	alog_add(&lv, 1, a0);
	if (lv.na < NLOG)
		lv.ptr[lv.na++] = p;
	return (u32)(uintptr_t)p;
}

u64 private_vfree(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	alog_add(&lv, 2, alog_find(&lv, T32O_HOST(a0)));
	return 0;
}

static void *o_vmalloc(u32 size)
{
	void *p = size ? malloc(size) : NULL;

	if (p)
		memset(p, 0x5a, size);
	alog_add(&lo, 1, size);
	if (lo.na < NLOG)
		lo.ptr[lo.na++] = p;
	return p;
}

static void o_vfree(void *p)
{
	u32 i = alog_find(&lo, p);

	alog_add(&lo, 2, i);
	if (i && i != 0xffffffffu) {
		free(p);
		lo.ptr[i - 1] = NULL;	/* a later malloc may reuse the address */
	}
}

static const struct t32_defog_cw_ops ops = { o_vmalloc, o_vfree };

/* 11 argument call of the reference on a stack below 4 GiB */
static u8 cw_stack[1u << 20] __attribute__((aligned(64)));
static ucontext_t cw_main, cw_ref;
static u32 cw_a[11];
static void cw_tramp(void)
{
	defog_count_weight35abc(cw_a[0], cw_a[1], cw_a[2], cw_a[3], cw_a[4], cw_a[5], cw_a[6], cw_a[7], cw_a[8],
				cw_a[9], cw_a[10]);
}

static void cw_call(void)
{
	getcontext(&cw_ref);
	cw_ref.uc_stack.ss_sp = cw_stack;
	cw_ref.uc_stack.ss_size = sizeof(cw_stack);
	cw_ref.uc_link = &cw_main;
	makecontext(&cw_ref, cw_tramp, 0);
	swapcontext(&cw_main, &cw_ref);
}

static u32 rs = 0xdefc3a5u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

/* section image: thr24 at +0 (48 B), thr31 at +64 (62 B), curves at +144 + 48 * k (32 B each), canaries between */
/* scale factors g where a tail entry equals its clipped / walked upper neighbour (the < / <= boundary of the walk) */
#define NTIE 64
static u32 ties[NTIE], nties;
static int tie_at(const u16 *tab, unsigned int last, u32 max, u32 g, u32 sh)
{
	u32 t[64], j;

	for (j = 0; j <= last; j++)
		t[j] = (tab[j * (sh == 13 ? 2 : 1) + (sh == 13)] * g + (1u << (sh - 1))) >> sh & 0xffff;
	if (t[last] <= max)
		return 0;
	t[last] = max;
	for (j = last - 1; j > 0; j--) {
		if (t[j] == t[j + 1])
			return 1;
		if (t[j] < t[j + 1])
			return 0;
		t[j] = t[j + 1] - 1;
	}
	return 0;
}

static void find_ties(void)
{
	u32 g;

	for (g = 1; g < (1u << 20) && nties < NTIE; g++)
		if (tie_at(t32_data_bfa30, 23, 8191, g, 14) || tie_at(t32_data_bfa60, 30, 16383, g, 13))
			ties[nties++] = g;
}

#define SECN 400
#define O24 0
#define O31 64
#define OC(k) (144 + 48 * (k))

int main(void)
{
	unsigned int c, e, k, failed = 0, calls = 0, nzero = 0, grow = 0;
	unsigned int n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 600;
	u8 *obuf[NCH];
	u32 osize[NCH];

	find_ties();
	for (e = 0; e < n; e++) {
		unsigned int ncall = 1 + rnd() % 8;

		t32o_reset();
		memset(t32_data_e20e0, 0, sizeof(t32_data_e20e0));
		memset(t32_data_e211c, 0, sizeof(t32_data_e211c));
		memset(&lv, 0, sizeof(lv));
		memset(&lo, 0, sizeof(lo));
		for (k = 0; k < NCH; k++) {
			obuf[k] = NULL;
			osize[k] = 0;
		}
		for (c = 0; c < ncall; c++) {
			u32 ch = (rnd() & 3) | ((rnd() & 3) == 0 ? (rnd() & 0xff) << 8 : 0), a, b, gain, m = rnd() % 10;
			u8 *vs = t32o_alloc(SECN), os[SECN], *outp[5];
			unsigned int i;

			if (m == 0) { a = rnd() % 12; b = rnd() % 20; }
			else if (m == 1) { a = 20000 + rnd() % 40000; b = rnd() % 30; }
			else if (m == 2) { a = rnd() % 30; b = 20000 + rnd() % 40000; }
			else if (m == 3) {	/* a * b + 1012 = 2025 * q - 1: the rounding edge of the area factor */
				static const u32 ab[8][2] = { {4, 253}, {11, 92}, {23, 44}, {46, 22}, {2, 2531}, {19, 373},
							     {373, 19}, {92, 11} };
				k = rnd() % 8; a = ab[k][0]; b = ab[k][1];
			}
			else { a = rnd() % 2701; b = rnd() % 2001; }
			gain = (rnd() % 5 == 0) ? rnd() : (rnd() % 7 == 0 ? 255 : rnd() & 0xff);
			if (nties && rnd() % 6 == 0) {	/* area factor 1: g = gain >> 7 */
				a = 45; b = 45;
				gain = ties[rnd() % nties] << 7;
			}
			if ((a + 5) / 10 * ((b + 9) / 18) == 0)
				nzero++;
			for (i = 0; i < SECN; i++)
				vs[i] = os[i] = (u8)rnd();
			for (k = 0; k < 5; k++)
				outp[k] = os + OC(k);
			cw_a[0] = ch; cw_a[1] = a; cw_a[2] = b; cw_a[3] = gain;
			cw_a[4] = T32O_PTR(vs + O24); cw_a[5] = T32O_PTR(vs + O31);
			for (k = 0; k < 5; k++)
				cw_a[6 + k] = T32O_PTR(vs + OC(k));
			if (t32_data_e211c[ch & 0xff & 3] < 13 * ((a + 5) / 10 * ((b + 9) / 18)))
				grow++;
			cw_call();
			t32_defog_count_weight35abc(ch, a, b, gain, (u16 *)(os + O24), (u16 *)(os + O31), outp, obuf,
						    osize, &ops);
			calls++;
			k = ch & 0xff & 3;
			if (memcmp(vs, os, SECN) || osize[k] != t32_data_e211c[k] ||
			    (osize[k] && memcmp(obuf[k], T32O_HOST(t32_data_e20e0[k]), osize[k])) ||
			    lv.n != lo.n || memcmp(lv.k, lo.k, sizeof(lv.k)) || memcmp(lv.v, lo.v, sizeof(lv.v))) {
				if (failed++ < 8) {
					printf("MISMATCH ep %u call %u ch %x a %u b %u gain %u size %u/%u log %u/%u\n", e, c,
					       ch, a, b, gain, t32_data_e211c[k], osize[k], lv.n, lo.n);
					for (i = 0; i < SECN; i++)
						if (vs[i] != os[i]) {
							printf("  sec +%u vendor %02x ours %02x\n", i, vs[i], os[i]);
							break;
						}
				}
				break;
			}
		}
		for (k = 0; k < NCH; k++)
			free(obuf[k]);
	}
	printf("t32_defogcw_oracle: %u tie scales, %u episodes, %u calls (%u empty grids, %u grows) compared, %u mismatches\n", nties, n,
	       calls, nzero, grow, failed);
	return failed ? 1 : 0;
}
