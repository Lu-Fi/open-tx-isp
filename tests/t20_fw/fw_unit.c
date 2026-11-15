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

/* OEM AE graph, compact AWB and NR (openimp #609 scenario oem-ae) */
void t20fw_set_oem_ae(void)
{
	t20_simple_ae = 0;
	t20_simple_awb = 1;
	t20_simple_nr = 1;
}

int t20fw_compact_ae(void)
{
	return t20_simple_ae;
}

/* the four Bayer gains the simple AWB holds across frame starts */
void t20fw_awb_gains(uint32_t *g)
{
	g[0] = t20_simple_awb_gain_00;
	g[1] = t20_simple_awb_gain_01;
	g[2] = t20_simple_awb_gain_10;
	g[3] = t20_simple_awb_gain_11;
}

/* safe anti-fog preset (t20_antifog_request) and its module parameter */
int t20fw_antifog(uint32_t level)
{
	return t20_antifog_request(level);
}

void t20fw_antifog_variance(int on)
{
	t20_antifog_variance = on;
}

unsigned t20fw_antifog_ignored(void)
{
	return t20_antifog_ignored;
}

int t20fw_mesh_scale(uint32_t scale)
{
	return t20_mesh_scale_request(scale);
}
