/* Host test of driver/t31/tx_isp_t31_linetime.h (openimp #733: stock ispcore_sync_sensor_attr line time). */
#include <assert.h>
#include <stdio.h>

#include "../driver/t31/tx_isp_t31_linetime.h"

/* the stock arithmetic as decompiled (0x7757c): u32 product, two unsigned divisions, u16 store */
static u16 stock(u32 fps, u32 th)
{
	return (u16)((((fps & 0xffff) * 1000000U) / (fps >> 16)) / (th & 0xffff));
}

int main(void)
{
	static const u32 fps[] = { 25 << 16 | 1, 30 << 16 | 1, 15 << 16 | 1, 20 << 16 | 1, 30000 << 16 | 1001,
				   1 << 16 | 1, 60 << 16 | 1, 5 << 16 | 2, 0xffff0001 };
	static const u32 th[] = { 1125, 1500, 2250, 1320, 1, 0xffff, 1080, 805 };
	unsigned int i, j;

	for (i = 0; i < sizeof(fps) / sizeof(fps[0]); i++)
		for (j = 0; j < sizeof(th) / sizeof(th[0]); j++)
			assert(t31_sync_line_time(fps[i], th[j]) == stock(fps[i], th[j]));
	/* gc2053 1080p25 VTS 1125: 35 us; sc2336 30 fps VTS 1500: 22 us */
	assert(t31_sync_line_time(25 << 16 | 1, 1125) == 35);
	assert(t31_sync_line_time(30 << 16 | 1, 1500) == 22);
	/* the u32 product wraps as the stock code's (denominator 5000: 5e9 mod 2^32) */
	assert(t31_sync_line_time(1 << 16 | 5000, 1) == stock(1 << 16 | 5000, 1));
	/* the stock code would trap: the caller keeps the field */
	assert(t31_sync_line_time(0 << 16 | 1, 1125) == 0);
	assert(t31_sync_line_time(25 << 16 | 1, 0) == 0);
	printf("tx_isp_t31_linetime_test: ok\n");
	return 0;
}
