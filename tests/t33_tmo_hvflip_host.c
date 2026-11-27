/*
 * t33_tmo_hvflip_host: t33_tmo_hvflip_table (driver/t32/tx_isp_t33_tmoA.h) against the literal word loops of
 * SDK 2.1.0 PRJ008 tisp_tmo_hvflip (drv disassembly 0xf6dc4..0xf6f1c) (openimp #928).
 *  flip: for a1 = base + 5600 down to + 3200 (step 400): swap the 100 words at a1 with those at base + 5600 - (a1 - base);
 *  mirror: per block (15 x 400 bytes) 12 iterations: row j (4 words) <-> row 24 - j.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t33_tmoA.h"

int isp_printf(unsigned int level, const char *fmt, ...) { (void)level; (void)fmt; return 0; }

static void ref(u32 *t, u32 a1, u32 a2)
{
	u32 off, blk, i, j;

	if (a1 == 1) {
		for (off = 5600; off != 2800; off -= 400) {
			u32 *a = (u32 *)((u8 *)t + off), *v = (u32 *)((u8 *)t + (5600 - off));

			for (i = 0; i < 25; i++) {	/* 16 bytes per round */
				u32 t0[4];

				memcpy(t0, v + 4 * i, 16);
				memcpy(v + 4 * i, a + 4 * i, 16);
				memcpy(a + 4 * i, t0, 16);
			}
		}
	}
	if (a2 == 1) {
		for (blk = 0; blk < 6000; blk += 400) {
			u32 *v = (u32 *)((u8 *)t + blk);	/* ascending, from the block start */
			u32 *a = v + 96;			/* descending, from the last row */

			for (j = 0; j < 12; j++, v += 4, a -= 4) {
				u32 t0[4];

				memcpy(t0, v, 16);
				memcpy(v, a, 16);
				memcpy(a, t0, 16);
			}
		}
	}
}

int main(void)
{
	static u32 a[1500], b[1500], o[1500];
	unsigned int bad = 0, m, k;

	for (m = 0; m < 4; m++) {
		for (k = 0; k < 1500; k++)
			o[k] = a[k] = b[k] = (u32)rand() * 2654435761u + k;
		ref(a, (m >> 1) & 1, m & 1);
		t33_tmo_hvflip_table(b, (m >> 1) & 1, m & 1);
		if (memcmp(a, b, sizeof(a))) {
			printf("FAIL: mode %u differs from the vendor loops\n", m);
			bad++;
		}
		if ((m & 3) && !memcmp(b, o, sizeof(o))) {
			printf("FAIL: mode %u changed nothing\n", m);
			bad++;
		}
		t33_tmo_hvflip_table(b, (m >> 1) & 1, m & 1);
		if (memcmp(b, o, sizeof(o))) {
			printf("FAIL: mode %u not an involution\n", m);
			bad++;
		}
	}
	if (bad)
		return 1;
	printf("t33_tmo_hvflip_host: OK\n");
	return 0;
}
