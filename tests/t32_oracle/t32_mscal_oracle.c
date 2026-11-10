// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: MSCA day/night refresh (tx_isp_t32_msca_dn.h): dn_refresh.  Runs the unmodified
 * vendor functions (Matt Davis' recovered C, PR #15 @ 2e2e06bc) and our port
 * from the same random state and compares every register write (address,
 * value, order), the parameter block, the set block, the coefficient arrays,
 * the pending flag and the isp_printf count.  Each case also runs the vendor
 * with t32_data_d03a0 = 1: everything but the addresses must stay the same
 * (init_chx_cfg's writes are modelled in direct mode only) and the kick write
 * must be shadow index 13 of group <channel>.  Other channels' blocks are decoys
 * that must stay unchanged.  The vendor's return value is the kick write's
 * result and not compared.  Exit 1 on the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_msca_dn.h"

#define MAXW	64
#define PB	512
#define SB	(4 * 872 + 64)

extern u64 tisp_msca_dn_refresh(u32, u32, u32, u32);
u32 t32_data_e2740[8] __attribute__((weak));
u32 t32_data_e2758[2] __attribute__((weak));
u64 system_reg_write(u32 a0, u32 a1, u32 a2, u32 a3);

static u32 rs = 0x3c5a1e7u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

struct cs {
	u8 p[PB];
	u16 coef[T32_MSCA_COEFS];
	u8 buf[16];
	u8 sets[SB];

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

	for (i = 0; i < 6; i++)
		s->shadow[i] = rnd();
	s->scale = rnd();
	s->pend = (u8)(rnd() % 3);
	for (i = 0; i < 16; i++) s->buf[i] = (u8)rnd();
	s->buf[0] = (u8)(rnd() % 3 ? 1 : rnd() % 3);
	s->buf[1] = 0;
}


struct cap {
	struct t32_front_write w[MAXW];
	unsigned int n, printfs;
	u8 p[PB], sets[SB];
	u16 coef[T32_MSCA_COEFS];
	u8 pend;
};

/* tisp_reg_write: direct (d03a0 0) is system_reg_write, shadow mode logs 0x80000000 | group << 16 | index */
u64 tisp_reg_write(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a3;
	if (t32_data_d03a0[0] != 1)
		return system_reg_write(a1, a2, 0, 0);
	if (t32o_log.n < T32O_MAX_WRITES) {
		t32o_log.w[t32o_log.n].reg = 0x80000000u | (a0 & 0xff) << 16 | a1;
		t32o_log.w[t32o_log.n].val = a2;
		t32o_log.n++;
	}
	return 0;
}

static void ref_run(u32 chi, const struct cs *in, u8 mode, struct cap *r)
{
	u8 *pb, *sb, *cx, *dpb, *dsb, *dcx;
	unsigned int i;
	u32 pv;

	t32o_reset();
	pb = t32o_alloc(PB); sb = t32o_alloc(SB); cx = t32o_alloc(4 + 300 + 16);
	dpb = t32o_alloc(PB); dsb = t32o_alloc(SB); dcx = t32o_alloc(4 + 300 + 16);
	memcpy(pb, in->p, PB); memcpy(sb, in->sets, SB);
	pv = T32O_PTR(dpb);
	memcpy(cx, &pv, 4);
	t32_data_dd0a4[chi] = T32O_PTR(pb) - 0xef50;
	memcpy(cx + 4, in->coef, sizeof(in->coef));
	memcpy(dpb, pb, PB); memcpy(dsb, sb, SB); memcpy(dcx, cx, 4 + 300 + 16);
	pv = T32O_PTR(dpb);
	memcpy(dcx, &pv, 4);
	for (i = 0; i < 4; i++) {
		t32_data_e28ac[i] = T32O_PTR(i == chi ? cx : dcx);
		t32_data_e2768[i] = T32O_PTR(i == chi ? sb : dsb);
		t32_data_e2898[i] = 0x5a;
	}
	t32_data_d03a0[0] = mode;
	t32_data_e2898[chi] = in->pend;
	memcpy(t32_data_e2740, in->shadow, sizeof(in->shadow));
	t32_data_e2758[0] = in->scale;
	t32o_call((t32o_fn)(void (*)(void))tisp_msca_dn_refresh, chi, 0, 0, 0, 0);
	memcpy(&pv, cx, 4);
	if (pv != T32O_PTR(pb)) {
		printf("MISMATCH: vendor par pointer %08x\n", pv);
		exit(1);
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
	r->printfs = t32o_log.printfs;
	memcpy(r->p, pb, PB); memcpy(r->sets, sb, SB);
	memcpy(r->coef, cx + 4, sizeof(r->coef));
	r->pend = t32_data_e2898[chi];
	for (i = 0; i < 4; i++)
		if (i != chi && t32_data_e2898[i] != 0x5a) {
			printf("MISMATCH: vendor changed the pending flag of channel %u (ch %u)\n", i, chi);
			exit(1);
		}
	if (memcmp(dpb, in->p, PB) || memcmp(dsb, in->sets, SB) || memcmp(dcx + 4, in->coef, sizeof(in->coef))) {
		printf("MISMATCH: vendor changed another channel's blocks (ch %u)\n", chi);
		exit(1);
	}
}

static void port_run(const struct cs *in, struct cap *o)
{
	struct t32_msca_ch oc;
	static u8 decoy[PB];
	int n;


	memset(o, 0, sizeof(*o));
	memcpy(o->p, in->p, PB);
	memcpy(o->sets, in->sets, SB);
	memset(&oc, 0, sizeof(oc));
	memcpy(decoy, in->p, PB);
	oc.par = decoy;	/* dn_refresh must re-point it at o->p */
	memcpy(oc.coef, in->coef, sizeof(oc.coef));
	memcpy(oc.shadow, in->shadow, sizeof(oc.shadow));
	oc.scale_bits = in->scale;
	oc.pending = in->pend;
	n = t32_msca_dn_refresh(&oc, o->sets, T32O_PTR(o->p) - 0xef50, o->w);
	o->n = (unsigned int)n;
	o->printfs = 0;
	memcpy(o->coef, oc.coef, sizeof(o->coef));
	o->pend = oc.pending;
}

static int same_state(const char *what, const struct cap *a, const struct cap *b)
{
	if (memcmp(a->p, b->p, PB) || memcmp(a->sets, b->sets, SB) || memcmp(a->coef, b->coef, sizeof(a->coef)) ||
	    a->pend != b->pend || a->printfs != b->printfs || a->n != b->n) {
		printf("MISMATCH %s: state (writes %u/%u pend %u/%u printfs %u/%u)\n", what, a->n, b->n,
		       a->pend, b->pend, a->printfs, b->printfs);
		return 1;
	}
	return 0;
}

static unsigned int compared;

static int check(const char *what, u32 chi, const struct cs *in)
{
	static struct cap r0, r1, o0;
	unsigned int i;

	ref_run(chi, in, 0, &r0);
	ref_run(chi, in, 1, &r1);
	port_run(in, &o0);
	compared++;
	if (same_state(what, &r0, &o0) || same_state(what, &r1, &o0))
		return 1;
	for (i = 0; i < r0.n; i++)
		if (r0.w[i].reg != o0.w[i].reg || r0.w[i].val != o0.w[i].val) {
			printf("MISMATCH %s: write #%u vendor %05x=%08x ours %05x=%08x\n", what, i,
			       (unsigned int)r0.w[i].reg, (unsigned int)r0.w[i].val,
			       (unsigned int)o0.w[i].reg, (unsigned int)o0.w[i].val);
			return 1;
		}
	/* shadow mode: same values; our kick (high bit) exact, the others direct or shadow index */
	for (i = 0; i < r1.n; i++) {
		u32 x = r1.w[i].reg;

		if (r1.w[i].val != r0.w[i].val ||
		    (o0.w[i].reg & 0x80000000u ? x != o0.w[i].reg : (x != r0.w[i].reg && !(x & 0x80000000u)))) {
			printf("MISMATCH %s: shadow mode write #%u %08x=%08x / ours %08x\n", what, i, x,
			       (unsigned int)r1.w[i].val, (unsigned int)o0.w[i].reg);
			return 1;
		}
	}
	return 0;
}

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 3000;
	unsigned int c, i, kicks = 0;

	for (c = 0; c < n; c++) {
		static struct cs in;
		u32 chi = rnd() & 3;

		gen(&in);
		/* a zero input size makes the vendor's curve_calc pass ratio 0xffffffff (it effectively never returns): not tested */
		for (i = 0; i < 3; i++) {
			unsigned int o;

			for (o = 26; o <= 34; o += 2)
				if (!in.sets[i * 872 + o] && !in.sets[i * 872 + o + 1])
					in.sets[i * 872 + o] = 1;
		}
		if (check("dn_refresh", chi, &in))
			return 1;
		kicks++;
	}
	printf("t32_mscal_oracle: %u compared, 0 mismatches\n", compared);
	(void)kicks;
	return 0;
}
