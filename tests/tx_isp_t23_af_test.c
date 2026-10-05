/*
 * Host test of the T23 AF chain (driver/t23/tx_isp_t23_af.h on the shared
 * driver/t31/tx_isp_t31_af.h data handling).
 *
 * The golden values were produced by running the STOCK tx-isp-t23.ko
 * (md5 8237acb18a548d8ad8c2d3fcf1517724) in the MIPS emulator
 * (driver/t23/audit/memu.py): tiziano_af_init, af_interrupt_static twice,
 * apical_isp_core_ops_s_ctrl/g_ctrl 0x8000042 and 0x8000044, with the inputs
 * generated below (LCG 1664525 / 1013904223, seed 0x12345678, value = x >> 8).
 * Hashes are FNV-1a over 32-bit words.
 */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../driver/t23/tx_isp_t23_af.h"

static uint32_t lcg = 0x12345678U;

static uint32_t rnd(void)
{
	lcg = lcg * 1664525U + 1013904223U;
	return lcg >> 8;
}

static uint32_t fnv(const uint32_t *w, unsigned int n)
{
	uint32_t h = 2166136261U;
	unsigned int i;

	for (i = 0; i < n; i++)
		h = (h ^ w[i]) * 16777619U;
	return h;
}

static uint32_t fnv_regs(uint32_t (*r)[2], unsigned int n, unsigned int *kept)
{
	uint32_t w[2 * T31_AF_REGS_MAX];
	unsigned int i, k = 0;

	for (i = 0; i < n; i++) {
		if (r[i][0] == 0xb800U)
			continue;
		w[k++] = r[i][0];
		w[k++] = r[i][1];
	}
	*kept = k / 2;
	return fnv(w, k);
}

static void test_places(void)
{
	/* stock tiziano_af_params_refresh: tparams + 0x27c74 .. weight + 900 */
	assert(T23_AF_ACTIVE_OFFSET == 0x27c74U);
	assert(T23_AF_ACTIVE_WEIGHT_OFFSET == 0x27ed4U);
	assert(T23_AF_ACTIVE_WEIGHT_OFFSET - T23_AF_ACTIVE_OFFSET == 0x260U);
	/* tisp_s_af_weight: day/night bank + 0x14dd4 */
	assert(T23_AF_BANK_WEIGHT_OFFSET == 0x14dd4U);
	assert(T23_AF_BANK_OFFSET == 0x14b74U);
	assert(T23_AF_BANK_WEIGHT_OFFSET + 900U <= 0x15844U); /* bank size */
	assert(sizeof(struct t31_af_params) == 1508U);
	assert(T23_AF_IRQ_BIT == 31U);
	assert(T23_AF_STAT_BANK_REG == 0xb8bcU && T23_AF_STAT_PAGES == 4U);
	assert(t23_af_bank_ok(0) && t23_af_bank_ok(3));
	assert(!t23_af_bank_ok(4) && !t23_af_bank_ok(0xffffffffU));
}

static struct t31_af_params p;
static struct t31_af_stats st;
static struct t31_af_result res;

static void test_stock_vectors(void)
{
	uint32_t w[152 + 225], stats[1024], regs[T31_AF_REGS_MAX][2];
	uint32_t last[T31_AF_ZONES], k[2];
	uint8_t u[T31_AF_ATTR_BYTES], a[T31_AF_ATTR_BYTES], g[T31_AF_ATTR_BYTES];
	unsigned int i, n, kept;
	uint32_t pw[149], aw[22];

	for (i = 0; i < 152; i++)
		w[i] = rnd() % 0x400U;
	w[1] = 5U + rnd() % 11U;
	w[3] = 5U + rnd() % 11U;
	for (i = 36; i < 49; i++)
		w[i] = rnd() % 2U;
	w[129] = rnd() % 1025U;
	w[130] = rnd() % 1025U;
	w[131] = rnd() % 2048U;
	w[132] = rnd() % 2048U;
	w[133] = 10;
	for (i = 0; i < 225; i++)
		w[152 + i] = rnd() % 9U;
	for (i = 0; i < 1024; i++) {
		uint32_t hi = rnd();

		stats[i] = (hi << 8) | (rnd() >> 8);
	}
	memcpy(&p, w, sizeof(p));

	/* tiziano_af_init(1080, 1920): grid layout and the register words */
	t31_af_zone_layout(&p, 1920, 1080);
	n = t31_af_hw_regs(&p, 1, regs);
	assert(fnv_regs(regs, n, &kept) == 0xce0d053dU && kept == 41);

	/* af_interrupt_static, bank 2, twice (the weighted means shift) */
	t31_af_unpack(stats, t31_af_rows(&p), t31_af_cols(&p), &st);
	t31_af_fv_run(&p, &st, &res);
	memcpy(last, res.fv_value, sizeof(last));
	assert(fnv(last, T31_AF_ZONES) == 0x5065d77fU);
	assert(p.fv[0] == 283461620U && p.fv[1] == 305694220U &&
	       p.fv[2] == 261951170U && res.fv_alt == 3230836U);
	assert(fnv(p.fv_wmean, 15) == 0x60442c8dU);
	assert(st.frame_num == 7);
	t31_af_unpack(stats, t31_af_rows(&p), t31_af_cols(&p), &st);
	t31_af_fv_run(&p, &st, &res);
	assert(fnv(p.fv_wmean, 15) == 0x68e8e801U);

	/* SetAfHist (control 0x8000042): conversion, apply, registers */
	for (i = 0; i < T31_AF_ATTR_BYTES; i++)
		u[i] = (uint8_t)(rnd() & 0xffU);
	u[30] = 8;
	u[31] = 9;
	u[28] = 0;
	u[29] = 1;
	assert(t31_af_hist_from_user(u, a) == 0);
	k[0] = t31_af_attr_apply(a, &p);
	assert(k[0] == 252);			/* AF_Enable = attribute byte 16 */
	t31_af_zone_layout(&p, 1920, 1080);
	n = t31_af_hw_regs(&p, 1, regs);	/* af_first cleared by the set */
	assert(fnv_regs(regs, n, &kept) == 0x3cd6a478U && kept == 41);
	memcpy(pw, &p, sizeof(pw));
	assert(fnv(pw, 149) == 0xdf12a08dU);
	memcpy(aw, a, sizeof(aw));
	assert(fnv(aw, 22) == 0x80054a19U);

	/*
	 * GetAfHist (0x8000042).  Stock copies its stack buffer out, the bytes
	 * it does not write (37, 45, 47 ...) carry stack leftovers; only the
	 * written bytes are compared, this driver returns 0 for the others.
	 */
	t31_af_attr_read(&p, res.fv_alt, (uint8_t)k[0], a[17], st.frame_num, g);
	{
		static const uint8_t base[4] = { 38, 50, 62, 74 };
		static const uint8_t off[10] = { 0, 1, 2, 3, 4, 5, 6, 8, 9, 10 };
		uint8_t m[T31_AF_ATTR_BYTES] = { 0 };
		unsigned int b, o;

		for (i = 0; i < 37; i++)
			m[i] = g[i];
		for (b = 0; b < 4; b++)
			for (o = 0; o < 10; o++)
				m[base[b] + off[o]] = g[base[b] + off[o]];
		memcpy(aw, m, sizeof(aw));
		assert(fnv(aw, 22) == 0xcd65af02U);
		/* what this driver returns has no byte outside the written set */
		for (i = 0; i < T31_AF_ATTR_BYTES; i++)
			assert(m[i] == g[i]);
	}

	/* SetAfWeight (0x8000044): 225 bytes 0..8 to the u32 table */
	{
		uint8_t wt[T31_AF_ZONES];
		uint32_t tab[T31_AF_ZONES];

		for (i = 0; i < T31_AF_ZONES; i++)
			wt[i] = (uint8_t)(rnd() % 9U);
		assert(t31_af_weight_from_user(wt, tab) == 0);
		assert(fnv(tab, T31_AF_ZONES) == 0x2c3ee140U);
		wt[100] = 9;
		assert(t31_af_weight_from_user(wt, tab) == -EINVAL);
	}
}

int main(void)
{
	test_places();
	test_stock_vectors();
	printf("tx_isp_t23_af_test: ok\n");
	return 0;
}
