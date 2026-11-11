// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: tisp_lce_interrupt_static (tx_isp_t32_blkrest.h) against
 * the vendor function (Matt Davis' recovered C, PR #15 @ 2e2e06bc).  Per case
 * random channel, bank register value, d03a0 (0 / 1 / other), buffer table
 * (e1db8, physical addresses random), curve buffer contents.  Compared:
 * return value, the register read, every tisp_reg_write (channel, register,
 * value, order), the get_sum_data call (channel, bank pointer, checksum of
 * the bank), the bank word (e1d7c), the channel byte (e1d78), every byte of
 * the curve buffer and of the buffer table.
 * Not observable here: private_dma_cache_sync (strong in env.c).
 * Exit 1 on the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_blkrest.h"

u32 t32_data_e1d7c[1];
u8 t32_data_e1d78[4];
extern u64 t32o_ref_tisp_lce_interrupt_static(u32, u32, u32, u32), private_dma_cache_sync(u32, u32, u32, u32);

static u32 rs = 0x85ebca6bu;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

#define LOGN 12
struct hlog { unsigned int n; u32 e[LOGN][4]; };
static struct hlog vl, ol, *cur;
static u32 bank_val;
static u8 *stat;
static void hrec(u32 k, u32 a, u32 b, u32 c)
{
	if (cur->n < LOGN) { cur->e[cur->n][0] = k; cur->e[cur->n][1] = a; cur->e[cur->n][2] = b; cur->e[cur->n][3] = c; }
	cur->n++;
}
static u32 rd(u32 reg) { hrec(1, reg, 0, 0); return bank_val; }
u64 tisp_reg_write(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; hrec(2, a0, a1, a2); return 0; }
u64 tisp_lce_get_sum_data(u32 a0, u32 a1, u32 a2, u32 a3)
{
	const u8 *p = (const u8 *)(unsigned long)a1;
	u32 h = 2166136261u, i;

	(void)a2; (void)a3;
	for (i = 0; i < T32_LCI_BANK_LEN; i++) h = (h ^ p[i]) * 16777619u;
	hrec(3, a0, a1 - T32O_PTR(stat), h);
	return 0;
}
static void o_wr(u32 ch, u32 reg, u32 val) { tisp_reg_write(ch, reg, val, 0); }
static void o_cs(void *p, u32 len) { private_dma_cache_sync(0, T32O_PTR(p), len, 0); }
static void o_sum(u32 ch, u8 *s) { tisp_lce_get_sum_data(ch, T32O_PTR(s), 0, 0); }
static const struct t32_lci_ops ops = { rd, o_wr, o_cs, o_sum };

#define CURB (2 * T32_LCI_CURVE_LEN)

int main(void)
{
	u8 *cv[2] = { t32o_alloc(CURB), t32o_alloc(CURB) }, *tb[2] = { t32o_alloc(4 * 36), t32o_alloc(4 * 36) };
	u32 i, s;
	unsigned long n, N = 4000, shd = 0;

	stat = t32o_alloc(4 * T32_LCI_BANK_LEN);
	for (i = 0; i < 4 * T32_LCI_BANK_LEN; i++) stat[i] = rnd();
	for (n = 0; n < N; n++) {
		struct t32_lci l;
		u32 ch = rnd() & 3, bank[2], rv, ro;
		u8 sh = (rnd() % 3 == 0) ? 0 : ((rnd() & 1) ? 1 : rnd() | 2), last[2];

		bank_val = rnd();
		for (i = 0; i < CURB; i++) cv[0][i] = rnd();
		memcpy(cv[1], cv[0], CURB);
		for (s = 0; s < 2; s++) {
			for (i = 0; i < 4 * 36; i++) tb[s][i] = rnd();
			if (s) memcpy(tb[1], tb[0], 4 * 36);
			memcpy(tb[s] + 36 * ch + 12, (u32[3]){ T32O_PTR(cv[s]), T32O_PTR(stat), rnd() }, 12);
		}
		memcpy(tb[1] + 36 * ch + 20, tb[0] + 36 * ch + 20, 4);
		bank[0] = bank[1] = rnd(); last[0] = last[1] = rnd();

		memset(&ol, 0, sizeof(ol)); cur = &ol;
		l.ops = &ops; l.buf = (const struct t32_lci_buf *)tb[1]; l.shadow = &sh; l.bank = &bank[1]; l.last_ch = &last[1];
		ro = (u32)t32_lce_interrupt_static(&l, ch);

		t32o_reset();
		memset(&vl, 0, sizeof(vl)); cur = &vl;
		t32o_rd_hook = rd;
		memcpy(t32_data_e1db8, tb[0], 4 * 36);
		t32_data_e1d7c[0] = bank[0]; t32_data_e1d78[0] = last[0];
		t32_data_d03a0[0] = sh;
		rv = (u32)t32o_call((t32o_fn)(void (*)(void))t32o_ref_tisp_lce_interrupt_static, ch, rnd(), rnd(), rnd(), 0);
		t32_data_d03a0[0] = 0;
		t32o_rd_hook = NULL;
		memcpy(tb[0] + 36 * ch + 12, tb[1] + 36 * ch + 12, 4);	/* the curve pointers differ by design */
		memcpy(t32_data_e1db8 + 36 * ch + 12, tb[1] + 36 * ch + 12, 4);

		if (rv != ro || vl.n != ol.n || memcmp(vl.e, ol.e, sizeof(vl.e)) || t32_data_e1d7c[0] != bank[1] ||
		    t32_data_e1d78[0] != last[1] || memcmp(cv[0], cv[1], CURB) || memcmp(t32_data_e1db8, tb[1], 4 * 36)) {
			printf("t32_lceirq_oracle: FAIL case %lu ch %u sh %u: ret %#x/%#x calls %u/%u\n", n, ch, sh, rv, ro, vl.n, ol.n);
			for (i = 0; i < vl.n && i < LOGN; i++)
				printf("  %u: %u %#x %#x %#x / %u %#x %#x %#x\n", i, vl.e[i][0], vl.e[i][1], vl.e[i][2], vl.e[i][3],
				       ol.e[i][0], ol.e[i][1], ol.e[i][2], ol.e[i][3]);
			return 1;
		}
		shd += sh != 0;
	}
	printf("t32_lceirq_oracle: PASS %lu cases (%lu shadow mode)\n", N, shd);
	return 0;
}
