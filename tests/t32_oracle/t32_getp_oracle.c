// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: module-parameter getters of tx_isp_t32_api_get.h
 * (get_clk*_name, get_isp_clk*, get_isp_memopt, get_isp_cache_line,
 * get_isp_streamoff_mode) against the vendor functions (Matt Davis' recovered
 * C, PR #15 @ 2e2e06bc) on random parameter words.
 *
 * get_clk_name, get_clka_name and get_clkl_name are not run as references:
 * their recovered C returns an address (&.data[60], &t32_data_ce254,
 * .data + 40) where the vendor binary loads a word (tx-isp-t32.ko 0x2ac
 * "lw v0,60(v0)", 0x2c4 "lw v0,52(v0)", 0x2dc "lw v0,44(v0)", all .data).
 * For those three the oracle checks ours against that load.
 * Exit 1 on the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_api_get.h"

/* vendor .data (ce220, 0x60 B; the name pointers +0x2c..+0x3c) and .bss start (d9000: isp_cache_line +0, stream-off mode +24) */
u32 t32_data_ce220[24] __attribute__((aligned(16), weak));
u32 t32_data_d9000[8] __attribute__((aligned(16), weak));
__asm__(".weak t32_data_ce250, t32_data_ce254, t32_data_ce258\n"
	".set t32_data_ce250, t32_data_ce220 + 0x30\n.set t32_data_ce254, t32_data_ce220 + 0x34\n"
	".set t32_data_ce258, t32_data_ce220 + 0x38");

extern u64 get_clks_name(u32, u32, u32, u32);
extern char *get_clkv_name(void);
extern u32 get_isp_clk(void), get_isp_clks(void), get_isp_clka(void), get_isp_clkv(void), get_isp_clkl(void);
extern u32 get_isp_memopt(void), get_isp_cache_line(void), get_isp_streamoff_mode(void);

static u32 rs = 0x51c3e7d1u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

#define NG 13
static const char *const names[NG] = {
	"get_clk_name", "get_clks_name", "get_clka_name", "get_clkv_name", "get_clkl_name", "get_isp_clk",
	"get_isp_clks", "get_isp_clka", "get_isp_clkv", "get_isp_clkl", "get_isp_memopt",
	"get_isp_cache_line", "get_isp_streamoff_mode"
};

static u32 vendor(int k)
{
	switch (k) {
	case 0: return t32_data_ce220[15];	/* lw v0, 60(.data) */
	case 1: return (u32)t32o_call((t32o_fn)(void (*)(void))get_clks_name, 0, 0, 0, 0, 0);
	case 2: return t32_data_ce220[13];	/* lw v0, 52(.data) */
	case 3: return (u32)(uintptr_t)get_clkv_name();
	case 4: return t32_data_ce220[11];	/* lw v0, 44(.data) */
	case 5: return get_isp_clk();
	case 6: return get_isp_clks();
	case 7: return get_isp_clka();
	case 8: return get_isp_clkv();
	case 9: return get_isp_clkl();
	case 10: return get_isp_memopt();
	case 11: return get_isp_cache_line();
	default: return get_isp_streamoff_mode();
	}
}

static u32 ours(int k, const struct t32_isp_modparams *p)
{
	switch (k) {
	case 0: return T32O_PTR(t32_get_clk_name(p));
	case 1: return T32O_PTR(t32_get_clks_name(p));
	case 2: return T32O_PTR(t32_get_clka_name(p));
	case 3: return T32O_PTR(t32_get_clkv_name(p));
	case 4: return T32O_PTR(t32_get_clkl_name(p));
	case 5: return t32_get_isp_clk(p);
	case 6: return t32_get_isp_clks(p);
	case 7: return t32_get_isp_clka(p);
	case 8: return t32_get_isp_clkv(p);
	case 9: return t32_get_isp_clkl(p);
	case 10: return t32_get_isp_memopt(p);
	case 11: return t32_get_isp_cache_line(p);
	default: return t32_get_isp_streamoff_mode(p);
	}
}

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 20000;
	unsigned int c, i, cmp = 0;
	int k;

	for (c = 0; c < n; c++) {
		struct t32_isp_modparams p;
		static char *namebuf[5];
		u32 w;

		t32o_reset();
		for (i = 0; i < 24; i++)
			t32_data_ce220[i] = rnd();
		for (i = 0; i < 8; i++)
			t32_data_d9000[i] = rnd();
		/* the name pointers: 5 buffers below 4 GiB (the vendor words are their addresses) */
		for (i = 0; i < 5; i++) {
			namebuf[i] = t32o_alloc(8 + (rnd() & 0x3c0));
			t32_data_ce220[11 + i] = T32O_PTR(namebuf[i]);
		}
		if (c % 7 == 0)
			t32_data_ce220[5] = c & 1 ? 1500 : 0;	/* isp_memopt like the module default / 0 */
		p.isp_memopt = t32_data_ce220[5];
		p.isp_clkl = t32_data_ce220[6];
		p.isp_clkv = t32_data_ce220[7];
		p.isp_clka = t32_data_ce220[8];
		p.isp_clks = t32_data_ce220[9];
		p.isp_clk = t32_data_ce220[10];
		p.clkl_name = namebuf[0];
		p.clkv_name = namebuf[1];
		p.clka_name = namebuf[2];
		p.clks_name = namebuf[3];
		p.clk_name = namebuf[4];
		p.isp_cache_line = t32_data_d9000[0];
		p.streamoff_mode = t32_data_d9000[6];
		for (k = 0; k < NG; k++) {
			w = vendor(k);
			if (w != ours(k, &p)) {
				printf("t32_getp_oracle: case %u %s: vendor 0x%08x ours 0x%08x\n", c, names[k], w, ours(k, &p));
				return 1;
			}
			cmp++;
		}
	}
	printf("t32_getp_oracle: %u getter calls compared (%u random parameter sets, 13 getters), 0 mismatches\n", cmp, n);
	return 0;
}
