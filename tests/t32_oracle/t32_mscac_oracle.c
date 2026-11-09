// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: MSCA per-frame paths.  Runs the unmodified vendor
 * functions (Matt Davis' recovered C, PR #15 @ 2e2e06bc) and our port in
 * driver/t32/tx_isp_t32_msca_curve.h from the same random state and compares
 * every register write (address, value, order), the parameter block, the set
 * block, the coefficient arrays, the shadow words (e2740), e2758, e2898 and
 * the u16 counter.  Exit 1 on the first mismatch.
 *
 * Direct register mode only (t32_data_d03a0 == 0).  t32_data_e2740 and
 * t32_data_e2758 are not declared by t32_oracle_env.h; they are defined weak
 * here so an env.c definition wins if it exists.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_msca_curve.h"

#define MAXW	512
#define PB	512
#define SB	(4 * 872 + 64)

extern u64 tisp_msca_para_calc(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_msca_init_chx_cfg(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_msca_ch_curve_write(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_msca_ch_curve_write_multi(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_msca_scaling_algorithm(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_msca_params_update(u32 a0, u32 a1, u32 a2, u32 a3);

u32 t32_data_e2740[8] __attribute__((weak));
u32 t32_data_e2758[2] __attribute__((weak));

static u32 rs = 0x3c5a1e7u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

struct cs {
	u8 p[PB];
	u16 coef[T32_MSCA_COEFS];
	u8 sets[SB];
	u16 cnt;
	u32 shadow[6];
	u32 scale;
	u8 pend;
};

static unsigned int compared;

static u8 flag(void)
{
	static const u8 f[] = { 0, 1, 1, 1, 2, 0x81 };

	return f[rnd() % 6];
}

static void put32(u8 *p, unsigned int o, u32 v)
{
	memcpy(p + o, &v, 4);
}

static void gen(struct cs *s)
{
	static const u16 mk[3] = { 0xffff, 0x3ff, 0xf };
	unsigned int i, k;
	u16 m = (rnd() & 1) ? mk[rnd() % 3] : 0xffff;

	for (i = 0; i < PB; i++)
		s->p[i] = (u8)rnd();
	for (i = 0; i < T32_MSCA_COEFS; i++)
		s->coef[i] = (u16)rnd();
	for (i = 0; i < SB; i++)
		s->sets[i] = (u8)rnd();
	for (i = 0; i + 1 < SB; i += 2) {
		u16 v;

		memcpy(&v, s->sets + i, 2);
		v &= m;
		memcpy(s->sets + i, &v, 2);
	}
	for (i = 172; i <= 174; i++)
		s->p[i] = flag();
	for (i = 181; i <= 186; i++)
		s->p[i] = flag();
	if (rnd() % 4 == 0) {
		for (i = 172; i <= 174; i++)
			s->p[i] = 1;
		for (i = 181; i <= 186; i++)
			s->p[i] = 1;
	}
	for (k = 0; k < 3; k++) {
		unsigned int b = 4 + 32 * k;
		u32 a = rnd(), e = rnd(), c = rnd(), d = rnd(), sp;

		switch (rnd() % 5) {
		case 0:
			break;
		case 1:
			e = a;
			break;
		case 2:
			sp = 1 + rnd() % 1000;
			e = a + sp + 65536 * (rnd() % 3);
			c = rnd() % 100000;
			d = c + rnd() % (sp + 1);
			break;
		case 3:
			sp = 1 + rnd() % 3;
			e = a + sp;
			c = rnd() % 100000 + 10;
			d = c - 1 - rnd() % 3;
			break;
		default:
			sp = 1 + rnd() % 300;
			e = a + sp;
			c = rnd();
			d = c + rnd() % (sp * 300);
			break;
		}
		put32(s->p, b, a);
		put32(s->p, b + 4, e);
		put32(s->p, b + 8, c);
		put32(s->p, b + 12, d);
	}
	s->cnt = (u16)rnd();
	for (i = 0; i < 6; i++)
		s->shadow[i] = rnd();
	s->scale = rnd();
	s->pend = (u8)(rnd() % 3);
}

static int check(const char *what, int which, u32 chi, u32 arg0, u32 x, const struct cs *in)
{
	u8 *pb, *sb, *cx, *cp;
	u8 rp[PB], rsets[SB];
	u16 rcoef[T32_MSCA_COEFS], rcnt;
	u32 rshadow[6], rscale;
	u8 rpend;
	struct t32_front_write rw[MAXW], ow[MAXW];
	unsigned int rn, i;
	int on = 0;
	struct t32_msca_ch oc;
	u8 op[PB], os[SB];
	u16 ocnt;
	u32 pv;

	t32o_reset();
	pb = t32o_alloc(PB);
	sb = t32o_alloc(SB);
	cx = t32o_alloc(4 + 300 + 16);
	cp = t32o_alloc(8);
	if (!pb || !sb || !cx || !cp) {
		fprintf(stderr, "alloc failed\n");
		exit(2);
	}
	memcpy(pb, in->p, PB);
	memcpy(sb, in->sets, SB);
	pv = T32O_PTR(pb);
	memcpy(cx, &pv, 4);
	memcpy(cx + 4, in->coef, sizeof(in->coef));
	memcpy(cp, &in->cnt, 2);
	t32_data_e28ac[chi] = T32O_PTR(cx);
	t32_data_e2768[chi] = T32O_PTR(sb);
	t32_data_d03a0[0] = 0;
	t32_data_e2898[chi] = in->pend;
	memcpy(t32_data_e2740, in->shadow, sizeof(in->shadow));
	t32_data_e2758[0] = in->scale;

	switch (which) {
	case 0:
		t32o_call((t32o_fn)(void (*)(void))tisp_msca_para_calc, arg0, x, 0, 0, 0);
		break;
	case 1:
		t32o_call((t32o_fn)(void (*)(void))tisp_msca_init_chx_cfg, arg0, x, 0, 0, 0);
		break;
	case 2:
		t32o_call((t32o_fn)(void (*)(void))tisp_msca_ch_curve_write, chi, T32O_PTR(cp), 0, 0, 0);
		break;
	case 3:
		t32o_call((t32o_fn)(void (*)(void))tisp_msca_ch_curve_write_multi, chi, 0, 0, 0, 0);
		break;
	case 4:
		t32o_call((t32o_fn)(void (*)(void))tisp_msca_scaling_algorithm, chi, 0, 0, 0, 0);
		break;
	default:
		t32o_call((t32o_fn)(void (*)(void))tisp_msca_params_update, chi, 0, 0, 0, 0);
		break;
	}
	rn = t32o_log.n;
	if (rn > MAXW) {
		fprintf(stderr, "ref wrote %u > MAXW\n", rn);
		exit(2);
	}
	for (i = 0; i < rn; i++) {
		rw[i].reg = t32o_log.w[i].reg;
		rw[i].val = t32o_log.w[i].val;
	}
	memcpy(rp, pb, PB);
	memcpy(rsets, sb, SB);
	memcpy(rcoef, cx + 4, sizeof(rcoef));
	memcpy(&rcnt, cp, 2);
	memcpy(rshadow, t32_data_e2740, sizeof(rshadow));
	rscale = t32_data_e2758[0];
	rpend = t32_data_e2898[chi];

	memcpy(op, in->p, PB);
	memcpy(os, in->sets, SB);
	ocnt = in->cnt;
	memset(&oc, 0, sizeof(oc));
	oc.par = op;
	memcpy(oc.coef, in->coef, sizeof(oc.coef));
	memcpy(oc.shadow, in->shadow, sizeof(oc.shadow));
	oc.scale_bits = in->scale;
	oc.pending = in->pend;
	switch (which) {
	case 0:
		t32_msca_para_calc(&oc, os, x);
		break;
	case 1:
		on = t32_msca_init_chx_cfg(&oc, os, x, ow);
		break;
	case 2:
		on = t32_msca_ch_curve_write(&oc, &ocnt, ow);
		break;
	case 3:
		on = t32_msca_ch_curve_write_multi(&oc, ow);
		break;
	case 4:
		on = t32_msca_scaling_algorithm(&oc, ow);
		break;
	default:
		on = t32_msca_params_update(&oc, os, ow);
		break;
	}

	compared++;
	if (on < 0 || (unsigned int)on != rn) {
		printf("MISMATCH %s: writes ref %u ours %d\n", what, rn, on);
		return 1;
	}
	for (i = 0; i < rn; i++)
		if (rw[i].reg != ow[i].reg || rw[i].val != ow[i].val) {
			printf("MISMATCH %s: write #%u vendor %05x=%08x ours %05x=%08x\n", what, i,
			       (unsigned int)rw[i].reg, (unsigned int)rw[i].val,
			       (unsigned int)ow[i].reg, (unsigned int)ow[i].val);
			return 1;
		}
	if (memcmp(rp, op, PB)) {
		printf("MISMATCH %s: parameter block\n", what);
		return 1;
	}
	if (memcmp(rsets, os, SB)) {
		printf("MISMATCH %s: set block\n", what);
		return 1;
	}
	if (memcmp(rcoef, oc.coef, sizeof(rcoef))) {
		printf("MISMATCH %s: coefficients\n", what);
		return 1;
	}
	if (rcnt != ocnt || rpend != oc.pending || rscale != oc.scale_bits ||
	    memcmp(rshadow, oc.shadow, sizeof(rshadow))) {
		printf("MISMATCH %s: state cnt %04x/%04x pend %u/%u scale %08x/%08x\n", what,
		       rcnt, ocnt, rpend, oc.pending, rscale, oc.scale_bits);
		return 1;
	}
	return 0;
}

int main(void)
{
	static const char *const names[6] = {
		"para_calc", "init_chx_cfg", "ch_curve_write",
		"ch_curve_write_multi", "scaling_algorithm", "params_update"
	};
	unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 4000;
	unsigned int c;
	int which;
	static struct cs st;

	for (c = 0; c < n; c++) {
		for (which = 0; which < 6; which++) {
			u32 chi = rnd() & 3, arg0 = chi, x = 0;

			gen(&st);
			if (which == 0) {
				arg0 = chi | (rnd() & ~0xffu);
				x = (rnd() & ~0xffu) | (rnd() % 4);
			} else if (which == 1) {
				arg0 = chi | (rnd() & ~0xffu);
				x = (rnd() & ~0xffu) | (rnd() % 5);
			}
			if (check(names[which], which, chi, arg0, x, &st))
				return 1;
		}
	}
	printf("t32_mscac_oracle: %u compared, 0 mismatches\n", compared);
	return 0;
}
