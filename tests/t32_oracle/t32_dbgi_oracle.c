// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: t32_isp_debug_info (dispatch through the 16-word table t32_data_c3020) and
 * t32_isp_info_tisp (pass-through to lib_tisp_debug_info) of driver/t32/tx_isp_t32_dbg.h against
 * the recovered vendor C (Matt Davis, PR #15 @ 2e2e06bc).  Random table (entries 0 or one of 16
 * recording handlers), random section number 0..15 and arguments; compared: result, which
 * handler ran with which four argument words.  Exit 0 = all equal, 1 = first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "t32_oracle_env.h"
int t32dbg_printk(const char *fmt, ...);
#define T32_DBG_PRINTK t32dbg_printk
#include "tx_isp_t32_dbg.h"

#define R(f)	((t32o_fn)(void (*)(void))(f))
int t32dbg_printk(const char *fmt, ...) { (void)fmt; return 0; }
int isp_printf(unsigned int level, const char *fmt, ...) { (void)level; (void)fmt; return 0; }
u32 t32_data_c3020[16];
static u32 log_[2][6];
static int side, nlog[2];

#define H(i) static u64 h##i(u32 a, u32 b, u32 c, u32 d) { nlog[side]++; log_[side][0] = i; log_[side][1] = a; log_[side][2] = b; log_[side][3] = c; log_[side][4] = d; return 0x1000 + i; }
H(0) H(1) H(2) H(3) H(4) H(5) H(6) H(7) H(8) H(9) H(10) H(11) H(12) H(13) H(14) H(15)
static u64 (*const hs[16])(u32, u32, u32, u32) = { h0, h1, h2, h3, h4, h5, h6, h7, h8, h9, h10, h11, h12, h13, h14, h15 };

u64 lib_tisp_debug_info(u32 a, u32 b, u32 c, u32 d)
{
	nlog[side]++; log_[side][0] = 99; log_[side][1] = a; log_[side][2] = b; log_[side][3] = c; log_[side][4] = d;
	return 0x777;
}
u64 isp_debug_info(u32, u32, u32, u32), isp_info_tisp(u32, u32, u32, u32);

static u32 rs = 0x7f4a7c15u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}
static u32 slib(u32 a, u32 b, u32 c, u32 d) { return (u32)lib_tisp_debug_info(a, b, c, d); }

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 20000, c, i, ran = 0;

	for (c = 0; c < n; c++) {
		u32 idx = rnd() & 15, a1 = rnd(), a2 = rnd(), a3 = rnd(), rv, ro;

		if ((c & 7) == 0) a1 = 0;
		for (i = 0; i < 16; i++)
			t32_data_c3020[i] = (rnd() & 3) ? T32O_PTR(hs[(i + c) & 15]) : 0;
		nlog[0] = nlog[1] = 0; memset(log_, 0, sizeof(log_));
		side = 0;
		rv = (u32)t32o_call(R(isp_debug_info), idx, a1, a2, a3, 0);
		{ u32 l0[6]; int n0 = nlog[0]; memcpy(l0, log_[0], sizeof(l0));
		side = 1;
		ro = (u32)t32_isp_debug_info(t32_data_c3020, idx, a1, a2, a3);
		if (rv != ro || n0 != nlog[1] || memcmp(l0, log_[1], 5 * 4)) {
			printf("t32_dbgi_oracle: case %u idx %u: vendor ret %x calls %d h%u %08x %08x %08x %08x / ours ret %x calls %d h%u %08x %08x %08x %08x\n",
			       c, idx, rv, n0, l0[0], l0[1], l0[2], l0[3], l0[4], ro, nlog[1], log_[1][0], log_[1][1], log_[1][2], log_[1][3], log_[1][4]);
			return 1;
		}
		ran += n0; }

		/* isp_info_tisp */
		nlog[0] = nlog[1] = 0; memset(log_, 0, sizeof(log_));
		side = 0;
		rv = (u32)t32o_call(R(isp_info_tisp), a1, a2, a3, idx, 0);
		{ u32 l0[6]; int n0 = nlog[0]; memcpy(l0, log_[0], sizeof(l0));
		side = 1;
		ro = (u32)t32_isp_info_tisp(slib, a1, a2, a3, idx);
		if (rv != ro || n0 != 1 || nlog[1] != 1 || memcmp(l0, log_[1], 5 * 4)) {
			printf("t32_dbgi_oracle: case %u isp_info_tisp: ret %x/%x calls %d/%d args differ %d\n", c, rv, ro, n0, nlog[1],
			       memcmp(l0, log_[1], 5 * 4) != 0);
			return 1;
		}
		ran++; }
	}
	printf("t32_dbgi_oracle: %u calls compared (%u handler runs), 0 mismatches\n", 2 * n, ran);
	return 0;
}
