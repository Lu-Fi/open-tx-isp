// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our defog ratio setter (driver/t32/tx_isp_t32_api_dfg.h)
 * against the vendor tisp_s_defog_ratio (with the reference tisp_get_tuning)
 * from Matt Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc).
 *
 * tisp_defog_strength_itp, tisp_defog_interp_trans_k and dfg_array_cmp are
 * recording stubs that write argument-hashed bytes into their outputs (the
 * interpolation leaves its output unchanged in about half of the cases, so
 * both results of the compare occur).  Compared: every call (order,
 * arguments, the 5 bytes of the vendor's stack copy), every byte of the
 * parameter sections and work blocks of all channels, the bank tunings and
 * the attribute block.
 *
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_api_dfg.h"

extern u32 t32_data_f0904[4], t32_data_e2130[4], t32_data_dd0b4[24];

static u32 rs = 0xdef09u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static u32 hash(u32 h, u32 a, u32 b)
{
	h ^= a * 0x9e3779b1u; h = (h << 7 | h >> 25) * 0x85ebca6bu;
	h ^= b * 0xc2b2ae35u; h ^= h >> 15;
	return h;
}

#define NREC 12
static struct { u32 fn, a[9]; u8 b[5]; } lg[NREC];
static unsigned int nlg;
static u32 salt;

static void rec(u32 fn, const u32 *a, unsigned int n, const u8 *b)
{
	unsigned int i;

	if (nlg < NREC) {
		memset(&lg[nlg], 0, sizeof(lg[nlg]));
		lg[nlg].fn = fn;
		for (i = 0; i < n; i++)
			lg[nlg].a[i] = a[i];
		if (b)
			memcpy(lg[nlg].b, b, 5);
	}
	nlg++;
}
u64 tisp_defog_strength_itp(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u32 a[3] = { a0, a1, a2 }, i;
	u8 *o = T32O_HOST(a2);

	(void)a3;
	rec(1, a, 3, 0);
	for (i = 0; i < 9; i++)
		o[i] = (u8)hash(salt, a0 + i, a1);
	return 0;
}
u64 tisp_defog_interp_trans_k(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6, u32 a7, u32 a8)
{
	u32 a[9] = { a0, a1, a2, a3, a4, a5, a6, a7, a8 }, i;
	u8 *o = T32O_HOST(a8);

	rec(2, a, 9, 0);
	if (hash(salt, a1, 7) & 1)
		for (i = 0; i < 5; i++)
			o[i] = (u8)hash(salt, a1 + i, a0);
	return 0;
}
u64 dfg_array_cmp(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u32 a[3] = { 0, a1, a2 };

	(void)a3;
	rec(3, a, 3, T32O_HOST(a0));
	return memcmp(T32O_HOST(a0), T32O_HOST(a1), 5) ? 1 + (hash(salt, a1, 3) & 7) : 0;
}

static void o_itp(u32 r, const u8 *in, u8 *out) { tisp_defog_strength_itp(r, T32O_PTR(in), T32O_PTR(out), 0); }
static void o_itk(u32 ch, u32 ev, const u8 *thr, const u8 *ta, const u8 *tb, const u8 *tc, const u8 *td,
		  const u8 *te, u8 *out)
{
	tisp_defog_interp_trans_k(ch, ev, T32O_PTR(thr), T32O_PTR(ta), T32O_PTR(tb), T32O_PTR(tc), T32O_PTR(td),
				  T32O_PTR(te), T32O_PTR(out));
}
static int o_cmp(const u8 *a, const u8 *b, u32 n)
{
	static u8 low[5];	/* ours keeps the copy on the host stack */

	memcpy(low, a, 5);
	return (int)dfg_array_cmp(T32O_PTR(low), T32O_PTR(b), n, 0);
}
static const struct t32_api_dfg_ops ops = { .strength_itp = o_itp, .interp_trans_k = o_itk, .array_cmp = o_cmp };

u64 tisp_s_defog_ratio(u32, u32, u32, u32);

#define PB	700
#define QB	3500
#define TB	(0x8466 + 64)
#define ATB	(4 * 128 + 1100)
static unsigned int failed, compared;

static void run(u32 ch, unsigned int c)
{
	static u8 p0[4][PB], q0[4][QB], t0[6][TB], at0[ATB], x0[4][32], st0[1808];
	static u8 vp[4][PB], vq[4][QB], vt[6][TB], vat[ATB];
	u8 *p[4], *q[4], *t[6], *x[4], *attr;
	unsigned int i, j, vn;
	u32 vl[NREC][10], w;
	u8 vb[NREC][5];

	t32o_reset();
	salt = rnd();
	attr = t32o_alloc(ATB);
	for (j = 0; j < 4; j++) {
		p[j] = t32o_alloc(PB); q[j] = t32o_alloc(QB); x[j] = t32o_alloc(32);
		for (i = 0; i < PB; i++) p0[j][i] = (u8)rnd();
		for (i = 0; i < QB; i++) q0[j][i] = (u8)rnd();
		for (i = 0; i < 32; i++) x0[j][i] = (u8)rnd();
		w = T32O_PTR(p[j]); memcpy(x0[j] + 12, &w, 4);
		w = T32O_PTR(q[j]); memcpy(x0[j] + 16, &w, 4);
		memcpy(x[j], x0[j], 32);
		t32_data_e2130[j] = T32O_PTR(x[j]);
		p0[j][590] = (c & 3) ? 0 : (u8)rnd();
	}
	for (j = 0; j < 6; j++) {
		t[j] = t32o_alloc(TB);
		for (i = 0; i < TB; i++) t0[j][i] = (u8)rnd();
	}
	for (i = 0; i < 24; i++)	/* bank pointers: 6 distinct tunings, shuffled per channel */
		t32_data_dd0b4[i] = T32O_PTR(t[(i * 5 + c) % 6]);
	for (i = 0; i < 1808; i++) st0[i] = (u8)rnd();
	for (j = 0; j < 2; j++)
		st0[j * 904 + 125] = (u8)(rnd() % 6);	/* bank >= 6 reads past the table (vendor), not compared */
	for (i = 0; i < ATB; i++) at0[i] = (u8)rnd();
	if (c & 12) {	/* slot mode 0, or only its top byte set */
		w = (c & 4) ? 0 : (rnd() | 1) << 24;
		memcpy(at0 + ch * 128 + 1092, &w, 4);
	}
	t32_data_f0904[0] = (c % 7 == 0) ? 0 : T32O_PTR(attr);

	for (j = 0; j < 4; j++) { memcpy(p[j], p0[j], PB); memcpy(q[j], q0[j], QB); }
	for (j = 0; j < 6; j++) memcpy(t[j], t0[j], TB);
	memcpy(attr, at0, ATB); memcpy(t32_data_dc284, st0, 1808);
	nlg = 0;
	t32o_call((t32o_fn)(void (*)(void))tisp_s_defog_ratio, ch, rnd(), rnd(), rnd(), 0);
	vn = nlg;
	for (i = 0; i < NREC; i++) {
		vl[i][0] = lg[i].fn;
		memcpy(vl[i] + 1, lg[i].a, 36);
		memcpy(vb[i], lg[i].b, 5);
	}
	for (j = 0; j < 4; j++) { memcpy(vp[j], p[j], PB); memcpy(vq[j], q[j], QB); }
	for (j = 0; j < 6; j++) memcpy(vt[j], t[j], TB);
	memcpy(vat, attr, ATB);

	for (j = 0; j < 4; j++) { memcpy(p[j], p0[j], PB); memcpy(q[j], q0[j], QB); }
	for (j = 0; j < 6; j++) memcpy(t[j], t0[j], TB);
	memcpy(attr, at0, ATB); memcpy(t32_data_dc284, st0, 1808);
	nlg = 0;
	{
		u32 ev;

		memcpy(&ev, x0[ch] + 24, 4);
		t32_s_defog_ratio(&ops, p[ch], q[ch], ev, t32_data_f0904[0] ? attr : 0,
				  T32O_HOST(t32_data_dd0b4[t32_defog_bank_index(t32_data_dc284, ch)]), ch);
	}
	compared++;
	if (vn != nlg || memcmp(vat, attr, ATB) || memcmp(t32_data_dc284, st0, 1808))
		goto bad;
	for (j = 0; j < 4; j++)
		if (memcmp(vp[j], p[j], PB) || memcmp(vq[j], q[j], QB) || memcmp(x[j], x0[j], 32))
			goto bad;
	for (j = 0; j < 6; j++)
		if (memcmp(vt[j], t[j], TB))
			goto bad;
	for (i = 0; i < vn && i < NREC; i++)
		if (vl[i][0] != lg[i].fn || memcmp(vl[i] + 1, lg[i].a, 36) || memcmp(vb[i], lg[i].b, 5))
			goto bad;
	return;
bad:
	if (failed++ < 8)
		printf("MISMATCH ch=%u c=%u: calls %u/%u\n", ch, c, vn, nlg);
}

int main(void)
{
	unsigned int c, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 4000;

	for (c = 0; c < n; c++)
		run(rnd() & 1, c);	/* the channel state dc284 has 2 channels */
	printf("t32_apidfg_oracle: %u cases compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
