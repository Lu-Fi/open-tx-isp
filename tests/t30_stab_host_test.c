/* Host test: T30 SYSTEM_TAB block (0x800002c, tx_isp_t30_stab.h).  Only the
 * items whose enable byte (0x3c + item) is set reach the system table, with
 * their full width; the MoveState day block of the libimp (T20/T30 vendor
 * logic) sets the exposure cap, manual sinter/temper and the move.txt limits. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../driver/t30/tx_isp_t30_stab.h"

int main(void)
{
	uint8_t stab[T30_STAB_BYTES], ref[T30_STAB_BYTES], blk[T30_STAB_BLOCK];
	uint16_t it = 0x2a1, v16;
	uint32_t v32 = 0x11223344;
	int bad = 0, i, len = 0;

	for (i = 0; i < T30_STAB_ITEMS; i++)
		len += t30_stab_items[i].len;
	bad += len != 59;	/* 60 bytes, one pad byte (55) */
	for (i = 1; i < T30_STAB_ITEMS; i++)
		bad += t30_stab_items[i].off < t30_stab_items[i - 1].off + t30_stab_items[i - 1].len;

	/* no enable byte: nothing changes */
	for (i = 0; i < T30_STAB_BYTES; i++)
		stab[i] = ref[i] = (uint8_t)(0x40 + i);
	memset(blk, 0x77, T30_STAB_BYTES);
	memset(blk + T30_STAB_BYTES, 0, T30_STAB_BLOCK - T30_STAB_BYTES);
	t30_stab_apply(stab, blk);
	bad += memcmp(stab, ref, sizeof(stab)) != 0;

	/* MoveState day block: [26] u16 cap (item 22), [10] manual sinter,
	 * [43] sinter target, [11] manual temper, [46] temper target */
	memset(blk, 0, sizeof(blk));
	memcpy(blk + 26, &it, 2);
	blk[10] = 1; blk[43] = 9; blk[11] = 1; blk[46] = 3;
	blk[82] = blk[70] = blk[98] = blk[71] = blk[101] = 1;
	t30_stab_apply(stab, blk);
	memcpy(&v16, stab + 26, 2);
	bad += v16 != it || stab[10] != 1 || stab[43] != 9 || stab[11] != 1 || stab[46] != 3;
	for (i = 0; i < T30_STAB_BYTES; i++)
		if (i != 26 && i != 27 && i != 10 && i != 43 && i != 11 && i != 46)
			bad += stab[i] != ref[i];

	/* the 4-byte item (16) and the last one (59, DIS y) */
	memset(blk, 0, sizeof(blk));
	memcpy(blk + 16, &v32, 4);
	blk[58] = 0x5a; blk[59] = 0xa5;
	blk[T30_STAB_BYTES + 16] = blk[T30_STAB_BYTES + 51] = 1;
	t30_stab_apply(stab, blk);
	bad += memcmp(stab + 16, &v32, 4) != 0 || stab[58] != 0x5a || stab[59] != 0xa5;

	printf("t30_stab_host_test: %s\n", bad ? "FAIL" : "PASS");
	return bad != 0;
}
