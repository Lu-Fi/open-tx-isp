/*
 * Host test of driver/t23/tx_isp_t23_tuning_ext.h: the data handling of the
 * T23 stock tuning controls the recovered driver did not route before.
 * Expected values follow the stock libt23-firmware-1.3.0 code paths quoted
 * in the header.
 */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../driver/t23/tx_isp_t23_tuning_ext.h"

static void test_weights(void)
{
	uint8_t in[T23X_ZONES], back[T23X_ZONES];
	uint32_t w[T23X_ZONES], roui[T23X_ZONES];
	unsigned int i;

	for (i = 0; i < T23X_ZONES; i++)
		in[i] = (uint8_t)(i % 9U);
	assert(t23x_weight_expand(in, w) == 0);
	for (i = 0; i < T23X_ZONES; i++)
		assert(w[i] == i % 9U);
	t23x_roui_from_roi(w, roui);
	for (i = 0; i < T23X_ZONES; i++)
		assert(roui[i] == 8U - i % 9U);
	t23x_weight_pack(w, back);
	assert(memcmp(in, back, sizeof(in)) == 0);

	/* one entry above 8 rejects the whole table, nothing written */
	memset(w, 0xa5, sizeof(w));
	in[200] = 9;
	assert(t23x_weight_expand(in, w) == -EINVAL);
	assert(w[0] == 0xa5a5a5a5U);
}

static void test_ae_manual(void)
{
	uint32_t c[T23X_AE_CTRLS_WORDS], w[T23X_AE_CTRLS_WORDS];

	/* not frozen: only the enabled values are taken */
	memset(c, 0, sizeof(c));
	c[1] = 2000; c[2] = 3000; c[3] = 400; c[4] = 5000;
	memset(w, 0, sizeof(w));
	w[15] = 1; w[3] = 123;          /* it manual */
	w[16] = 0; w[1] = 7777;         /* again auto: ignored */
	w[38] = 1; w[2] = 10;           /* dgain manual, raised to 1x */
	w[17] = 1; w[4] = 4096;
	t23x_ae_manual_set(c, w);
	assert(c[0] == 0 && c[15] == 1 && c[3] == 123);
	assert(c[16] == 0 && c[1] == 2000);
	assert(c[38] == 1 && c[2] == 1024);
	assert(c[17] == 1 && c[4] == 4096);

	/* frozen: all four taken, gains at least 1x, enables untouched */
	memset(w, 0, sizeof(w));
	w[0] = 1; w[1] = 5; w[2] = 2048; w[3] = 77; w[4] = 0;
	c[15] = 9;
	t23x_ae_manual_set(c, w);
	assert(c[0] == 1 && c[1] == 1024 && c[2] == 2048 && c[3] == 77 &&
	       c[4] == 1024 && c[15] == 9);

	/* short frame frozen */
	memset(w, 0, sizeof(w));
	w[26] = 1; w[20] = 1; w[21] = 55; w[27] = 3000; w[29] = 0;
	t23x_ae_manual_set(c, w);
	assert(c[26] == 1 && c[20] == 1024 && c[21] == 55 && c[27] == 3000 &&
	       c[29] == 1024);
}

static void test_freeze_expr(void)
{
	uint32_t c[T23X_AE_CTRLS_WORDS];

	memset(c, 0, sizeof(c));
	c[1] = 1500; c[2] = 1024; c[3] = 300; c[4] = 1024;
	assert(t23x_ae_freeze(c, 2) == -EINVAL);
	assert(c[0] == 0);
	assert(t23x_ae_freeze(c, 1) == 0);
	assert(c[0] == 1 && c[26] == 1 && c[3] == 300 && c[1] == 1500);
	assert(t23x_ae_freeze(c, 0) == 0);
	assert(c[0] == 0 && c[26] == 0);

	/* manual, lines */
	assert(t23x_expr_set(c, 1, 0, 600, 30) == 0);
	assert(c[15] == 1 && c[3] == 600);
	/* manual, us: divided by the line time */
	assert(t23x_expr_set(c, 1, 1, 3000, 30) == 0);
	assert(c[3] == 100);
	assert(t23x_expr_set(c, 1, 1, 3000, 0) == -EINVAL);
	assert(t23x_expr_set(c, 1, 2, 3000, 30) == -EINVAL);
	/* auto: enable cleared, it kept */
	assert(t23x_expr_set(c, 0, 0, 1, 30) == 0);
	assert(c[15] == 0 && c[3] == 100);
	assert(t23x_expr_set(c, 2, 0, 1, 30) == -EINVAL);
}

static void test_hist_state_min(void)
{
	uint8_t h[T23X_AE_HIST_BYTES], out[16], st[12];
	uint32_t stat[5] = { 0x42, 1, 0x80, 0, 0 };
	uint32_t th[20], wdr[10];
	unsigned int i;

	memset(h, 0, sizeof(h));
	h[1044] = 13; h[1045] = 64; h[1046] = 144; h[1047] = 192;
	for (i = 0; i < 5U; i++) {
		uint32_t v = 0x10000U + 0x101U * (i + 1U);

		memcpy(h + 1024 + 4 * i, &v, 4);
	}
	h[1048] = 15; h[1049] = 14;
	t23x_ae_hist_pack(h, out);
	assert(out[0] == 13 && out[1] == 64 && out[2] == 144 && out[3] == 192);
	assert(out[4] == 0x01 && out[5] == 0x01);       /* u16 truncation */
	assert(out[12] == 0x05 && out[13] == 0x05);
	assert(out[14] == 15 && out[15] == 14);

	t23x_ae_state_pack(stat, st);
	assert(st[0] == 1 && st[1] == 0 && st[4] == 0x42 && st[8] == 0x80);
	stat[1] = 0;
	t23x_ae_state_pack(stat, st);
	assert(st[0] == 0);

	memset(th, 0, sizeof(th));
	memset(wdr, 0, sizeof(wdr));
	th[0] = 1000; th[1] = 0x445c;
	assert(t23x_ae_min_set(th, wdr, 10, 2048) == 3U);
	assert(th[4] == 10 && th[5] == 2048 && wdr[0] == 1 && wdr[1] == 1);
	memset(wdr, 0, sizeof(wdr));
	assert(t23x_ae_min_set(th, wdr, 0, 512) == 0U);    /* both rejected */
	assert(th[4] == 10 && th[5] == 2048 && !wdr[0] && !wdr[1]);
	assert(t23x_ae_min_set(th, wdr, 1001, 0x445d) == 0U);
}

static void test_awb(void)
{
	static uint32_t r[T23X_ZONES], g[T23X_ZONES], b[T23X_ZONES],
		pix[T23X_ZONES];
	uint8_t out[T23X_AWB_ZONE_BYTES];
	uint32_t custom = 0;

	r[0] = 1000; g[0] = 2000; b[0] = 300; pix[0] = 10;
	r[1] = 100000; g[1] = 0; b[1] = 0; pix[1] = 10;  /* 10000: byte wraps */
	r[224] = 5; pix[224] = 0;                        /* empty zone */
	t23x_awb_zone_pack(r, g, b, pix, out);
	assert(out[0] == 100 && out[225] == 200 && out[450] == 30);
	assert(out[1] == (uint8_t)10000);
	assert(out[224] == 0 && out[449] == 0 && out[674] == 0);

	/* auto: follows the measurement */
	assert(t23x_awb_ct_select(0, 4800, &custom) == 4800 && custom == 4800);
	/* manual: the set value wins, 0 keeps the measured one */
	custom = 6500;
	assert(t23x_awb_ct_select(1, 4800, &custom) == 6500 && custom == 6500);
	custom = 0;
	assert(t23x_awb_ct_select(1, 4800, &custom) == 4800 && custom == 0);
	/* presets: untouched */
	custom = 3000;
	assert(t23x_awb_ct_select(3, 4800, &custom) == 4800 && custom == 3000);
}

int main(void)
{
	test_awb();
	test_weights();
	test_ae_manual();
	test_freeze_expr();
	test_hist_state_min();
	printf("tx_isp_t23_tuning_ext_test: ok\n");
	return 0;
}
