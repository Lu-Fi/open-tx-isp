// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our ldc_video_s_stream and ldc_core_ops_init (driver/t32/tx_isp_t32_ldcops.h) against Matt
 * Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc).
 *
 * Compared bit-exact: return value, the whole private block (state words and the header / canaries around
 * them), the argument (unchanged).  Inputs: enable 0 / 1 / random, instance 0 / 1, states 0..5 and random,
 * NULL and error (-4095..-1) sub-device and private pointers (incl. -4095 itself).
 *
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_ldcops.h"

u64 ldc_video_s_stream(u32, u32, u32, u32);
u64 ldc_core_ops_init(u32, u32, u32, u32);

#define F(fn)	((t32o_fn)(void (*)(void))(fn))
#define PB	(288 + 8 + 16)

static u32 rs = 0x55aa1234u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

int main(void)
{
	unsigned int c, i, compared = 0, failed = 0, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 20000;
	static unsigned char mine[PB] __attribute__((aligned(8)));
	static struct t32_ldc_sd sd;

	for (c = 0; c < n; c++) {
		u8 *vsd, *vpriv;
		u32 *va, vs, vp, r, o, err = c % 23 == 0 ? 1 + rnd() % 4095 : 0;
		struct t32_ldc_stream_arg arg;
		struct t32_ldc_sd *osd = &sd;
		struct t32_ldc_priv *op = (struct t32_ldc_priv *)mine;
		unsigned int bad, which;

		t32o_reset();
		vsd = t32o_alloc(288);
		vpriv = t32o_alloc(PB);
		va = t32o_alloc(8);
		for (i = 0; i < PB; i++)
			vpriv[i] = mine[i] = (u8)rnd();
		for (i = 0; i < 2; i++) {
			u32 s = c % 4 ? rnd() % 6 : rnd();

			memcpy(vpriv + 288 + 4 * i, &s, 4);
			memcpy(mine + 288 + 4 * i, &s, 4);
		}
		arg.enable = c / 2 % 3 == 2 ? rnd() : c / 2 % 2;
		arg.idx = rnd() & 1;
		va[0] = arg.enable;
		va[1] = arg.idx;
		vs = T32O_PTR(vsd);
		vp = T32O_PTR(vpriv);
		which = c % 46 < 23 ? 0 : 1;
		if (err && c % 69 == 0)
			err = 4095;	/* the lowest error pointer, -4095 */
		if (err && which == 0) {
			vs = (u32)-err;
			osd = (struct t32_ldc_sd *)(uintptr_t)-(uintptr_t)err;
		} else if (err) {
			vp = (u32)-err;
			op = (struct t32_ldc_priv *)(uintptr_t)-(uintptr_t)err;
		}
		if (c % 31 == 5) {
			if (c & 1) {
				vs = 0;
				osd = NULL;
			} else {
				vp = 0;
				op = NULL;
			}
		}
		memcpy(vsd + 280, &vp, 4);
		sd.priv = op;

		if (c % 2) {
			r = (u32)t32o_call(F(ldc_core_ops_init), vs, T32O_PTR(va), 0, 0, 0);
			o = t32_ldc_core_ops_init(osd, &arg);
		} else if (vs && vp && vs < 0xfffff001u && vp < 0xfffff001u) {
			r = (u32)t32o_call(F(ldc_video_s_stream), vs, T32O_PTR(va), 0, 0, 0);
			o = t32_ldc_video_s_stream(osd, &arg);
		} else {
			continue;	/* ldc_video_s_stream does not check its pointers */
		}
		bad = r != o || memcmp(vpriv, mine, PB) || va[0] != arg.enable || va[1] != arg.idx;
		compared++;
		if (bad && failed++ < 10)
			printf("MISMATCH %s case %u: ret %x/%x en %x idx %u\n", c % 2 ? "core_ops_init" : "video_s_stream",
			       c, r, o, arg.enable, arg.idx);
	}
	printf("t32_ldcops_oracle: %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
