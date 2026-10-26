/* Host test: T21 legacy integration-time block on 0x800002c
 * (tx_isp_t21_tuning_ctl.h, issue openimp#110).  The set blocks are built
 * as the T20 3.12.0 / T21 1.0.33 libimp (and OpenIMP) send them; the get
 * fill must give GetIntegrationTime its mode/time/maximum back and leave
 * the sinter bytes alone. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#define T21_TUNING_CTL_HOST_TEST
#include "../driver/t21/tx_isp_t21_tuning_ctl.h"

static void it_set_block(uint8_t *ae, int mode, uint16_t it, uint16_t max)
{
	memset(ae, 0, T21_SINTER_BLOCK);
	switch (mode) {
	case 0:
		ae[0x3f] = 1;
		break;
	case 1:
		ae[0x03] = 1;
		ae[0x3f] = 1;
		ae[0x51] = 1;
		memcpy(ae + 0x18, &it, 2);
		break;
	case 2:
		ae[0x52] = 1;
		memcpy(ae + 0x1a, &max, 2);
		break;
	}
}

int main(void)
{
	uint8_t b[T21_SINTER_BLOCK];
	uint16_t it, max;
	int bad = 0, i;

	it_set_block(b, 0, 0, 0);
	bad += t21_it_block_mode(b) != T21_IT_AUTO;
	it_set_block(b, 1, 30, 0);
	bad += t21_it_block_mode(b) != T21_IT_MANUAL;
	bad += t21_it_block_u16(b, T21_IT_B_TIME) != 30;
	it_set_block(b, 1, 0x1ff, 0);
	bad += t21_it_block_u16(b, T21_IT_B_TIME) != 0x1ff;
	it_set_block(b, 2, 0, 1200);
	bad += t21_it_block_mode(b) != T21_IT_RANGE;
	bad += t21_it_block_u16(b, T21_IT_B_MAX) != 1200;

	/* sinter set blocks (AUTO and MANUAL) and an empty block are no IT block */
	memset(b, 0, sizeof(b));
	bad += t21_it_block_mode(b) != T21_IT_NONE;
	b[T21_SINTER_B_VALID] = 1;
	bad += t21_it_block_mode(b) != T21_IT_NONE;
	b[T21_SINTER_B_MANUAL] = b[T21_SINTER_B_MANUAL2] = 1;
	b[T21_SINTER_B_STRENGTH] = 200;
	bad += t21_it_block_mode(b) != T21_IT_NONE;

	/* get: the bytes GetIntegrationTime reads, nothing else */
	for (i = 0; i < (int)sizeof(b); i++)
		b[i] = 0xa5;
	t21_it_block_fill(b, 1, 30, 1123);
	bad += b[0x03] != 1;
	memcpy(&it, b + 0x18, 2);
	memcpy(&max, b + 0x1a, 2);
	bad += it != 30 || max != 1123;
	for (i = 0; i < (int)sizeof(b); i++)
		if (i != 0x03 && (i < 0x18 || i > 0x1b))
			bad += b[i] != 0xa5;
	t21_it_block_fill(b, 0, 0x12345, 0x10000);
	memcpy(&it, b + 0x18, 2);
	memcpy(&max, b + 0x1a, 2);
	bad += b[0x03] != 0 || it != 0xffff || max != 0xffff;

	if (bad) {
		printf("t21_it_block_host_test: FAIL (%d)\n", bad);
		return 1;
	}
	printf("t21_it_block_host_test: PASS\n");
	return 0;
}
