/* Host test for the T23 MDNS reference hold words (driver/t23/tx_isp_t23_mdns_hold.h). */
#include <stdio.h>

#include "../driver/t23/tx_isp_t23_mdns_hold.h"

static int failures;

#define CHECK(cond) do { \
	if (!(cond)) { \
		fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
		failures++; \
	} \
} while (0)

int main(void)
{
	/* hold 0: every word passes through unchanged (zero behaviour change) */
	CHECK(t23_mdns_hold_word(0, 0x799c, 0x14ff14e6u) == 0x14ff14e6u);
	CHECK(t23_mdns_hold_word(0, 0x7a6c, 0x00b414b4u) == 0x00b414b4u);
	CHECK(t23_mdns_hold_word(0, 0x7a68, 0x00112200u) == 0x00112200u);
	CHECK(t23_mdns_hold_word(0, 0x7a70, 0x00331122u) == 0x00331122u);
	/* bit 0: luma and chroma weight words at maximum, others untouched */
	CHECK(t23_mdns_hold_word(1, 0x799c, 0x14ff14e6u) == 0x00ffffffu);
	CHECK(t23_mdns_hold_word(1, 0x7a6c, 0x00b414b4u) == 0x00ffffffu);
	CHECK(t23_mdns_hold_word(1, 0x7a68, 0x00112200u) == 0x00112200u);
	CHECK(t23_mdns_hold_word(1, 0x79a0, 0x00331122u) == 0x00331122u);
	/* bit 1: fake and mv at 0xff, fs_opt nibbles (byte 0) and top byte kept */
	CHECK(t23_mdns_hold_word(2, 0x7998, 0x00112231u) == 0x00ffff31u);
	CHECK(t23_mdns_hold_word(2, 0x7a68, 0x00112200u) == 0x00ffff00u);
	CHECK(t23_mdns_hold_word(2, 0x799c, 0x14ff14e6u) == 0x14ff14e6u);
	/* bit 2: r_max/r_min at 0xff, increase kept */
	CHECK(t23_mdns_hold_word(4, 0x79a0, 0x00331122u) == 0x0033ffffu);
	CHECK(t23_mdns_hold_word(4, 0x7a70, 0x00331122u) == 0x0033ffffu);
	CHECK(t23_mdns_hold_word(7, 0x1234, 0xdeadbeefu) == 0xdeadbeefu);

	if (failures) {
		fprintf(stderr, "%d failure(s)\n", failures);
		return 1;
	}
	printf("tx_isp_t23_mdns_hold_test: ok\n");
	return 0;
}
