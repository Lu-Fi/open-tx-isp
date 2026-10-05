/*
 * The firmware unit under test plus harness accessors for its module
 * parameters.  Built once per variant (-O0, -Os, per-function mixes).
 */
#include "../../driver/t20/tx_isp_t20_firmware.c"

void t20fw_set_knobs(int oem, int trace)
{
	t20_simple_ae = !oem;
	t20_simple_awb = !oem;
	t20_simple_nr = !oem;
	t20_trace_events = trace;
}

/* the four Bayer gains the simple AWB holds across frame starts */
void t20fw_awb_gains(uint32_t *g)
{
	g[0] = t20_simple_awb_gain_00;
	g[1] = t20_simple_awb_gain_01;
	g[2] = t20_simple_awb_gain_10;
	g[3] = t20_simple_awb_gain_11;
}
