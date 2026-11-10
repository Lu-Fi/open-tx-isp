// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: lib_tisp_set_isp_memory_space and lib_tisp_get_isp_memory_space
 * (driver/t32/tx_isp_t32_libwrap.h, group 4) against Matt Davis' mechanical MIPS-to-C recovery (ref/, PR #15
 * @ 2e2e06bc; REFFIX dc074_big.sh points the get at the full 528-byte t32o7_data_dc074 below).
 *
 * Compared bit-exact: return value (low word), callee call count and all arguments (incl. the fifth, stack,
 * argument of tisp_mdns_get_mem_size), the flag byte t32_data_dd674, the MDNS channel blocks, the channel
 * state and the caller's config (unchanged).  Inputs: channels 0 / 1 with high garbage bytes, random state,
 * config and arguments.  The vendor does not range-check the channel; ch & 0xff >= 2 is not compared.
 *
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_libwrap.h"

u64 lib_tisp_set_isp_memory_space(u32, u32, u32, u32);
u64 lib_tisp_get_isp_memory_space(u32, u32, u32, u32);
extern u8 t32_data_dd674[4];
u8 t32o7_data_dc074[528] __attribute__((aligned(4)));

#define F(fn)	((t32o_fn)(void (*)(void))(fn))

static u32 rs = 0x13579bdfu;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static u32 ret_val, v_n, v_a[5], o_n, o_a[5];

u64 tisp_mdns_set_mem_cfg(u32 a0, u32 a1, u32 a2, u32 a3)
{
	v_n++;
	v_a[0] = a0; v_a[1] = a1; v_a[2] = a2; v_a[3] = a3; v_a[4] = 0;
	return ret_val;
}

u64 tisp_mdns_get_mem_size(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4)
{
	v_n++;
	v_a[0] = a0; v_a[1] = a1; v_a[2] = a2; v_a[3] = a3; v_a[4] = a4;
	return ret_val;
}

static u32 o_set(void *c, u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)c;
	o_n++;
	o_a[0] = a0; o_a[1] = a1; o_a[2] = a2; o_a[3] = a3; o_a[4] = 0;
	return ret_val;
}

static u32 o_get(void *c, u32 a0, u32 a1, u32 a2, u32 a3, u32 a4)
{
	(void)c;
	o_n++;
	o_a[0] = a0; o_a[1] = a1; o_a[2] = a2; o_a[3] = a3; o_a[4] = a4;
	return ret_val;
}

static unsigned int compared, failed;

int main(void)
{
	unsigned int c, i, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 20000;
	static u8 mdns[528], st[sizeof(t32_data_dc284)];
	struct t32_lib_hooks h;

	memset(&h, 0, sizeof(h));
	h.mdns_set_mem_cfg = o_set;
	h.mdns_get_mem_size = o_get;
	for (c = 0; c < n; c++) {
		u32 ch = (rnd() & 1) | (c % 3 ? rnd() & 0xffffff00u : 0), a1 = rnd(), a2 = rnd(), a3 = rnd(), r, o;
		u8 cfg[8], *vcfg, flag = 0;
		unsigned int bad;

		t32o_reset();
		for (i = 0; i < sizeof(st); i++)
			t32_data_dc284[i] = st[i] = (u8)rnd();
		for (i = 0; i < sizeof(mdns); i++)
			t32o7_data_dc074[i] = mdns[i] = (u8)rnd();
		for (i = 0; i < sizeof(cfg); i++)
			cfg[i] = (u8)rnd();
		vcfg = t32o_alloc(sizeof(cfg));
		memcpy(vcfg, cfg, sizeof(cfg));
		t32_data_dd674[0] = 0;
		ret_val = rnd();
		v_n = o_n = 0;

		r = (u32)t32o_call(F(lib_tisp_set_isp_memory_space), ch, a1, a2, a3, 0);
		o = t32_lib_set_isp_memory_space(&h, &flag, ch, a1, a2, a3);
		bad = r != o || v_n != o_n || memcmp(v_a, o_a, sizeof(v_a)) || t32_data_dd674[0] != flag;
		compared++;
		if (bad && failed++ < 10)
			printf("MISMATCH set_isp_memory_space(%x)\n", ch);

		v_n = o_n = 0;
		ret_val = rnd();
		r = (u32)t32o_call(F(lib_tisp_get_isp_memory_space), ch, T32O_PTR(vcfg), a2, a3, 0);
		o = t32_lib_get_isp_memory_space(&h, mdns, st, ch, cfg);
		bad = r != o || v_n != o_n || memcmp(v_a, o_a, sizeof(v_a)) || memcmp(mdns, t32o7_data_dc074, 528) ||
		      memcmp(st, t32_data_dc284, sizeof(st)) || memcmp(vcfg, cfg, sizeof(cfg));
		compared++;
		if (bad && failed++ < 10)
			printf("MISMATCH get_isp_memory_space(%x): ret %x/%x a %x/%x %x/%x %x/%x %x/%x\n", ch, r, o,
			       v_a[1], o_a[1], v_a[2], o_a[2], v_a[3], o_a[3], v_a[4], o_a[4]);
	}
	printf("t32_libms_oracle: %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
