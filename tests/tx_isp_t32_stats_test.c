/* Host test: T32 AE/AWB statistics initialisation (driver/t32/tx_isp_t32_stats.h).
 * With T32_IQ_BIN (cv8001-PRJ007-day.bin) the writes are compared with the vendor's
 * (QEMU run v1, AddSensor, 3840x2160; buffer addresses as the vendor's). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../driver/t32/tx_isp_t32_stats.h"

static int bad;
#define CHECK(c) do { if (!(c)) { bad++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static const struct t32_stats_write ae_vendor[] = {
	{ 0x17068, 0x02d40000 }, { 0x1706c, 0x02d41000 }, { 0x17070, 0x02d42000 }, { 0x17074, 0x02d43000 },
	{ 0x17078, 0x02d44000 }, { 0x1707c, 0x02d44800 }, { 0x17080, 0x02d45000 }, { 0x17084, 0x02d45800 },
	{ 0x17088, 0x10010033 }, { 0x17040, 0xf001f001 }, { 0x17044, 0x80808080 }, { 0x17048, 0x80808080 },
	{ 0x1704c, 0x80808080 }, { 0x17050, 0x00808080 }, { 0x17054, 0x48484848 }, { 0x17058, 0x48484848 },
	{ 0x1705c, 0x48484848 }, { 0x17060, 0x00484848 }, { 0x17064, 0x0011aa1e }, { 0x17090, 0x096005a0 },
	{ 0x17094, 0x0546032a }, { 0x17010, 1 }, { 0x17010, 1 }, { 0x17004, 0x0134da11 },
};
static const struct t32_stats_write awb_vendor[] = {
	{ 0x16078, 0x02c80000 }, { 0x1607c, 0x02c87000 }, { 0x16080, 0x02c8e000 }, { 0x16084, 0x02c95000 },
	{ 0x16088, 3 }, { 0x16040, 0x00010001 }, { 0x16044, 0x78787878 }, { 0x16048, 0x78787878 },
	{ 0x1604c, 0x78787878 }, { 0x16050, 0x10787878 }, { 0x16054, 0x48484848 }, { 0x16058, 0x48484848 },
	{ 0x1605c, 0x48484848 }, { 0x16060, 0x0f484848 }, { 0x16088, 0x01010103 }, { 0x160f4, 0x00480078 },
	{ 0x16064, 0x00f0003c }, { 0x16068, 0x00e60032 }, { 0x1606c, 0x00b40100 }, { 0x16070, 0x015e0100 },
	{ 0x16010, 1 }, { 0x160f0, 0x00080800 }, { 0x16074, 0x01e6801e }, { 0x160ec, 0 }, { 0x160f8, 0x00080800 },
	{ 0x16010, 1 },
};

static void compare(const char *what, const struct t32_stats_write *got, unsigned int n,
		    const struct t32_stats_write *want, unsigned int m)
{
	unsigned int i, diff = 0;

	CHECK(n == m);
	for (i = 0; i < n && i < m; i++)
		if (got[i].reg != want[i].reg || got[i].val != want[i].val) {
			diff++;
			printf("  %s #%u: %05x=%08x, vendor %05x=%08x\n", what, i, got[i].reg, got[i].val,
			       want[i].reg, want[i].val);
		}
	CHECK(diff == 0);
	printf("  %s: %u writes, %u differ\n", what, n, diff);
}

/* the histogram as the engine packs it: per 64-bit word two 24-bit bins */
static void hist_put(u8 *h, unsigned int i, u32 v)
{
	unsigned long long *q = (unsigned long long *)(h + i / 2 * 8);

	*q |= (unsigned long long)(v & 0xffffff) << (i & 1) * 24;
}

static void bringup_tests(void)
{
	static u8 h[T32_AE_HIST_BINS * 4];
	unsigned int i;

	memset(h, 0, sizeof(h));
	CHECK(t32_ae_hist_mean16(h) == 0);
	for (i = 0; i < T32_AE_HIST_BINS; i++)
		hist_put(h, i, i * 0x10101 & 0xffffff);
	for (i = 0; i < T32_AE_HIST_BINS; i++)
		CHECK(t32_ae_hist_bin(h, i) == (i * 0x10101 & 0xffffff));
	memset(h, 0, sizeof(h));
	hist_put(h, 32, 1000);
	hist_put(h, 96, 1000);
	CHECK(t32_ae_hist_mean16(h) == 64 * 16);
	hist_put(h, 255, 0xffffff);
	CHECK(t32_ae_hist_bin(h, 255) == 0xffffff && t32_ae_hist_bin(h, 254) == 0);
	/* proportional, limited per step and by the frame length */
	CHECK(t32_bringup_ae_step(1000, 32 * 16, 64 * 16, 2000) == 2000);
	CHECK(t32_bringup_ae_step(1000, 128 * 16, 64 * 16, 2000) == 500);
	CHECK(t32_bringup_ae_step(1000, 255 * 16, 1 * 16, 2000) == 250);
	CHECK(t32_bringup_ae_step(100, 1, 64 * 16, 5000) == 400);
	CHECK(t32_bringup_ae_step(1000, 0, 64 * 16, 2000) == 1000);
	CHECK(t32_bringup_ae_step(2, 255 * 16, 16, 2000) == 1);
}

int main(void)
{
	static const u8 t[22] = { 10, 0, 20, 0, 40, 0, 40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 99, 0 };
	const char *path = getenv("T32_IQ_BIN");
	struct t32_stats_write w[T32_STATS_MAX_WRITES];
	FILE *f = path ? fopen(path, "rb") : NULL;

	/* tisp_simple_intp_uint16 */
	CHECK(t32_intp_u16(0, 0x8000, t) == 15 && t32_intp_u16(2, 0x1234, t) == 40);
	CHECK(t32_intp_u16(3, 0x8000, t) == 20 && t32_intp_u16(11, 0, t) == 99);
	bringup_tests();
	if (f) {
		static u8 bin[0x40000];
		size_t len = fread(bin, 1, sizeof(bin), f);
		unsigned int n;

		fclose(f);
		CHECK(len > T32_AWB_PARAM + 5000);
		n = t32_ae_stats_init(bin, 3840, 2160, 0x02d40000, w);
		compare("AE", w, n, ae_vendor, sizeof(ae_vendor) / sizeof(ae_vendor[0]));
		n = t32_awb_stats_init(bin, 0x02c80000, T32_AWB_INIT_EV, w);
		compare("AWB", w, n, awb_vendor, sizeof(awb_vendor) / sizeof(awb_vendor[0]));
	} else {
		printf("  (no T32_IQ_BIN: vendor comparison skipped)\n");
	}
	printf("tx_isp_t32_stats_test: %s\n", bad ? "FAIL" : "PASS");
	return bad != 0;
}
