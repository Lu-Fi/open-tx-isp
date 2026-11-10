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
 * lce_hist_method reads t32_data_bf8a0 (128 bytes, only copied over a buffer that is
 * rewritten), bf9a8 (1024/n table), d6c66 and d6c67 (shift bytes).  They are given here as weak
 * definitions in case env.c does not define them; env.c's objects win when present, and the
 * shift bytes are read from whichever object is linked.
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

static int run_hfj16(const u16 *hist)
{
	u8 *hbuf, *obuf, vref, vour = 0xaa;

	t32o_reset();
	hbuf = t32o_alloc(64);
	obuf = t32o_alloc(4);
	memcpy(hbuf, hist, 64);
	t32o_call((t32o_fn)(void (*)(void))lce_hist_filter_and_judge_16bit, T32O_PTR(hbuf), 0, 0,
		  T32O_PTR(obuf), 0);
	vref = *obuf;
	t32_lce_hist_filter_and_judge_16bit(hist, &vour);
	compared++;
	if (vref != vour) {
		if (failed++ < 5)
			printf("MISMATCH hfj16: vendor %02x ours %02x\n", vref, vour);
		return 1;
	}
	return 0;
}

static int run_hfj(const u32 *hist)
{
	u8 *hbuf, *obuf, vref, vour = 0xaa;

	t32o_reset();
	hbuf = t32o_alloc(128);
	obuf = t32o_alloc(4);
	memcpy(hbuf, hist, 128);
	t32o_call((t32o_fn)(void (*)(void))lce_hist_filter_and_judge, T32O_PTR(hbuf), 0, 0,
		  T32O_PTR(obuf), 0);
	vref = *obuf;
	t32_lce_hist_filter_and_judge(hist, &vour);
	compared++;
	if (vref != vour) {
		if (failed++ < 5)
			printf("MISMATCH hfj: vendor %02x ours %02x\n", vref, vour);
		return 1;
	}
	return 0;
}

static int run_pdf(const u32 *pdf)
{
	u8 *pbuf, *obuf, oref[20], oour[20];

	memset(oref, 0xaa, sizeof(oref));
	memset(oour, 0xaa, sizeof(oour));
	t32o_reset();
	pbuf = t32o_alloc(128);
	obuf = t32o_alloc(32);
	memset(obuf, 0xaa, 32);
	memcpy(pbuf, pdf, 128);
	t32o_call((t32o_fn)(void (*)(void))lce_pdf_to_cdf, T32O_PTR(pbuf), T32O_PTR(obuf), 0, 0, 0);
	memcpy(oref, obuf, 20);
	t32_lce_pdf_to_cdf_raw(pdf, oour);
	compared++;
	if (memcmp(oref, oour, 20)) {
		if (failed++ < 5)
			printf("MISMATCH pdf_to_cdf\n");
		return 1;
	}
	return 0;
}

static int run_slc(const u8 *in, const u8 *en, const u8 *up, const u8 *down)
{
	u8 *ibuf, *ebuf, *ubuf, *dbuf, *obuf, oref[20], oour[20];

	memset(oref, 0xaa, sizeof(oref));
	memset(oour, 0xaa, sizeof(oour));
	t32o_reset();
	ibuf = t32o_alloc(32);
	ebuf = t32o_alloc(32);
	ubuf = t32o_alloc(32);
	dbuf = t32o_alloc(32);
	obuf = t32o_alloc(32);
	memcpy(ibuf, in, 20);
	memcpy(ebuf, en, 16);
	memcpy(ubuf, up, 20);
	memcpy(dbuf, down, 20);
	t32o_call((t32o_fn)(void (*)(void))lce_self_light_correct_local, T32O_PTR(ibuf), T32O_PTR(obuf),
		  T32O_PTR(ebuf), T32O_PTR(ubuf), T32O_PTR(dbuf));
	memcpy(oref, obuf, 20);
	t32_lce_self_light_correct_local(in, oour, en, up, down);
	compared++;
	if (memcmp(oref, oour, 20)) {
		if (failed++ < 5)
			printf("MISMATCH slc\n");
		return 1;
	}
	return 0;
}

static int run_conv(u32 gain, u32 lo, u32 hi, const u8 *target, const u8 *cur)
{
	u8 *tbuf, *cbuf, cref[20], cour[20];

	memcpy(cour, cur, 20);
	t32o_reset();
	tbuf = t32o_alloc(32);
	cbuf = t32o_alloc(32);
	memcpy(tbuf, target, 20);
	memcpy(cbuf, cur, 20);
	t32o_call((t32o_fn)(void (*)(void))lce_16bit_data_converge, gain, lo, hi, T32O_PTR(tbuf),
		  T32O_PTR(cbuf));
	memcpy(cref, cbuf, 20);
	t32_lce_16bit_data_converge(gain, lo, hi, target, cour);
	compared++;
	if (memcmp(cref, cour, 20)) {
		if (failed++ < 5)
			printf("MISMATCH converge gain %08x lo %02x hi %02x\n", gain, lo, hi);
		return 1;
	}
	return 0;
}

static int run_hm(const u32 *hist, const struct t32_lce_hm16_par *p)
{
	u8 *hbuf, *obuf, *pbuf;
	u32 oref[32], oour[32];
	unsigned int i;

	t32o_reset();
	hbuf = t32o_alloc(128);
	obuf = t32o_alloc(128);
	pbuf = t32o_alloc(64);
	memcpy(hbuf, hist, 128);
	memcpy(pbuf, p, sizeof(*p));
	t32o_call((t32o_fn)(void (*)(void))lce_hist_method, T32O_PTR(hbuf), T32O_PTR(obuf), T32O_PTR(pbuf), 0, 0);
	memcpy(oref, obuf, 128);
	t32_lce_hist_method32(hist, oour, p, oc_d6c66, oc_d6c67);
	compared++;
	if (memcmp(oref, oour, 128)) {
		if (failed++ < 5) {
			printf("MISMATCH hist_method n %u\n", p->n & 0xff);
			for (i = 0; i < 32; i++)
				if (oref[i] != oour[i]) {
					printf("  bin %u vendor %08x ours %08x\n", i, oref[i], oour[i]);
					break;
				}
		}
		return 1;
	}
	return 0;
}

static u32 rbits(void)
{
	u32 sc = 1 + rnd() % 16;

	return rnd() >> (32 - sc);
}

int main(void)
{
	const unsigned int n = getenv("T32O_NRAND") ?
		(unsigned int)atoi(getenv("T32O_NRAND")) : 3000;
	unsigned int c, i;

	for (c = 0; c < n; c++) {
		u16 h16[32];
		u32 h32[32], hh[32];
		u8 in[20], en[16], up[20], down[20], target[20], cur[20];
		struct t32_lce_hm16_par p;
		u32 mode = rnd() % 4, lo, hi;

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
			/* running sum right at 16384 / 16385 */
			u32 k1 = rnd() % 31, k2 = k1 + 1 + rnd() % (31 - k1), v = 16383 + rnd() % 3;

			memset(h16, 0, sizeof(h16));
			memset(h32, 0, sizeof(h32));
			h16[k1] = (u16)v;
			h32[k1] = v;
			if (rnd() & 1) {
				h16[k2] = (u16)(16385 - 16384 + rnd() % 2);
				h32[k2] = 1 + rnd() % 2;
			}
		}
		run_hfj16(h16);
		run_hfj(h32);
		if (c % 7 == 0) {
			/* pdf sums right at the 32767 / 32768 clamp */
			memset(h32, 0, sizeof(h32));
			h32[rnd() % 32] = 32766 + rnd() % 4;
			if (rnd() & 1)
				h32[rnd() % 32] += rnd() % 3;
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
			en[i] = (u8)(rnd() & 1);
		run_slc(in, en, up, down);

		lo = rnd();
		hi = rnd();
		if (c & 1)
			for (i = 0; i < 20; i++) {
				u32 r = rnd() % 4;

				if (r == 0)
					cur[i] = (u8)(target[i] + (lo & 0xff));
				else if (r == 1)
					target[i] = (u8)(cur[i] + (lo & 0xff));
			}
		run_conv(rnd(), lo, hi, target, cur);

		/* lce_hist_method: n 0..31 (the vendor's 1024/n table is read unchecked) */
		memset(&p, 0, sizeof(p));
		p.n = (rnd() % 32) | (rnd() & 0xffffff00u);
		p.c3 = rnd();
		p.b = rnd();
		p.k8 = (64 + rnd() % 192) | (rnd() & 0xffffff00u);
		p.c1 = rnd() % (1U << (4 + rnd() % 9));
		p.a = rnd() % 4096;
		p.e1 = rnd() % 2048;
		p.e2 = rnd() % 2048;
		p.sh = rnd();
		if (rnd() % 16 == 0)
			p.c3 = p.b = p.e2 = 0;
		for (i = 0; i < 32; i++)
			hh[i] = (rnd() % 8 == 0) ? rnd() : rnd() % (1U << (6 + rnd() % 9));
		run_hm(hh, &p);
	}

	printf("t32_lcet_oracle: random %u compared, %u mismatches\n", compared, failed);
	if (failed)
		return 1;
	return 0;
}
