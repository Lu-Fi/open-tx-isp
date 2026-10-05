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
