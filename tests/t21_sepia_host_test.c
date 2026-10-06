/* Host test: T21 sepia tint on the CSC matrix (tx_isp_t21_tuning_ctl.h).
 * A grey input (R = G = B, after the BW saturation list) must keep its luma
 * and get U below / V above neutral; the plain matrix stays neutral. */
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#define T21_TUNING_CTL_HOST_TEST
#include "../driver/t21/tx_isp_t21_tuning_ctl.h"

static int row_sum(const int32_t *p)
{
	return p[0] + p[1] + p[2];
}

int main(void)
{
	int32_t in[TX_ISP_CSC_WORDS], out[TX_ISP_CSC_WORDS];
	uint32_t reg[5], plain[5];
	uint32_t sat_in[9] = { 256, 256, 256, 256, 256, 256, 256, 256, 256 }, sat_out[9];
	int bad = 0, i, mode;

	for (mode = 0; mode < 4; mode++) {
		for (i = 0; i < TX_ISP_CSC_WORDS; i++)
			in[i] = tx_isp_csc_presets[mode][i];
		t21_sepia_params(in, out);
		/* Y row and offsets untouched */
		for (i = 0; i < 3; i++)
			bad += out[i] != in[i];
		for (i = 9; i < TX_ISP_CSC_WORDS; i++)
			bad += out[i] != in[i];
		/* grey: U row sum (signs as the hardware applies them) is
		 * -T, V row sum +T relative to the plain matrix */
		/* (+-1: the OEM presets' own rows do not sum exactly) */
		bad += abs(row_sum(out + 3) - row_sum(in + 3) + T21_SEPIA_TINT) > 1;
		bad += abs(row_sum(out + 6) - row_sum(in + 6) - T21_SEPIA_TINT) > 1;
		tx_isp_csc_tiziano_regs(in, plain);
		tx_isp_csc_tiziano_regs(out, reg);
		bad += reg[0] != plain[0];	/* Y row word */
		bad += reg[1] == plain[1] || reg[2] == plain[2];
		bad += reg[3] != plain[3] || reg[4] != plain[4];
	}
	/* a user matrix near the limits is clamped, never wraps */
	for (i = 0; i < TX_ISP_CSC_WORDS; i++)
		in[i] = tx_isp_csc_presets[0][i];
	in[3] = -0x3ff; in[4] = -0x3ff; in[7] = -0x3ff; in[8] = -0x3ff;
	t21_sepia_params(in, out);
	bad += out[6] > 0x3ff || out[5] < 0;
	/* sepia scales the CCM saturation list to 0 like BW */
	t21_colorfx_sat(T21_COLORFX_SEPIA, sat_in, sat_out);
	for (i = 0; i < 9; i++)
		bad += sat_out[i] != 0;
	bad += !t21_colorfx_scales_sat(T21_COLORFX_SEPIA);
	puts(bad ? "FAIL" : "PASS");
	return bad != 0;
}
