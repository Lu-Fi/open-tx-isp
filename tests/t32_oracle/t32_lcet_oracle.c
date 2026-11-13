// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: LCE per-frame helpers.  Runs the unmodified vendor
 * functions (Matt Davis' recovered C, PR #15 @ 2e2e06bc) and our readable
 * ports from driver/t32/tx_isp_t32_lce.h, comparing every output byte.
 *
 * Covered: lce_hist_method, lce_hist_filter_and_judge_16bit,
 * lce_hist_filter_and_judge, lce_pdf_to_cdf, lce_self_light_correct_local,
 * lce_16bit_data_converge.
 *
 * Every buffer is a guarded block (64 canary bytes each side, 0xa5) that is compared WHOLE between the
 * vendor and our run; input buffers (histograms, parameter block, enable / gain tables) must come back
 * unchanged on both sides; the vendor's return value is checked where our port has an equivalent (the
 * hfj functions return the found bin in the low word, the other functions return register leftovers).
 *
 * lce_hist_method reads t32_data_bf8a0 (128 bytes, only copied over a buffer that is
 * rewritten), bf9a8 (1024/n table, read unchecked past its 32 entries for n >= 32: env.c holds the
 * rodata that follows), d6c66 and d6c67 (shift bytes, varied per case here: both are plain bytes
 * of writable objects).  They are given here as weak definitions in case env.c does not define them;
 * env.c's objects win when present.  n is generated up to 46: from 47 on the vendor's ramps overwrite its saved output
 * pointer (frame word 147) and the reference crashes in its own final memcpy.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_lce.h"

u8 oc_bf8a0[128] __asm__("t32_data_bf8a0") __attribute__((weak));
u16 oc_bf9a8[32] __asm__("t32_data_bf9a8") __attribute__((weak)) = {
	0x400, 0x400, 0x200, 0x155, 0x100, 0x0cd, 0x0ab, 0x092, 0x080, 0x072, 0x066, 0x05d, 0x055, 0x04f, 0x049, 0x044,
	0x040, 0x03c, 0x039, 0x036, 0x033, 0x031, 0x02f, 0x02d, 0x02b, 0x029, 0x027, 0x026, 0x025, 0x023, 0x022, 0x021,
};
u8 oc_d6c66 __asm__("t32_data_d6c66") __attribute__((weak)) = 20;
u8 oc_d6c67 __asm__("t32_data_d6c67") __attribute__((weak)) = 20;

/* unmodified vendor functions from ref/ */
extern u64 lce_hist_method(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 lce_hist_filter_and_judge_16bit(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 lce_hist_filter_and_judge(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 lce_pdf_to_cdf(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 lce_self_light_correct_local(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4);
extern u64 lce_16bit_data_converge(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4);

static u32 rs = 0x51ce7bu;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static unsigned int compared, failed;
static unsigned int cov_hm_neg, cov_hm_pos, cov_hm_big_n, cov_hm_v0, cov_hm_vm1, cov_hm_vneg, cov_hm_b0, cov_hfj_nf, cov_hfj_31,
		    cov_hfj_wrap;

static void fail(const char *fn, const char *what, unsigned int a, unsigned int b)
{
	if (failed++ < 8)
		printf("MISMATCH %s: %s (%x / %x)\n", fn, what, a, b);
}

/* guarded block: vendor copy in the oracle pool, our copy local, same initial image */
#define CAN 64
#define GMAX 512
struct gb {
	u8 *r;
	u32 sz, tot;
	u8 o[GMAX] __attribute__((aligned(64)));
	u8 img[GMAX];
};

static void gb_init(struct gb *g, u32 sz, const void *data)
{
	u32 i;

	g->sz = sz;
	g->tot = sz + 2 * CAN;
	memset(g->img, 0xa5, g->tot);
	for (i = 0; i < sz; i++)
		g->img[CAN + i] = (u8)rnd();	/* unwritten bytes must survive, so no zero fill */
	if (data)
		memcpy(g->img + CAN, data, sz);
	g->r = t32o_alloc(g->tot);
	memcpy(g->r, g->img, g->tot);
	memcpy(g->o, g->img, g->tot);
}
#define RP(g) ((g)->r + CAN)
#define OP(g) ((g)->o + CAN)

/* output block: vendor == ours over the whole block incl. canaries, canaries still intact */
static int gb_out(const struct gb *g, const char *fn)
{
	unsigned int i;
	int bad = 0;

	for (i = 0; i < g->tot; i++)
		if (g->r[i] != g->o[i]) {
			fail(fn, "output byte", i < CAN ? i : i - CAN, g->r[i] << 8 | g->o[i]);
			bad = 1;
			break;
		}
	if (memcmp(g->r, g->img, CAN) || memcmp(g->r + CAN + g->sz, g->img + CAN + g->sz, CAN)) {
		fail(fn, "vendor wrote outside its block", 0, 0);
		bad = 1;
	}
	if (memcmp(g->o, g->img, CAN) || memcmp(g->o + CAN + g->sz, g->img + CAN + g->sz, CAN)) {
		fail(fn, "port wrote outside its block", 0, 0);
		bad = 1;
	}
	return bad;
}

/* input block: both sides must leave it untouched */
static int gb_in(const struct gb *g, const char *fn)
{
	int bad = 0;

	if (memcmp(g->r, g->img, g->tot)) {
		fail(fn, "vendor modified an input", 0, 0);
		bad = 1;
	}
	if (memcmp(g->o, g->img, g->tot)) {
		fail(fn, "port modified an input", 0, 0);
		bad = 1;
	}
	return bad;
}

static int run_hfj16(const u16 *hist)
{
	struct gb h, o;
	u64 ret;
	int bad;

	t32o_reset();
	gb_init(&h, 64, hist);
	gb_init(&o, 4, NULL);
	ret = t32o_call((t32o_fn)(void (*)(void))lce_hist_filter_and_judge_16bit, T32O_PTR(RP(&h)), 0, 0,
			T32O_PTR(RP(&o)), 0);
	t32_lce_hist_filter_and_judge_16bit((const u16 *)OP(&h), OP(&o));
	compared++;
	bad = gb_out(&o, "hfj16") | gb_in(&h, "hfj16");
	if ((u32)ret != *RP(&o)) {
		fail("hfj16", "return value low word != stored bin", (u32)ret, *RP(&o));
		bad = 1;
	}
	if (!*RP(&o))
		cov_hfj_nf++;
	return bad;
}

static int run_hfj(const u32 *hist)
{
	struct gb h, o;
	u64 ret;
	int bad;

	t32o_reset();
	gb_init(&h, 128, hist);
	gb_init(&o, 4, NULL);
	ret = t32o_call((t32o_fn)(void (*)(void))lce_hist_filter_and_judge, T32O_PTR(RP(&h)), 0, 0,
			T32O_PTR(RP(&o)), 0);
	t32_lce_hist_filter_and_judge((const u32 *)OP(&h), OP(&o));
	compared++;
	bad = gb_out(&o, "hfj") | gb_in(&h, "hfj");
	if ((u32)ret != *RP(&o)) {
		fail("hfj", "return value low word != stored bin", (u32)ret, *RP(&o));
		bad = 1;
	}
	if (*RP(&o) == 31)
		cov_hfj_31++;
	return bad;
}

static int run_pdf(const u32 *pdf)
{
	struct gb p, o;
	int bad;

	t32o_reset();
	gb_init(&p, 128, pdf);
	gb_init(&o, 32, NULL);	/* the vendor writes 20 bytes; bytes 20..31 and the canaries must stay */
	t32o_call((t32o_fn)(void (*)(void))lce_pdf_to_cdf, T32O_PTR(RP(&p)), T32O_PTR(RP(&o)), 0, 0, 0);
	t32_lce_pdf_to_cdf_raw((const u32 *)OP(&p), OP(&o));
	compared++;
	bad = gb_out(&o, "pdf_to_cdf") | gb_in(&p, "pdf_to_cdf");
	return bad;
}

static int run_slc(const u8 *in, const u8 *en, const u8 *up, const u8 *down)
{
	struct gb i, e, u, d, o;
	int bad;

	t32o_reset();
	gb_init(&i, 20, in);
	gb_init(&e, 16, en);
	gb_init(&u, 20, up);
	gb_init(&d, 20, down);
	gb_init(&o, 32, NULL);
	t32o_call((t32o_fn)(void (*)(void))lce_self_light_correct_local, T32O_PTR(RP(&i)), T32O_PTR(RP(&o)),
		  T32O_PTR(RP(&e)), T32O_PTR(RP(&u)), T32O_PTR(RP(&d)));
	t32_lce_self_light_correct_local(OP(&i), OP(&o), OP(&e), OP(&u), OP(&d));
	compared++;
	bad = gb_out(&o, "slc");
	bad |= gb_in(&i, "slc") | gb_in(&e, "slc") | gb_in(&u, "slc") | gb_in(&d, "slc");
	return bad;
}

static int run_conv(u32 gain, u32 lo, u32 hi, const u8 *target, const u8 *cur)
{
	struct gb t, c;
	int bad;

	t32o_reset();
	gb_init(&t, 20, target);
	gb_init(&c, 20, cur);
	t32o_call((t32o_fn)(void (*)(void))lce_16bit_data_converge, gain, lo, hi, T32O_PTR(RP(&t)),
		  T32O_PTR(RP(&c)));
	t32_lce_16bit_data_converge(gain, lo, hi, OP(&t), OP(&c));
	compared++;
	bad = gb_out(&c, "converge") | gb_in(&t, "converge");
	return bad;
}

static int run_hm(const u32 *hist, const struct t32_lce_hm16_par *p, u32 shb, u32 sha)
{
	struct gb h, o, pb;
	unsigned char pimg[64];
	int bad;

	memset(pimg, 0, sizeof(pimg));
	memcpy(pimg, p, sizeof(*p));
	t32o_reset();
	gb_init(&h, 128, hist);
	gb_init(&o, 128, NULL);
	gb_init(&pb, 64, pimg);	/* words the vendor does not read stay as the random image bytes of the caller */
	oc_d6c66 = (u8)shb;
	oc_d6c67 = (u8)sha;
	t32o_call((t32o_fn)(void (*)(void))lce_hist_method, T32O_PTR(RP(&h)), T32O_PTR(RP(&o)), T32O_PTR(RP(&pb)),
		  0, 0);
	t32_lce_hist_method32((const u32 *)OP(&h), (u32 *)OP(&o), (const struct t32_lce_hm16_par *)OP(&pb),
			      (u8)shb, (u8)sha);
	compared++;
	bad = gb_out(&o, "hist_method") | gb_in(&h, "hist_method") | gb_in(&pb, "hist_method");
	if (bad && failed <= 8)
		printf("  n %u shb %u sha %u\n", p->n & 0xff, shb & 0xff, sha & 0xff);
	return bad;
}

static u32 rbits(void)
{
	u32 sc = 1 + rnd() % 16;

	return rnd() >> (32 - sc);
}

static u32 recip_a(u32 n)
{
	return t32_lce_hm_recip[n];
}

/* the a ramp of lce_hist_method for n >= 32 (needed only to aim the directed over-full case) */
static void aim_overfull(u32 *hist, struct t32_lce_hm16_par *p, u32 *sha, int mode)
{
	u32 sa, a[32], sum = 0, tgt, cap = 4400000u, i;

	memset(p, 0, sizeof(*p));
	p->n = 34 | (rnd() & 0xffffff00u);
	p->a = 0xffff | (rnd() & 0xffff0000u);
	p->b = rnd();
	p->c3 = rnd();
	p->k8 = 128 | (rnd() & 0xffffff00u);
	p->c1 = rnd() % 4096;
	p->e1 = rnd();
	p->e2 = rnd();
	p->sh = rnd();
	sa = ((p->a & 0xffff) * recip_a(34)) >> 10;
	for (i = 0; i < 32; i++) {
		a[i] = (p->c1 & 0xffff) + sa * i;
		hist[i] = a[i] < cap ? a[i] : cap;
		sum += hist[i];
	}

	/* sum<<5>>14 == -1 (v == 0) for sum = 0x07fffe00..0x07ffffff, == -2 (v == -1) for 0x07fffc00..0x07fffdff,
	 * mode 2: any other negative v (sum 0x04000000..0x06ffffff) */
	if (mode == 2) {
		tgt = 0x04000000u + rnd() % 0x03000000u;
		cap = tgt / 32;
	} else {
		tgt = (mode == 1 ? 0x07fffc00u : 0x07fffe00u) + rnd() % 512;
	}
	hist[31] += tgt - sum;
	*sha = mode == 1 ? 31 : rnd();
}

int main(void)
{
	const unsigned int n = getenv("T32O_NRAND") ?
		(unsigned int)atoi(getenv("T32O_NRAND")) : 6000;
	unsigned int c, i;

	for (c = 0; c < n; c++) {
		u16 h16[32];
		u32 h32[32], hh[32];
		u8 in[20], en[16], up[20], down[20], target[20], cur[20];
		struct t32_lce_hm16_par p;
		u32 mode = rnd() % 4, lo, hi, shb, sha;

		for (i = 0; i < 32; i++) {
			if (mode == 0) {
				h16[i] = (u16)rnd();
				h32[i] = rnd();
			} else {
				h16[i] = (u16)(rbits() & 0xfff);
				h32[i] = rbits();
			}
		}
		if (c % 5 == 0) {
			/* running sum at 16383 / 16384 / 16385 at a random bin, then more bins (stop or continue) */
			u32 k1 = rnd() % 32, k2 = k1 + (k1 < 31 ? 1 + rnd() % (31 - k1) : 0), v = 16383 + rnd() % 3;

			memset(h16, 0, sizeof(h16));
			memset(h32, 0, sizeof(h32));
			h16[k1] = (u16)v;
			h32[k1] = v;
			if (rnd() & 1) {
				h16[k2] = (u16)(rnd() % 3);
				h32[k2] = rnd() % 3;
			}
		} else if (c % 5 == 1) {
			/* stop exactly at bin 31 / never stop / stop at a random bin by a split sum */
			u32 t = (rnd() & 1) ? 31 : rnd() % 32, tot = 16383 + rnd() % 3, k;

			memset(h16, 0, sizeof(h16));
			memset(h32, 0, sizeof(h32));
			for (k = 0; k < t; k++)
				h16[k] = h32[k] = (u16)(rnd() % ((tot / (t + 1)) + 1));
			for (k = 0; k < t; k++)
				tot -= h32[k];
			h16[t] = (u16)tot;
			h32[t] = tot;
			if (rnd() % 4 == 0)
				h16[t] = 0, h32[t] = 0;	/* never reaches 16385 (or only the last bin) */
		} else if (c % 5 == 2) {
			/* u32 sum wraps back below 16385 in one addition (the 32 bit variant only) */
			u32 s = rnd() % 16385, r = rnd() % (s + 1), k1 = rnd() % 31;

			memset(h32, 0, sizeof(h32));
			h32[k1] = s;
			h32[k1 + 1] = (u32)0 - r;
			cov_hfj_wrap++;
		}
		run_hfj16(h16);
		run_hfj(h32);
		if (c % 7 == 0) {
			/* pdf sums right at the 32767 / 32768 clamp, in either of the two loops */
			memset(h32, 0, sizeof(h32));
			h32[rnd() % 32] = 32766 + rnd() % 4;
			if (rnd() & 1)
				h32[rnd() % 32] += rnd() % 3;
		} else if (c % 7 == 1) {
			/* huge bins: 32 bit wrap of the running sum */
			for (i = 0; i < 32; i++)
				h32[i] = (rnd() & 1) ? 0xffffffffu - rnd() % 4 : rnd() % 3;
		}
		run_pdf(h32);

		for (i = 0; i < 20; i++) {
			in[i] = (u8)rnd();
			up[i] = (u8)rnd();
			down[i] = (u8)rnd();
			target[i] = (u8)rnd();
			cur[i] = (u8)rnd();
		}
		for (i = 0; i < 16; i++)
			en[i] = (u8)(rnd() % 3 == 0 ? 0 : rnd());	/* any non-zero value enables */
		if (c % 3 == 0)
			for (i = 0; i < 20; i++) {
				u32 r = rnd() % 4;

				in[i] = (u8)(t32_lce_slc_ofs[i] + (r == 0 ? 0 : r == 1 ? 1 : r == 2 ? -1 : (int)(rnd() % 5) - 2));
			}
		if (c % 11 == 0)
			en[8] = en[12] = 0;
		run_slc(in, en, up, down);

		lo = rnd();
		hi = rnd();
		if (c & 1)
			for (i = 0; i < 20; i++) {
				u32 r = rnd() % 5;

				if (r == 0)
					cur[i] = (u8)(target[i] + (lo & 0xff));
				else if (r == 1)
					target[i] = (u8)(cur[i] + (lo & 0xff));
				else if (r == 2)
					cur[i] = (u8)(target[i] + (hi & 0xff));
				else if (r == 3)
					cur[i] = target[i];
			}
		run_conv(rnd(), lo, hi, target, cur);

		/* lce_hist_method: n 0..46 (n >= 32 reads the rodata after the 1024/n table), shift bytes varied */
		shb = (rnd() & 3) ? 20 + rnd() % 8 : rnd();
		sha = (rnd() & 3) ? 20 + rnd() % 8 : rnd();
		memset(&p, 0, sizeof(p));
		{
			u32 r = rnd() % 8, nn = r < 4 ? rnd() % 32 : r < 7 ? 32 + rnd() % 15 : (rnd() & 1) * 31;

			p.n = nn | (rnd() & 0xffffff00u);
		}
		p.r0 = rnd();
		p.r2[0] = rnd(); p.r2[1] = rnd(); p.r2[2] = rnd();
		p.c3 = rnd();
		p.b = rnd();
		p.k8 = (rnd() % 4 == 0 ? 128 : rnd() % 256) | (rnd() & 0xffffff00u);
		p.c1 = rnd() % (1U << (4 + rnd() % 13));
		p.a = (rnd() % 4 == 0 ? rnd() % 4096 : rnd() % (1U << (1 + rnd() % 16))) | (rnd() & 0xffff0000u);
		p.e1 = rnd() % 4096 | (rnd() & 0xffff0000u);
		p.e2 = rnd() % 4096 | (rnd() & 0xffff0000u);
		p.sh = rnd();
		if (rnd() % 16 == 0) {
			p.c3 = p.b = p.e2 = 0;	/* sum(b) == 0 when also the ramp is flat: the divide by zero */
			cov_hm_b0++;
		}
		for (i = 0; i < 32; i++)
			hh[i] = (rnd() % 8 == 0) ? rnd() : rnd() % (1U << (6 + rnd() % 9));
		if (c % 16 == 3) {
			aim_overfull(hh, &p, &sha, 0);
			cov_hm_v0++;
		} else if (c % 16 == 4) {
			aim_overfull(hh, &p, &sha, 1);
			cov_hm_vm1++;
		} else if (c % 16 == 5) {
			aim_overfull(hh, &p, &sha, 2);
			cov_hm_vneg++;
		}
		if ((p.n & 0xff) >= 32)
			cov_hm_big_n++;
		run_hm(hh, &p, shb, sha);
	}

	/* every n 0..46 with every shift byte class: one sweep with fixed ramps (rem >= 0 and < 0) */
	for (i = 0; i < 47 * 4; i++) {
		struct t32_lce_hm16_par p;
		u32 hh[32], k;

		memset(&p, 0, sizeof(p));
		p.n = i / 4;
		p.c3 = 17; p.b = 200; p.k8 = 128; p.c1 = 900; p.a = 3000; p.e1 = 1500; p.e2 = 40; p.sh = 9 + (i & 3) * 7;
		for (k = 0; k < 32; k++)
			hh[k] = (i & 1) ? 100 + 31 * k : 3000 + 40 * k;	/* low: rem >= 0, high: rem < 0 */
		run_hm(hh, &p, 20, 20);
		cov_hm_pos += (i & 1);
		cov_hm_neg += !(i & 1);
	}

	printf("coverage: hfj stop at 31 %u, hfj not found/0 %u, hfj wrap cases %u, hm n>=32 %u, hm sweep rem>=0/<0 %u/%u, "
	       "hm sum(b)=0 %u, hm v==0 %u, hm v==-1 %u, hm v<-1 %u\n", cov_hfj_31, cov_hfj_nf, cov_hfj_wrap, cov_hm_big_n,
	       cov_hm_pos, cov_hm_neg, cov_hm_b0, cov_hm_v0, cov_hm_vm1, cov_hm_vneg);
	printf("t32_lcet_oracle: random %u compared, %u mismatches\n", compared, failed);
	if (failed)
		return 1;
	return 0;
}
