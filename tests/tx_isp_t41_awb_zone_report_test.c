/* T41 GetAwbStatistics freshness contract (driver/t41/tx_isp_t41_awb_zone_report.h):
 * the 2700-byte zone planes are handed out only while the native AWB stream is
 * live and the last filling frame is at most TX_ISP_AWB_ZONE_MAX_AGE_MS old. */
#include <assert.h>
#include <stdio.h>
#include "../driver/t41/tx_isp_t41_awb_zone_report.h"

int main(void)
{
	assert(T41_AWB_ZONE_REPORT_BYTES == 3U * 15U * 15U * 4U);
	assert(T41_AWB_ZONE_REPORT_OFFSET == 60096U);	/* stock tisp_awb_get_zone plane 0 */
	/* never filled since the stream started */
	assert(t41_awb_zone_report_fresh(1, 0, 5000) == 0);
	/* stream not live (before start, after stop): the planes are stale */
	assert(t41_awb_zone_report_fresh(0, 4999, 5000) == 0);
	/* live and recent */
	assert(t41_awb_zone_report_fresh(1, 5000, 5000) == 1);
	assert(t41_awb_zone_report_fresh(1, 4000, 5000) == 1);
	assert(t41_awb_zone_report_fresh(1, 4000, 5000 + TX_ISP_AWB_ZONE_MAX_AGE_MS - 1000) == 1);
	/* one tick too old */
	assert(t41_awb_zone_report_fresh(1, 4000, 4000 + TX_ISP_AWB_ZONE_MAX_AGE_MS + 1) == 0);
	/* a zero jiffies value is stored as 1 so it never reads as "never" */
	assert(t41_awb_zone_report_stamp(0) == 1UL);
	assert(t41_awb_zone_report_stamp(77) == 77UL);
	assert(t41_awb_zone_report_fresh(1, t41_awb_zone_report_stamp(0), 1) == 1);
	printf("tx_isp_t41_awb_zone_report_test: ok\n");
	return 0;
}
