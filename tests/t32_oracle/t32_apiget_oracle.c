// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: tisp_api_param_array_get (tx_isp_t32_blkrest.h) against
 * the vendor function (Matt Davis' recovered C, PR #15 @ 2e2e06bc).  Per case
 * random id (0 .. 20, with high bits), channel 0 / 1 (with high bits),
 * data blocks and callee results (each callee fills its buffer with case
 * bytes).  The vendor jump table t32_data_c2f00 is defined here with its
 * data.c values (lines 6115 .. 6131).  Compared: every callee call in order
 * (channel, buffer), the out buffer (2700 B canary filled), the length word
 * (canary), the mask-out and MSCA blocks; the return value where the vendor
 * defines it (the length; ids 3 .. 6, 8 and unknown ids return a left-over).
 * Exit 1 on the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_blkrest.h"

/* data.c:6115 .. 6131: code addresses of the vendor's switch */
u32 t32_data_c2f00[17] = { 0xb2aa8, 0xb2acc, 0xb2b00, 0xb2b54, 0xb2b54, 0xb2b54, 0xb2b54, 0xb2b2c, 0xb2b54,
			   0xb2bc8, 0xb2b90, 0xb2bac, 0xb2bac, 0xb2bac, 0xb2bac, 0xb2bac, 0xb2bac };
u8 t32_data_c2fa4[4], t32_data_cb8c4[4];
u8 t32_data_e4000[32] __attribute__((aligned(4)));
u8 t32_data_f0928[2 * 2636] __attribute__((aligned(4)));
extern u8 t32_data_f3258[512];
extern u64 tisp_api_param_array_get(u32, u32, u32, u32);

static u32 rs = 0xcc9e2d51u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

#define LOGN 4
struct hlog { unsigned int n; u32 e[LOGN][3]; };
static struct hlog vl, ol, *cur;
static u32 seed;
static const u8 *rbase;	/* block whose offset is logged (0 = stack buffer) */
static void cb(u32 k, u32 ch, u8 *b, u32 len)
{
	u32 i, x = seed ^ k;

	if (cur->n < LOGN) { cur->e[cur->n][0] = k; cur->e[cur->n][1] = ch; cur->e[cur->n][2] = rbase ? (u32)(b - rbase) : 0; }
	cur->n++;
	for (i = 0; i < len; i++) { x = x * 1103515245u + 12345u; b[i] = x >> 16; }
}
#define VP(a) ((u8 *)(unsigned long)(a))
u64 tisp_msca_api_get_mask_out_cfg(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; rbase = t32_data_e4000; cb(1, a0, VP(a1), 24); return 0; }
u64 tisp_g_fcrop_control(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; rbase = NULL; cb(2, a0, VP(a1), 20); return 0; }
u64 tisp_msca_api_get_par(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; rbase = t32_data_f0928; cb(3, a0, VP(a1), 2636); return 0; }
u64 tisp_bcsh_api_get_brightness(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; rbase = NULL; cb(4, a0, VP(a1), 1); return 0; }
u64 tisp_msca_api_get_autozoom(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; rbase = NULL; cb(5, a0, VP(a1), 60); return 0; }
static u8 *o_mo_base, *o_mp_base;
static void o_mo(u32 ch, u8 *c) { rbase = o_mo_base; cb(1, ch, c, 24); }
static void o_fc(u32 ch, u8 *b) { rbase = NULL; cb(2, ch, b, 20); }
static void o_mp(u32 ch, u8 *p) { rbase = o_mp_base; cb(3, ch, p, 2636); }
static void o_br(u32 ch, u8 *b) { rbase = NULL; cb(4, ch, b, 1); }
static void o_az(u32 ch, u8 *b) { rbase = NULL; cb(5, ch, b, 60); }
static const struct t32_apig_ops ops = { o_mo, o_fc, o_mp, o_br, o_az };

#define OUTB 2700
#define MPB sizeof(t32_data_f0928)
static void fill(u8 *p, u32 n) { u32 i; for (i = 0; i < n; i++) p[i] = rnd(); }

int main(void)
{
	u8 *out[2] = { t32o_alloc(OUTB), t32o_alloc(OUTB) }, *lenp = t32o_alloc(8);
	static u8 mo[32], mp[MPB], ma[512], pipe[1808];
	unsigned long n, N = 20000, per[18] = { 0 };
	u32 i;

	for (n = 0; n < N; n++) {
		struct t32_apig g;
		u32 ch = rnd() & 1, a0 = ch | (rnd() & 0xff00u), a3 = (rnd() & 3) ? rnd() % 21 : rnd(), id, ln, l0, rv, ro;

		if (rnd() & 1) a3 = (a3 & 0xffff) | (rnd() << 16);
		id = a3 & 0xffff;
		seed = rnd();
		fill(out[0], OUTB); memcpy(out[1], out[0], OUTB);
		fill(t32_data_e4000, 32); memcpy(mo, t32_data_e4000, 32);
		fill(t32_data_f0928, MPB); memcpy(mp, t32_data_f0928, MPB);
		fill(ma, 512); fill(pipe, sizeof(pipe));
		ln = l0 = rnd();

		memset(&ol, 0, sizeof(ol)); cur = &ol;
		o_mo_base = mo; o_mp_base = mp;
		g.ops = &ops; g.mask_attr = ma; g.pipe = pipe; g.mask_out = mo; g.msca_par = mp;
		ro = t32_api_param_array_get(&g, a0, out[1], &ln, a3);

		t32o_reset();
		memset(&vl, 0, sizeof(vl)); cur = &vl;
		memcpy(t32_data_f3258, ma, 512); memcpy(t32_data_dc284, pipe, sizeof(pipe));
		memcpy(lenp, &l0, 4);
		rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_api_param_array_get, a0, T32O_PTR(out[0]), T32O_PTR(lenp), a3, 0);
		memcpy(&l0, lenp, 4);

		if ((id < 17 && !(id >= 3 && id <= 8 && id != 7) && rv != ro) || l0 != ln || vl.n != ol.n ||
		    memcmp(vl.e, ol.e, sizeof(vl.e)) || memcmp(out[0], out[1], OUTB) || memcmp(t32_data_e4000, mo, 32) ||
		    memcmp(t32_data_f0928, mp, MPB)) {
			printf("t32_apiget_oracle: FAIL case %lu id %#x ch %#x: ret %#x/%#x len %#x/%#x calls %u/%u\n", n, a3, a0, rv, ro,
			       l0, ln, vl.n, ol.n);
			for (i = 0; i < OUTB; i++) if (out[0][i] != out[1][i]) { printf("  out +%u: %#x / %#x\n", i, out[0][i], out[1][i]); break; }
			return 1;
		}
		per[id < 17 ? id : 17]++;
	}
	printf("t32_apiget_oracle: PASS %lu cases (", N);
	for (i = 0; i < 18; i++) printf("%s%lu", i ? " " : "", per[i]);
	printf(" per id, last = unknown)\n");
	return 0;
}
