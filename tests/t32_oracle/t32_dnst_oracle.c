// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our lib_tisp_csccr_dn_status (driver/t32/tx_isp_t32_bayer.h, group 3) against the vendor
 * function and tisp_reg_read from Matt Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc).
 *
 * Compared bit-exact: return value (low word), the last-value array t32_data_e3130, the shadow images
 * (unchanged), no register writes.  Inputs: mode byte 0 / 1 / 2, channels 0..3 with high garbage bytes,
 * register / shadow words and last values zero or random (all four zero/non-zero combinations).
 *
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_bayer.h"

u64 lib_tisp_csccr_dn_status(u32, u32, u32, u32);
extern u32 t32_data_e3130[4];

#define F(fn)	((t32o_fn)(void (*)(void))(fn))
#define WORDS	800

static u32 rs = 0x2468ace1u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static u32 regs[0x100], *img[4], mine[4][WORDS];

static u32 rd(u32 reg) { return regs[reg & 0xff]; }
static u32 h_read(void *ctx, u32 reg) { (void)ctx; return rd(reg); }
static u32 *h_shadow(void *ctx, u32 ch) { (void)ctx; return mine[ch]; }

static u32 val(unsigned int k)
{
	return k & 1 ? rnd() | 1 : 0;
}

int main(void)
{
	unsigned int c, i, ch_i, compared = 0, failed = 0, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 20000;
	struct t32_regio io;
	u32 last[4];

	memset(&io, 0, sizeof(io));
	io.sys_read = h_read;
	io.shadow = h_shadow;
	t32o_rd_hook = rd;
	for (c = 0; c < n; c++) {
		u32 ch = rnd() % 4, r, o;
		u8 *blk;
		unsigned int bad;

		if (c % 3)
			ch |= rnd() & 0xffffff00u;
		io.mode = c % 3 == 2 ? 2 : c % 2;
		t32o_reset();
		for (i = 0; i < 0x100; i++)
			regs[i] = val(rnd());
		blk = t32o_alloc(4 * 256 + 256);
		for (ch_i = 0; ch_i < 4; ch_i++) {
			u32 p;

			img[ch_i] = t32o_alloc(WORDS * 4);
			for (i = 0; i < WORDS; i++)
				img[ch_i][i] = mine[ch_i][i] = val(rnd());
			p = T32O_PTR(img[ch_i]);
			memcpy(blk + (ch_i << 8) + 200, &p, 4);
		}
		t32_data_da844[0] = T32O_PTR(blk);
		t32_data_d03a0[0] = io.mode;
		for (i = 0; i < 4; i++)
			t32_data_e3130[i] = last[i] = val(rnd());

		r = (u32)t32o_call(F(lib_tisp_csccr_dn_status), ch, rnd(), 0, 0, 0);
		o = t32_lib_csccr_dn_status(&io, last, ch);
		bad = r != o || memcmp(last, t32_data_e3130, sizeof(last)) || t32o_log.n != 0;
		for (i = 0; !bad && i < 4; i++)
			bad = memcmp(img[i], mine[i], WORDS * 4) != 0;
		compared++;
		if (bad && failed++ < 10)
			printf("MISMATCH dn_status(%x) mode %u: ret %x/%x\n", ch, io.mode, r, o);
	}
	t32_data_d03a0[0] = 0;
	printf("t32_dnst_oracle: %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
