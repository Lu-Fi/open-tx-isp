/* T10/T20 GetAwbZone: metering-memory addresses and zone unpacking
 * (driver/include/tx_isp/tx_isp_t2x_awb_zone.h). */
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include "tx_isp/tx_isp_t2x_awb_zone.h"

/* T20 3.12.0 imp_isp.h: struct isp_core_wb_zone_info / IMPISPAWBZone */
struct vendor_wb_zone_info {
	unsigned short red_green;
	unsigned short blue_green;
	unsigned int sum;
};

int main(void)
{
	struct tx_isp_t2x_awb_zone z;

	/* the vendor structure layout, 15x15 of them */
	assert(sizeof(struct tx_isp_t2x_awb_zone) ==
	       sizeof(struct vendor_wb_zone_info));
	assert(offsetof(struct tx_isp_t2x_awb_zone, blue_green) == 2);
	assert(offsetof(struct tx_isp_t2x_awb_zone, sum) == 4);
	assert(TX_ISP_T2X_AWB_ZONES * sizeof(z) == 1800);

	/* zone 0 at metering word 464 (0x1d0), as the firmware
	 * awb_read_statistics() and the compact AWB (0x8740) read it */
	assert(tx_isp_t2x_awb_zone_reg(0, 0) == 0x8740);
	assert(tx_isp_t2x_awb_zone_reg(0, 1) == 0x8744);
	assert(tx_isp_t2x_awb_zone_reg(1, 0) == 0x8748);
	assert(tx_isp_t2x_awb_zone_reg(224, 1) == 0x8740 + 224 * 8 + 4);
	/* the last zone stays below the AF bank (word 928) */
	assert(tx_isp_t2x_awb_zone_reg(224, 1) < 0x8000 + 928 * 4);

	/* ratios are 12-bit fields, the population is the whole word */
	tx_isp_t2x_awb_zone_unpack(0xf123f456u, 0x89abcdefu, &z);
	assert(z.red_green == 0x456 && z.blue_green == 0x123);
	assert(z.sum == 0x89abcdefu);
	tx_isp_t2x_awb_zone_unpack(0x01000100u, 0, &z);
	assert(z.red_green == 256 && z.blue_green == 256 && z.sum == 0);

	printf("tx_isp_t2x_awb_zone_test: ok\n");
	return 0;
}
