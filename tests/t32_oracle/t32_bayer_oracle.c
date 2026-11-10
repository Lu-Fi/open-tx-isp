// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our tisp_reg_read and Bayer-order API (driver/t32/tx_isp_t32_bayer.h) against the vendor
 * tisp_reg_read, tisp_api_get_bayer_type / _set_bayer_type and lib_tisp_api_get_bayer / _set_bayer from Matt
 * Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc; tisp_reg_write = ref/oa_reg_write.c).
 *
 * Compared bit-exact: return value (low word), every register write (address, value, order; shadow writes
 * tagged with the channel), the error print (count, line argument), the shadow images (unchanged).  Inputs:
 * random register contents, channels 0..3, 4..255 and high garbage bytes, Bayer words, mode byte 0 / 1 / 2.
 *
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_bayer.h"

u64 tisp_reg_read(u32, u32, u32, u32);
u64 tisp_api_get_bayer_type(u32, u32, u32, u32);
u64 tisp_api_set_bayer_type(u32, u32, u32, u32);
u64 lib_tisp_api_get_bayer(u32, u32, u32, u32);
u64 lib_tisp_api_set_bayer(u32, u32, u32, u32);

#define F(fn)	((t32o_fn)(void (*)(void))(fn))
#define WORDS	64

static u32 rs = 0x3b9e17u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static u32 regs[0x100], *img[4], mine[4][WORDS], pr_n, pr_line;

/* the vendor's isp_printf: count and the line argument (a3), return a marker */
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a1; (void)a2;
	pr_n++;
	pr_line = a3;
	return 0x7700 + a3;
}

static u32 rd(u32 reg)
{
	return regs[reg & 0xff];
}

/* our hooks */
static struct { u32 reg, val; } ow[8];
static unsigned int on, o_err, o_line;
static u32 h_read(void *ctx, u32 reg) { (void)ctx; return rd(reg); }
static u32 *h_shadow(void *ctx, u32 ch) { (void)ctx; return mine[ch]; }
static u32 h_write(void *ctx, u32 ch, u32 reg, u32 val)
{
	unsigned char mode = *(unsigned char *)ctx;

	ow[on].reg = mode == 1 ? 0x80000000u | (ch & 7) << 24 | (reg & 0xffffff) : reg;
	ow[on++].val = val;
	return 0;
}
static u32 h_error(void *ctx, u32 line) { (void)ctx; o_err++; o_line = line; return 0x7700 + line; }

static unsigned int compared, failed;

static void arm(unsigned char mode)
{
	u8 *blk;
	unsigned int ch, i;
	u32 p;

	t32o_reset();
	pr_n = pr_line = 0;
	blk = t32o_alloc(4 * 256 + 256);
	for (ch = 0; ch < 4; ch++) {
		img[ch] = t32o_alloc(WORDS * 4);
		for (i = 0; i < WORDS; i++)
			img[ch][i] = mine[ch][i] = rnd();
		p = T32O_PTR(img[ch]);
		memcpy(blk + (ch << 8) + 200, &p, 4);
	}
	t32_data_da844[0] = T32O_PTR(blk);
	t32_data_d03a0[0] = mode;
	on = o_err = o_line = 0;
}

static void cmp(const char *what, u32 vr, u32 or_, u32 a0, u32 a1)
{
	unsigned int i, bad = vr != or_ || t32o_log.n != on || pr_n != o_err || pr_line != o_line;

	for (i = 0; !bad && i < on; i++)
		bad = t32o_log.w[i].reg != ow[i].reg || t32o_log.w[i].val != ow[i].val;
	for (i = 0; !bad && i < 4; i++)
		bad = memcmp(img[i], mine[i], WORDS * 4) != 0;
	compared++;
	if (bad && failed++ < 10)
		printf("MISMATCH %s(%x, %x) mode %u: ret %x/%x writes %u/%u prints %u/%u\n", what, a0, a1,
		       t32_data_d03a0[0], vr, or_, t32o_log.n, on, pr_n, o_err);
}

int main(void)
{
	unsigned int c, i, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 20000;
	unsigned char mode;
	struct t32_regio io = { &mode, 0, h_read, h_shadow, h_write, h_error };

	t32o_rd_hook = rd;
	for (c = 0; c < n; c++) {
		u32 ch = c % 3 ? rnd() % 4 : (c % 2 ? rnd() % 256 : rnd()), b = rnd(), reg = rnd() % WORDS, r;

		if (c % 5 == 0)
			ch = (ch & 3) | (rnd() & 0xffffff00u);	/* high garbage, low byte valid */
		mode = c % 4 == 3 ? 2 : c % 2;
		io.mode = mode;
		for (i = 0; i < 0x100; i++)
			regs[i] = rnd();
		if (c % 4 == 0)
			regs[0x78] &= 0xffffff00u;

		arm(mode);
		r = (u32)t32o_call(F(tisp_reg_read), ch & 3, reg, 0, 0, 0);
		cmp("tisp_reg_read", r, t32_tisp_reg_read(&io, ch & 3, reg), ch & 3, reg);
		arm(mode);
		r = (u32)t32o_call(F(tisp_api_get_bayer_type), ch, b, 0, 0, 0);
		cmp("get_bayer_type", r, t32_api_get_bayer_type(&io, ch), ch, b);
		arm(mode);
		r = (u32)t32o_call(F(lib_tisp_api_get_bayer), ch, b, 0, 0, 0);
		cmp("lib_get_bayer", r, t32_api_get_bayer_type(&io, ch), ch, b);
		arm(mode);
		r = (u32)t32o_call(F(tisp_api_set_bayer_type), ch, b, 0, 0, 0);
		cmp("set_bayer_type", r, t32_api_set_bayer_type(&io, ch, b), ch, b);
		arm(mode);
		r = (u32)t32o_call(F(lib_tisp_api_set_bayer), ch, b, 0, 0, 0);
		cmp("lib_set_bayer", r, t32_api_set_bayer_type(&io, ch, b), ch, b);
	}
	t32_data_d03a0[0] = 0;
	printf("t32_bayer_oracle: %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
