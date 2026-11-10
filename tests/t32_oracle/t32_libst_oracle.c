// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: lib_tisp_dma_cache_32, lib_tisp_api_s_runningMode, lib_tisp_vic_get_par and
 * lib_tisp_api_get_3a_info (driver/t32/tx_isp_t32_libwrap.h, group 2) against Matt Davis' mechanical
 * MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc).
 *
 * Compared bit-exact: return value (low word; lib_tisp_api_s_runningMode is void, its v0 is an address half and
 * not compared), the channel state t32_data_dc284, the VIC parameter words, the AE debug arrays and the output
 * words with canaries around them.  Inputs: random words incl. 32-byte aligned and boundary values, channels
 * 0..3 with high garbage bytes, n = 0 / 1 / random.
 *
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_libwrap.h"

u64 lib_tisp_dma_cache_32(u32, u32, u32, u32);
u64 lib_tisp_api_s_runningMode(u32, u32, u32, u32);
u64 lib_tisp_vic_get_par(u32, u32, u32, u32);
u64 lib_tisp_api_get_3a_info(u32, u32, u32, u32);
extern u32 t32_data_e3f30[1], t32_data_e3f2c[1], t32_data_e1500[4], t32_data_e14f0[4];

#define F(fn)	((t32o_fn)(void (*)(void))(fn))

static u32 rs = 0x2545f491u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static unsigned int compared, failed;

static void cmp(const char *what, unsigned int bad, u32 a0, u32 a1)
{
	compared++;
	if (bad && failed++ < 10)
		printf("MISMATCH %s(%x, %x)\n", what, a0, a1);
}

int main(void)
{
	unsigned int c, i, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 20000;
	static u8 st[sizeof(t32_data_dc284)];

	for (c = 0; c < n; c++) {
		u32 x = rnd(), ch = rnd() % 4, v = rnd(), r, *vb, ob[6], on[4], om[4];
		struct t32_vic_par vp;

		if (c % 4 == 0)
			x &= ~0x1fu;
		if (c % 9 == 0)
			x = c % 2 ? 0xffffffffu : 0xffffffe0u;
		if (c % 3)
			ch |= rnd() & 0xffffff00u;
		if (c % 5 < 2)
			v = c % 5;
		t32o_reset();

		r = (u32)t32o_call(F(lib_tisp_dma_cache_32), x, v, 0, 0, 0);
		cmp("dma_cache_32", r != t32_lib_dma_cache_32(x), x, 0);

		for (i = 0; i < sizeof(st); i++)
			t32_data_dc284[i] = st[i] = (u8)rnd();
		t32o_call(F(lib_tisp_api_s_runningMode), v, x, 0, 0, 0);
		t32_lib_set_running_mode(st, v);
		cmp("s_runningMode", memcmp(st, t32_data_dc284, sizeof(st)) != 0, v, x);

		vp.par0 = t32_data_e3f30[0] = rnd();
		vp.par1 = t32_data_e3f2c[0] = rnd();
		vb = t32o_alloc(6 * 4);
		for (i = 0; i < 6; i++)
			vb[i] = ob[i] = rnd();
		r = (u32)t32o_call(F(lib_tisp_vic_get_par), T32O_PTR(&vb[1]), T32O_PTR(&vb[4]), 0, 0, 0);
		cmp("vic_get_par", r != t32_lib_vic_get_par(&vp, &ob[1], &ob[4]) || memcmp(vb, ob, sizeof(ob)) != 0 ||
		    vp.par0 != t32_data_e3f30[0] || vp.par1 != t32_data_e3f2c[0], 0, 0);

		for (i = 0; i < 4; i++) {
			t32_data_e1500[i] = on[i] = rnd();
			t32_data_e14f0[i] = om[i] = rnd();
		}
		r = (u32)t32o_call(F(lib_tisp_api_get_3a_info), ch, v, 0, 0, 0);
		cmp("get_3a_info", r != t32_lib_get_3a_info(on, om, ch, v) || memcmp(on, t32_data_e1500, 16) != 0 ||
		    memcmp(om, t32_data_e14f0, 16) != 0, ch, v);
	}
	printf("t32_libst_oracle: %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
