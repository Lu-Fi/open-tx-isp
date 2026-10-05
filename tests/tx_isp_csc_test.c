/* Host test for driver/common/tx_isp_csc.h (T10/T20/T21 CSC + front crop). */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "tx_isp_csc.h"

static int failures;

#define CHECK(cond) do { if (!(cond)) { \
	fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
	failures++; } } while (0)

static void attr_for(uint32_t *attr, uint32_t mode, const int32_t *p)
{
	int i;

	memset(attr, 0, TX_ISP_CSC_ATTR_BYTES);
	attr[0] = mode;
	for (i = 0; p && i < TX_ISP_CSC_WORDS; i++)
		attr[1 + i] = (uint32_t)p[i];
}

/* Preset 0 packs to exactly what the OEM T21 tisp_init writes at 0x1700. */
static void test_t21_init_words(void)
{
	uint32_t attr[TX_ISP_CSC_ATTR_WORDS], reg[5];
	int32_t p[TX_ISP_CSC_WORDS];

	attr_for(attr, 0, NULL);
	CHECK(tx_isp_csc_check(attr) == 0);
	tx_isp_csc_params(attr, p);
	tx_isp_csc_tiziano_regs(p, reg);
	CHECK(reg[0] == 0x07596532);
	CHECK(reg[1] == 0x20054cad);
	CHECK(reg[2] == 0x0536b600);
	CHECK(reg[3] == 0x00008000);
	CHECK(reg[4] == 0xff00ff00);
	CHECK(tx_isp_csc_tiziano_mono_clip(reg[4]) == 0xff008080);

	/* TV range: Y 16..235, UV 16..240, offsets 0x10/0x80 */
	attr_for(attr, 1, NULL);
	tx_isp_csc_params(attr, p);
	tx_isp_csc_tiziano_regs(p, reg);
	CHECK(reg[3] == 0x8010);
	CHECK(reg[4] == 0xeb10f010);
	CHECK(tx_isp_csc_tiziano_mono_clip(reg[4]) == 0xeb108080);
}

/* T31 preset 3 converts exactly to the Apical SDK default RGB2YUV table. */
static void test_apical_default_is_preset3(void)
{
	static const uint16_t sdk[12] = {
		47, 157, 16, 32794, 32855, 112, 112, 32870, 32778, 64, 512, 512,
	};
	uint16_t lut[12], clip[4];
	uint32_t attr[TX_ISP_CSC_ATTR_WORDS];
	uint16_t full[4] = { 0, 1023, 0, 1023 };
	int i;

	tx_isp_csc_to_apical(tx_isp_csc_presets[3], lut, clip);
	CHECK(memcmp(lut, sdk, sizeof(sdk)) == 0);
	CHECK(clip[0] == 64 && clip[1] == 940 && clip[2] == 64 && clip[3] == 960);

	tx_isp_csc_to_apical(tx_isp_csc_presets[0], lut, clip);
	CHECK(clip[0] == 0 && clip[1] == 1023 && clip[2] == 0 && clip[3] == 1023);
	CHECK(lut[9] == 0 && lut[10] == 512 && lut[11] == 512);

	/* getter: stock table + stock clip reports mode 3, clip as is */
	tx_isp_csc_from_apical(sdk, full, attr);
	CHECK(attr[0] == 3);
	for (i = 0; i < 11; i++)
		CHECK(attr[1 + i] == (uint32_t)tx_isp_csc_presets[3][i]);
	CHECK(attr[12] == 0 && attr[13] == 255 && attr[14] == 0 && attr[15] == 255);

	/* every preset round-trips through the Apical form */
	for (i = 0; i < 4; i++) {
		tx_isp_csc_to_apical(tx_isp_csc_presets[i], lut, clip);
		tx_isp_csc_from_apical(lut, clip, attr);
		CHECK(attr[0] == (uint32_t)i);
		CHECK(attr[12] == (uint32_t)tx_isp_csc_presets[i][11]);
		CHECK(attr[13] == (uint32_t)tx_isp_csc_presets[i][12]);
		CHECK(attr[14] == (uint32_t)tx_isp_csc_presets[i][13]);
		CHECK(attr[15] == (uint32_t)tx_isp_csc_presets[i][14]);
	}
}

static void test_user_matrix(void)
{
	static const int32_t user[TX_ISP_CSC_WORDS] = {
		0x3ff, -0x3ff, 1, -1, 0, -2, 3, -5, 0x200,
		0x20, 0x80, 0x00, 0xff, 0x40, 0xc0,
	};
	uint32_t attr[TX_ISP_CSC_ATTR_WORDS], back[TX_ISP_CSC_ATTR_WORDS];
	int32_t p[TX_ISP_CSC_WORDS];
	uint16_t lut[12], clip[4];
	int i;

	attr_for(attr, 4, user);
	CHECK(tx_isp_csc_check(attr) == 0);
	tx_isp_csc_params(attr, p);
	CHECK(memcmp(p, user, sizeof(user)) == 0);
	tx_isp_csc_to_apical(p, lut, clip);
	CHECK(lut[0] == 256 && lut[1] == (0x8000 | 256));
	CHECK(lut[2] == 0 && lut[3] == 0);	/* |1|, |-1| round to 0, no -0 */
	CHECK(lut[5] == (0x8000 | 1) && lut[6] == 1 && lut[7] == (0x8000 | 1));
	tx_isp_csc_from_apical(lut, clip, back);
	CHECK(back[0] == 4);
	for (i = 0; i < 9; i++) {
		int32_t d = (int32_t)back[1 + i] - user[i];

		CHECK(d >= -2 && d <= 2);
	}
	CHECK(back[10] == 0x20 && back[11] == 0x80);
	CHECK(back[12] == 0x00 && back[13] == 0xff && back[14] == 0x40 && back[15] == 0xc0);
}

static void test_check(void)
{
	uint32_t attr[TX_ISP_CSC_ATTR_WORDS];
	int32_t p[TX_ISP_CSC_WORDS];

	attr_for(attr, 5, NULL);
	CHECK(tx_isp_csc_check(attr) == -EINVAL);
	attr_for(attr, 0xffffffffu, NULL);
	CHECK(tx_isp_csc_check(attr) == -EINVAL);
	/* presets ignore the user words, whatever they hold */
	attr_for(attr, 2, NULL);
	memset(attr + 1, 0xff, TX_ISP_CSC_WORDS * 4);
	CHECK(tx_isp_csc_check(attr) == 0);

	memcpy(p, tx_isp_csc_presets[1], sizeof(p));
	attr_for(attr, 4, p);
	CHECK(tx_isp_csc_check(attr) == 0);
	attr[1] = 0x400;
	CHECK(tx_isp_csc_check(attr) == -EINVAL);
	attr[1] = (uint32_t)-0x400;
	CHECK(tx_isp_csc_check(attr) == -EINVAL);
	attr_for(attr, 4, p);
	attr[10] = 0x100;
	CHECK(tx_isp_csc_check(attr) == -EINVAL);
	attr_for(attr, 4, p);
	attr[12] = 0xf0;	/* Y min > Y max (0xeb) */
	CHECK(tx_isp_csc_check(attr) == -EINVAL);
	attr_for(attr, 4, p);
	attr[14] = 0xf1;	/* UV min > UV max (0xf0) */
	CHECK(tx_isp_csc_check(attr) == -EINVAL);
}

static void test_fcrop(void)
{
	uint32_t f[TX_ISP_FCROP_WORDS];
	uint32_t g[TX_ISP_FCROP_WORDS];

	/* {enable, top, left, width, height} */
	uint32_t off[5] = { 0, 7, 7, 7, 7 };
	uint32_t off_hi[5] = { 0x100, 1, 1, 1, 1 };	/* only the low byte counts */
	uint32_t full[5] = { 1, 0, 0, 1920, 1080 };
	uint32_t sub[5] = { 1, 100, 200, 960, 540 };
	uint32_t out[5] = { 1, 0, 1000, 1000, 1080 };
	uint32_t zero[5] = { 1, 0, 0, 0, 1080 };
	uint32_t wrap[5] = { 1, 0, 0xffffff00u, 0x200, 1080 };

	CHECK(tx_isp_fcrop_check(off, 1920, 1080) == 0);
	CHECK(tx_isp_fcrop_check(off_hi, 1920, 1080) == 0);
	CHECK(tx_isp_fcrop_check(full, 1920, 1080) == 0);
	CHECK(tx_isp_fcrop_check(full, 0, 0) == -EINVAL);
	CHECK(tx_isp_fcrop_check(sub, 1920, 1080) == -EOPNOTSUPP);
	CHECK(tx_isp_fcrop_check(out, 1920, 1080) == -EINVAL);
	CHECK(tx_isp_fcrop_check(zero, 1920, 1080) == -EINVAL);
	CHECK(tx_isp_fcrop_check(wrap, 1920, 1080) == -EINVAL);

	memcpy(f, full, sizeof(f));
	tx_isp_fcrop_get(1, 1920, 1080, g);
	CHECK(memcmp(f, g, sizeof(f)) == 0);
	tx_isp_fcrop_get(0, 1280, 720, g);
	CHECK(g[0] == 0 && g[1] == 0 && g[2] == 0 && g[3] == 1280 && g[4] == 720);
}

int main(void)
{
	CHECK(TX_ISP_CSC_ATTR_BYTES == 0x40);
	CHECK(TX_ISP_FCROP_BYTES == 0x14);
	test_t21_init_words();
	test_apical_default_is_preset3();
	test_user_matrix();
	test_check();
	test_fcrop();
	if (failures) {
		fprintf(stderr, "tx_isp_csc_test: %d failure(s)\n", failures);
		return 1;
	}
	printf("tx_isp_csc_test: ok\n");
	return 0;
}
