/* Host test for the T23 MSCA scratch helpers (driver/t23/tx_isp_t23_scratch.h). */
#include <stdio.h>

#include "../driver/t23/tx_isp_t23_scratch.h"

static int failures;

#define CHECK(cond) do { \
	if (!(cond)) { \
		fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
		failures++; \
	} \
} while (0)

int main(void)
{
	u32 b720 = t23_scratch_bytes(1280, 720);
	u32 b1080 = t23_scratch_bytes(1920, 1080);
	u32 phys;

	/* one Y plane, height aligned to 16, page rounded */
	CHECK(b720 == 921600U);			/* 1280 x 720 = 225 pages */
	CHECK(b1080 == 2088960U);		/* 1920 x 1088, already page aligned */
	CHECK(t23_scratch_bytes(640, 360) == 237568U);	/* 640 x 368 = 235520 -> 58 pages */
	CHECK(t23_scratch_bytes(0, 720) == 0);
	CHECK(t23_scratch_bytes(1280, 0) == 0);
	CHECK(t23_scratch_bytes(0x10000U, 0x10000U) == 0);	/* absurd size refused */
	CHECK((t23_scratch_bytes(1000, 17) & (T23_SCRATCH_PAGE - 1U)) == 0);

	/* placed at the end, behind MDNS and the crumb page */
	phys = t23_scratch_place(0x2b00000U, 0x300000U + 4096U + b720,
				 0x300000U, 4096U, b720);
	CHECK(phys == 0x2b00000U + 0x300000U + 4096U);
	phys = t23_scratch_place(0x2b00000U, 0x300000U + b720, 0x300000U, 0, b720);
	CHECK(phys == 0x2b00000U + 0x300000U);
	/* a bigger buffer: still the last bytes */
	phys = t23_scratch_place(0x2b00000U, 0x400000U + b720, 0x300000U, 0, b720);
	CHECK(phys == 0x2b00000U + 0x400000U);
	/* too small: no scratch (old GET_BUF size, or crumbs turned on in between) */
	CHECK(t23_scratch_place(0x2b00000U, 0x300000U, 0x300000U, 0, b720) == 0);
	CHECK(t23_scratch_place(0x2b00000U, 0x300000U + b720, 0x300000U, 4096U, b720) == 0);
	CHECK(t23_scratch_place(0, 0x400000U, 0x300000U, 0, b720) == 0);
	CHECK(t23_scratch_place(0x2b00000U, 0x400000U, 0x300000U, 0, 0) == 0);
	/* misaligned start: refused (the MSCA drops the low 3 address bits) */
	CHECK(t23_scratch_place(0x2b00004U, 0x300000U + b720, 0x300000U, 0, b720) == 0);
	/* wraps past 4 GiB: refused */
	CHECK(t23_scratch_place(0xfff00000U, 0x200000U, 0x1000U, 0, 0x1000U) == 0);

	/* frame fits: every channel up to the sensor size */
	CHECK(t23_scratch_fits(1280, 720, b720));
	CHECK(t23_scratch_fits(640, 360, b720));
	CHECK(!t23_scratch_fits(1920, 1080, b720));
	CHECK(!t23_scratch_fits(1280, 720, 0));
	CHECK(!t23_scratch_fits(0, 720, b720));
	CHECK(t23_scratch_fits(1920, 1080, b1080));
	CHECK(!t23_scratch_fits(1920, 1090, b1080));

	/* park only for a kept output with frames still coming */
	CHECK(t23_scratch_park_wanted(2, 0, 1, 1));
	CHECK(!t23_scratch_park_wanted(1, 0, 1, 1));	/* 1: kept only when the input stops */
	CHECK(!t23_scratch_park_wanted(0, 0, 1, 1));	/* 0: output switched off */
	CHECK(!t23_scratch_park_wanted(2, 1, 1, 1));	/* input stops with this STREAMOFF */
	CHECK(!t23_scratch_park_wanted(2, 0, 0, 1));	/* input not running */
	CHECK(!t23_scratch_park_wanted(2, 0, 1, 0));	/* output not on */

	/* completion match ignores the low 3 bits; no scratch matches nothing */
	CHECK(t23_scratch_is(0x2e01000U, 0x2e01000U));
	CHECK(t23_scratch_is(0x2e01000U, 0x2e01007U));
	CHECK(!t23_scratch_is(0x2e01000U, 0x2e01008U));
	CHECK(!t23_scratch_is(0, 0));
	CHECK(!t23_scratch_is(0, 0x2e01000U));

	if (failures) {
		fprintf(stderr, "tx_isp_t23_scratch_test: %d failure(s)\n", failures);
		return 1;
	}
	printf("tx_isp_t23_scratch_test: OK\n");
	return 0;
}
