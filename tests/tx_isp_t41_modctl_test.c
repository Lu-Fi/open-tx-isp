#include <stdio.h>
#include <string.h>
#include "../driver/t41/tx_isp_t41_modctl.h"
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
int main(void)
{
	unsigned char calib[32]; unsigned int top, m, v;
	memset(calib, 0, sizeof(calib)); calib[9] = 1;	/* CCM calibrated bypassed */
	/* set GAMMA bypass: upper bits kept */
	CHECK(!t41_modctl_plan(0xfc000000U | 0x200, 0x200 | 0x400, calib, 0, 0, &top, &m, &v));
	CHECK(top == (0xfc000000U | 0x600)); CHECK(m == 0x400 && v == 0x400);
	/* back to calibrated: override dropped */
	CHECK(!t41_modctl_plan(top, 0x200, calib, m, v, &top, &m, &v));
	CHECK(top == (0xfc000000U | 0x200)); CHECK(m == 0 && v == 0);
	/* enable CCM (calibrated 1) -> override value 0 */
	CHECK(!t41_modctl_plan(0x200, 0, calib, 0, 0, &top, &m, &v));
	CHECK(top == 0 && m == 0x200 && v == 0);
	/* unsupported bit (BLC bit 0) and out-of-range key refused, unchanged outputs */
	CHECK(t41_modctl_plan(0, 1, calib, 0, 0, &top, &m, &v) == -1);
	CHECK(t41_modctl_plan(0, 0x02000000U, calib, 0, 0, &top, &m, &v) == -1);
	/* identical key is a no-op even for an unsupported bit set by calibration */
	CHECK(!t41_modctl_plan(0x1, 0x1, calib, 0, 0, &top, &m, &v) && top == 1 && m == 0);
	/* no calibration: always an override */
	CHECK(!t41_modctl_plan(0, 1U<<13, NULL, 0, 0, &top, &m, &v) && m == 1U<<13);
	printf(fails ? "modctl FAILED\n" : "modctl ok\n");
	return fails != 0;
}
