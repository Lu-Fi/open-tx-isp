/* Host test of driver/t31/tx_isp_t31_af.h (T31 AF chain). */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../driver/t31/tx_isp_t31_af.h"

static uint32_t find(uint32_t (*r)[2], unsigned int n, uint32_t reg)
{
	unsigned int i;

	for (i = 0; i < n; i++)
		if (r[i][0] == reg)
			return r[i][1];
	assert(0);
	return 0;
}

static void test_layout_and_regs(void)
{
	static struct t31_af_params p;
	uint32_t r[T31_AF_REGS_MAX][2];
	unsigned int n, i;

	memset(&p, 0, sizeof(p));
	assert(sizeof(p) == 0x260 + 900);       /* bank block, weight at +0x260 */
	assert(T31_AF_BANK_OFFSET + 0x260 == T31_AF_BANK_WEIGHT_OFFSET);
	p.zone[0] = 7; p.zone[1] = 15; p.zone[2] = 1; p.zone[3] = 15;
	t31_af_zone_layout(&p, 1920, 1080);
	for (i = 0; i < 15; i++) {
		assert(p.zone[4 + i] == 126);    /* (1920-15)/15 = 127 -> even */
		assert(p.zone[19 + i] == 70);    /* (1080-3)/15 = 71 -> even */
	}
	for (i = 0; i < 13; i++)
		p.thres[i] = 1;
	p.fir0_v[0] = 0x11; p.fir0_v[1] = 0x22; p.fir0_v[4] = 0x55;
	p.iir1_h[7] = 0x77; p.iir1_h[5] = 0x55;
	p.iir0_ldg[0] = 1; p.iir0_ldg[1] = 2; p.iir0_ldg[2] = 3; p.iir0_ldg[3] = 4;
	p.iir1_cor[3] = 9; p.iir1_cor[2] = 8;
	p.zone[34] = 3; p.zone[35] = 5;
	n = t31_af_hw_regs(&p, 1, r);
	assert(n == 41);
	assert(r[0][0] == 0xb804 && r[0][1] == ((15u << 28) | (1u << 16) | 7u | (15u << 12)));
	assert(find(r, n, 0xb808) == ((126u << 24) | (126u << 16) | 126u | (126u << 8)));
	assert(find(r, n, 0xb814) == ((126u << 16) | (126u << 8) | 126u));
	assert(find(r, n, 0xb824) == ((70u << 16) | (70u << 8) | 70u));
	assert(find(r, n, 0xb828) == ((5u << 16) | (1u << 8) | 3u | (1u << 7) |
				     (1u << 6) | (1u << 5) | (1u << 4)));
	assert(find(r, n, 0xb82c) == ((1u << 28) | (1u << 24) | 1u | (1u << 20) |
				     (1u << 16) | (1u << 12) | (1u << 8) | (1u << 4)));
	assert(find(r, n, 0xb830) == 0x220011u && find(r, n, 0xb838) == 0x55u);
	assert(find(r, n, 0xb860) == 0x770055u);
	assert(find(r, n, 0xb878) == 0x04030201u);
	assert(find(r, n, 0xb8a4) == 0x90008u);
	n = t31_af_hw_regs(&p, 0, r);           /* grid only on the first write */
	assert(n == 32 && r[0][0] == 0xb828);
}

static void test_attr(void)
{
	static struct t31_af_params p;
	uint8_t u[T31_AF_ATTR_BYTES], a[T31_AF_ATTR_BYTES], g[T31_AF_ATTR_BYTES];
	unsigned int i;

	memset(&p, 0, sizeof(p));
	for (i = 0; i < sizeof(u); i++)
		u[i] = (uint8_t)(i + 1);
	u[28] = 0; u[29] = 1; u[30] = 15; u[31] = 9;
	assert(t31_af_hist_from_user(u, a) == 0);
	assert(a[28] == 1 && a[29] == 3 && a[30] == 15 && a[31] == 9);
	assert(a[0] == 0 && a[32] == 0 && a[37] == 0);  /* not taken */
	assert(t31_af_attr_apply(a, &p) == 17);         /* enable = byte 16 */
	assert(p.zone[2] == 1 && p.zone[0] == 3 && p.zone[3] == 15 && p.zone[1] == 9);
	assert(p.tilt[2] == (uint32_t)(19 | (20 << 8)) && p.thres[4] == (uint32_t)(23 | (24 << 8)));
	assert(p.thres[5] == 34 && p.thres[8] == 37);
	assert(p.fir0_ldg[0] == 39 && p.fir0_ldg[2] == (uint32_t)(41 | (42 << 8)) &&
	       p.fir0_ldg[7] == 49);
	assert(p.iir1_ldg[6] == (uint32_t)(83 | (84 << 8)) && p.iir1_ldg[7] == 85);

	p.fv[0] = 0x100; p.fv[1] = 0x200; p.fv[2] = 0x400;
	t31_af_attr_read(&p, 0x800, 1, 2, 0x5a, g);
	assert(g[0] == 0x00 && g[1] == 0x01);           /* fv[2] >> 2 = 0x100 */
	assert(g[4] == 0x00 && g[5] == 0x02);           /* fv_alt >> 2 */
	assert(g[8] == 0x40 && g[12] == 0x80);
	assert(g[16] == 1 && g[17] == 2 && g[32] == 0x5a);
	/* round trip of everything the attribute carries */
	assert(memcmp(g + 18, a + 18, 14) == 0);
	assert(memcmp(g + 33, a + 33, 4) == 0);
	assert(memcmp(g + 38, a + 38, 48) == 0);

	u[30] = 4;                                      /* columns 5..15 */
	assert(t31_af_hist_from_user(u, a) == -EINVAL);
	u[30] = 15; u[31] = 16;
	assert(t31_af_hist_from_user(u, a) == -EINVAL);
}

static void test_weight_and_init(void)
{
	static struct t31_af_params p;
	uint8_t u[T31_AF_ZONES], a[T31_AF_ATTR_BYTES];
	uint32_t w[T31_AF_ZONES];

	memset(u, 8, sizeof(u));
	assert(t31_af_weight_from_user(u, w) == 0 && w[224] == 8);
	u[3] = 9;
	assert(t31_af_weight_from_user(u, w) == -EINVAL);

	memset(&p, 0, sizeof(p));
	p.tilt[0] = 0x101; p.tilt[2] = 0x303; p.thres[4] = 0x404;
	p.zone[0] = 1; p.zone[1] = 15; p.zone[2] = 2; p.zone[3] = 14;
	t31_af_attr_init(&p, a);
	assert(a[24] == 1 && a[25] == 1 && a[18] == 3 && a[22] == 4);
	assert(a[29] == 1 && a[31] == 15 && a[28] == 2 && a[30] == 14 && a[17] == 0);
}

static void test_mult(void)
{
	assert(t31_af_mult(10, 3u << 10, 512) == 3u << 9);   /* 3 * 0.5 */
	assert(t31_af_mult(10, 1024, 1024) == 1024);
	/* q = 0: the stock routine adds all four partial products */
	assert(t31_af_mult(0, 3, 5) == 60);
}

static void test_unpack_and_fv(void)
{
	static uint32_t raw[15 * 15 * 4];
	static struct t31_af_stats st;
	static struct t31_af_result res;
	static struct t31_af_params p;
	unsigned int i;

	/* zone 0: fird0 = 0x123, fird1 = 0x400 (w1 bit 0 << 10), iird0 = 5,
	 * iird1 = 0x3fffff (22 bit), y = 0xabc, high = 0x7ffe */
	raw[0] = 0x123u;
	raw[1] = 0x1u | (5u << 12);
	raw[2] = (0x3fffffu << 2) | (0xbcu << 24);
	raw[3] = 0xau | (0x3fffu << 16) | (2u << 30);
	t31_af_unpack(raw, 15, 15, &st);
	assert(st.fird0[0] == 0x123 && st.fird1[0] == 0x400 && st.iird0[0] == 5);
	assert(st.iird1[0] == 0x3fffff && st.y_sum[0] == 0xabc &&
	       st.high_luma[0] == 0x7ffe);
	assert(st.frame_num == 2);

	/* flat statistics: blend 50/50, value weights 1/1, q = 10 */
	memset(&st, 0, sizeof(st));
	for (i = 0; i < T31_AF_ZONES; i++) {
		st.fird0[i] = 100; st.iird0[i] = 300;
		st.fird1[i] = 40; st.iird1[i] = 60;
	}
	memset(&p, 0, sizeof(p));
	p.zone[1] = 15; p.zone[3] = 15;
	p.tilt[0] = 512; p.tilt[1] = 512; p.tilt[2] = 1024; p.tilt[3] = 1024;
	p.tilt[4] = 10;
	for (i = 0; i < T31_AF_ZONES; i++)
		p.weight[i] = 1;
	p.fv_wmean[14] = 77;
	t31_af_fv_run(&p, &st, &res);
	/* a = 200, b = 50 per zone (Q10); zone value 250 in Q10 like stock;
	 * means 200/50 -> s5 250 */
	assert(res.fv_value[0] == 250u << 10 && res.fv_value[224] == 250u << 10);
	assert(p.fv[0] == 200u * 225 && p.fv[1] == 50u * 225 && p.fv[2] == 250u * 225);
	assert(p.fv_wmean[13] == 77 && p.fv_wmean[14] == 250);
	assert(res.fv_alt == ((200u * 225) >> 3) + ((50u * 225) >> 3));
	/* no weights: means 0, no division by zero */
	memset(p.weight, 0, sizeof(p.weight));
	t31_af_fv_run(&p, &st, &res);
	assert(p.fv[2] == 0 && res.fv_value[0] == 250u << 10);
}

/* the T31 1.1.6 public IMPISPAFHist, as the stock libimp passes it */
struct pub_af_hist {
	uint32_t af_metrics, af_metrics_alt;
	uint8_t af_enable, af_metrics_shift;
	uint16_t af_delta, af_theta, af_hilight_th, af_alpha_alt;
	uint8_t af_hstart, af_vstart, af_stat_nodeh, af_stat_nodev;
	uint8_t af_frame_num;
};
typedef char pub_size_check[sizeof(struct pub_af_hist) == T31_AF_HIST_PUB_BYTES ? 1 : -1];

static void test_pub_hist(void)
{
	static struct t31_af_params p;
	uint8_t arena[4096], g[T31_AF_ATTR_BYTES], full[T31_AF_ATTR_BYTES];
	uint8_t a[T31_AF_ATTR_BYTES], g2[T31_AF_ATTR_BYTES];
	struct pub_af_hist h;
	unsigned int i;

	memset(&p, 0, sizeof(p));
	p.zone[0] = 3; p.zone[1] = 8; p.zone[2] = 1; p.zone[3] = 8;
	p.tilt[0] = 0x13; p.tilt[1] = 0x3a; p.tilt[2] = 0x2a; p.tilt[3] = 0x16;
	p.thres[4] = 200; p.thres[5] = 1; p.fir0_ldg[2] = 0x123;
	p.fv[2] = 0x42c40; p.fv[0] = 1; p.fv[1] = 2;
	t31_af_attr_read(&p, 0xd0ae, 1, 0, 9, g);

	/* the get writes exactly 24 bytes into a 0xA5 arena */
	memset(arena, 0xa5, sizeof(arena));
	t31_af_hist_to_pub(g, arena + 100);
	for (i = 0; i < sizeof(arena); i++)
		if (i < 100 || i >= 100 + T31_AF_HIST_PUB_BYTES)
			assert(arena[i] == 0xa5);
	memcpy(&h, arena + 100, sizeof(h));
	assert(h.af_metrics == 0x42c40 && h.af_metrics_alt == 0xd0ae);
	assert(h.af_enable == 1 && h.af_metrics_shift == 0);
	assert(h.af_delta == 0x2a && h.af_theta == 0x16 &&
	       h.af_hilight_th == 200 && h.af_alpha_alt == 0x13);
	assert(h.af_hstart == 1 && h.af_vstart == 3 && h.af_stat_nodeh == 8 &&
	       h.af_stat_nodev == 8 && h.af_frame_num == 9);

	/* Get -> Set round trip: accepted, nothing changes */
	t31_af_hist_from_pub(arena + 100, g, full);
	assert(t31_af_hist_from_user(full, a) == 0);
	assert(t31_af_attr_apply(a, &p) == 1);
	t31_af_attr_read(&p, 0xd0ae, 1, 0, 9, g2);
	assert(memcmp(g + 16, g2 + 16, T31_AF_ATTR_BYTES - 16) == 0);
	assert(p.tilt[1] == 0x3a && p.thres[5] == 1 && p.fir0_ldg[2] == 0x123);

	/* nodeh 16 is rejected */
	arena[100 + 20] = 16;
	t31_af_hist_from_pub(arena + 100, g, full);
	assert(t31_af_hist_from_user(full, a) == -EINVAL);
}

int main(void)
{
	test_pub_hist();
	test_mult();
	test_layout_and_regs();
	test_attr();
	test_weight_and_init();
	test_unpack_and_fv();
	puts("tx_isp_t31_af_test: ok");
	return 0;
}
