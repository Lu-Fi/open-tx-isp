/* Host test: T21 brightness as AE target compensation (tx_isp_t21_ae_comp.h).
 * 128 must be the identity for every target; monotonic; bounded. */
#include <stdio.h>
#include <stdint.h>

#define _LINUX_TYPES_H
#include "../driver/t21/tx_isp_t21_ae_comp.h"

int main(void)
{
	static const uint32_t at[] = { 0x4b, 0x41, 0x37, 0x2d, 0x28, 0x1e, 1, 255 };
	int bad = 0;
	unsigned i, c;

	for (i = 0; i < sizeof(at) / sizeof(at[0]); i++) {
		uint32_t prev = 0;

		bad += t21_ae_comp_scale(at[i], 128) != at[i];
		for (c = 0; c < 256; c++) {
			uint32_t v = t21_ae_comp_scale(at[i], c);

			bad += v < 1 || v > 255 || v < prev;
			prev = v;
		}
		bad += t21_ae_comp_scale(at[i], 192) < at[i];
		bad += t21_ae_comp_scale(at[i], 64) > at[i];
	}
	bad += t21_ae_comp_scale(0, 200) != 0;
	bad += t21_ae_comp_scale(75, 255) != 149;
	bad += t21_ae_comp_scale(75, 64) != 38;
	bad += t21_ae_comp_scale(75, 0) != 9;
	printf("%s (%d)\n", bad ? "FAIL" : "ok", bad);
	return bad != 0;
}
