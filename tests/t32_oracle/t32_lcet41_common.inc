/*
 * Shared harness of the t32_lcet41_* oracles: Tisp_lce_soft_t41_simd (vendor 0x54ce4; Matt Davis' recovered C, PR #15
 * @ 2e2e06bc) against the blocks t32_lce_t41_blkN of driver/t32/tx_isp_t32_lce.h.
 *
 * Method: the vendor function cannot be cut into callable pieces, so each phase oracle runs the whole vendor function
 * truncated by a build-time sed (trunc_t41.sh, REFFIX, T32O_STOP = label) at the first arrival at the label that ends
 * the phase, and the port blocks of the phase (the first pass of the loops).  Compared after the run: the whole
 * working buffer (all bytes incl. the ones neither side may touch), the scratch .bss objects of the vendor (e18a4 ..
 * e1d74) against the port's scratch, the parameter block (unchanged).  The six scalar leaves and the 0x54bd0 helper run as
 * the vendor functions on the vendor side and as their ports (hook table) on the port side.
 * Exit 0 = equal; 1 = first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_lce.h"

#define WSZ 0x26400U
#define PSZ 400U

u8 oc_bf8a0[128] __asm__("t32_data_bf8a0") __attribute__((weak));
u8 oc_d6c66 __asm__("t32_data_d6c66") __attribute__((weak)) = 20;
u8 oc_d6c67 __asm__("t32_data_d6c67") __attribute__((weak)) = 20;
/* the vendor's scratch .bss objects e18a4 .. e1d74 (one blob so the arrays that run into the next symbol keep their layout) */
u8 oc_bss[0x4d0] __attribute__((aligned(16)));
__asm__(".globl t32_data_e18a4, t32_data_e18b4, t32_data_e1954, t32_data_e19f4, t32_data_e1a14, t32_data_e1a34, t32_data_e1a54, t32_data_e1a74, t32_data_e1a84, t32_data_e1aa4, t32_data_e1ac4, t32_data_e1ae4, t32_data_e1b04, t32_data_e1b14, t32_data_e1b24, t32_data_e1b34, t32_data_e1b44, t32_data_e1b54, t32_data_e1d54, t32_data_e1d64, t32_data_ce210\n"
	".set t32_data_e18a4, oc_bss+0x0\n.set t32_data_e18b4, oc_bss+0x10\n.set t32_data_e1954, oc_bss+0xb0\n.set t32_data_e19f4, oc_bss+0x150\n"
	".set t32_data_e1a14, oc_bss+0x170\n.set t32_data_e1a34, oc_bss+0x190\n.set t32_data_e1a54, oc_bss+0x1b0\n.set t32_data_e1a74, oc_bss+0x1d0\n"
	".set t32_data_e1a84, oc_bss+0x1e0\n.set t32_data_e1aa4, oc_bss+0x200\n.set t32_data_e1ac4, oc_bss+0x220\n.set t32_data_e1ae4, oc_bss+0x240\n"
	".set t32_data_e1b04, oc_bss+0x260\n.set t32_data_e1b14, oc_bss+0x270\n.set t32_data_e1b24, oc_bss+0x280\n.set t32_data_e1b34, oc_bss+0x290\n"
	".set t32_data_e1b44, oc_bss+0x2a0\n.set t32_data_e1b54, oc_bss+0x2b0\n.set t32_data_e1d54, oc_bss+0x4b0\n.set t32_data_e1d64, oc_bss+0x4c0\n"
	".set t32_data_ce210, t32_data_ce130+224\n");

extern u64 Tisp_lce_soft_t41_simd(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 lce_hist_filter_and_judge(u32, u32, u32, u32);

static u32 rs = 0x41c3e5u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

/* hook table of the port side: the existing ports of this header */
static void h_hfj(const u32 *h, u8 *o) { t32_lce_hist_filter_and_judge(h, o); }
static void h_hm(const u32 *h, u32 *o, const u32 *p)
{
	t32_lce_hist_method32(h, o, (const struct t32_lce_hm16_par *)p, oc_d6c66, oc_d6c67);
}
static void h_cdf(const u32 *p, u8 *o) { t32_lce_pdf_to_cdf_raw(p, o); }
static void h_slc(const u8 *i, u8 *o, const u8 *e, const u8 *u, const u8 *d) { t32_lce_self_light_correct_local(i, o, e, u, d); }
static void h_conv(u32 g, u32 lo, u32 hi, const u8 *t, u8 *c) { t32_lce_16bit_data_converge(g, lo, hi, t, c); }
static void h_hfj16(const u16 *h, u8 *o) { t32_lce_hist_filter_and_judge_16bit(h, o); }
static void h_slct(const u8 *i, u8 *o, u32 en, const u8 *u, const u8 *d) { t32_lce_self_light_correct_local_simd_test(i, o, en, u, d); }

static struct t32_lce_t41_ops ops = { h_hfj, h_hm, h_cdf, h_slc, h_conv, h_hfj16, h_slct, 20 };

static unsigned int compared;

/* one case; returns 1 on mismatch */
static void port_phase(const u8 *par, u8 *w, u32 mode, const struct t32_lce_t41_ops *ops);
static u32 port_bss_len(void);
static u32 port_bss_off(void);
static const u8 *port_bss(void);
static void gen_extra(u8 *par, u8 *w);
static int one(u8 *par, u8 *w, u32 mode)
{
	u8 *wo = malloc(WSZ), *po = malloc(PSZ), *wr, *pr;
	int bad = 0;
	unsigned int i;

	memcpy(wo, w, WSZ);
	memcpy(po, par, PSZ);
	ops.shift_byte = oc_d6c67;
	t32o_reset();
	wr = t32o_alloc(WSZ); pr = t32o_alloc(PSZ);
	memcpy(wr, w, WSZ);
	memcpy(pr, par, PSZ);
	memset(oc_bss, 0, sizeof(oc_bss));
	t32o_call((t32o_fn)(void (*)(void))Tisp_lce_soft_t41_simd, 0, T32O_PTR(pr), T32O_PTR(wr), mode, 0);
	port_phase(po, wo, mode, &ops);
	compared++;
	if (memcmp(pr, par, PSZ))
		bad = printf("MISMATCH vendor changed par\n");
	if (memcmp(po, par, PSZ))
		bad = printf("MISMATCH port changed par\n");
	for (i = 0; i < WSZ && !bad; i++)
		if (wr[i] != wo[i])
			bad = printf("MISMATCH w+0x%x vendor %02x port %02x (mode %u)\n", i, wr[i], wo[i], mode);
	for (i = 0; i < port_bss_len() && !bad; i++)
		if (oc_bss[port_bss_off() + i] != port_bss()[i])
			bad = printf("MISMATCH bss+0x%x vendor %02x port %02x (mode %u)\n", port_bss_off() + i,
				     oc_bss[port_bss_off() + i], port_bss()[i], mode);
	free(wo); free(po);
	return bad;
}

/* random inputs: a parameter block and a working buffer whose tile bins give totals on both sides of 0x8000 */
static int gen_and_run(void)
{
	static u8 par[PSZ], w[WSZ];
	unsigned int i, sc = 1u << (rnd() % 9);
	u32 mode;

	for (i = 0; i < PSZ; i++)
		par[i] = (u8)rnd();
	for (i = 0; i < WSZ; i++)
		w[i] = (u8)rnd();
	for (i = 0; i < 0x9000; i += 2) {
		u32 v = rnd() % (1 + sc);

		w[i] = (u8)v; w[i + 1] = (u8)(v >> 8);
	}
	{
		u32 mult = 1 + rnd() % 300, sh = rnd() % 20;

		memcpy(par + 4, &mult, 4);
		memcpy(par + 8, &sh, 4);
	}
	par[284] = (rnd() % 3) ? 1 : (u8)rnd();
	par[285] = (u8)(rnd() % 32);
	par[294] = (rnd() % 3) ? 1 : (u8)rnd();
	mode = (rnd() % 3) ? 4 : rnd() % 6;
	oc_d6c66 = (u8)rnd();
	oc_d6c67 = (u8)(6 + rnd() % 32);
	gen_extra(par, w);
	return one(par, w, mode);
}

int main(void)
{
	unsigned int c, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 300;
	int fail = 0;

	for (c = 0; c < n && !fail; c++)
		fail = gen_and_run();
	printf("%s: %u compared, %u mismatches\n", ORACLE_NAME, compared, fail ? 1u : 0u);
	return fail;
}
