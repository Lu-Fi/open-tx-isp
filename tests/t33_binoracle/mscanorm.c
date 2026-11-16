/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "mscanorm": the MSCA 2/3/4/6-point coefficient normalisation (openimp #99, #643) */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_mscacur.h"

void bo_tisp_msca_t33_two_point_normalized(u32 n, u32 taps, short *c)
{
	t33_msca_two_point_v(n, taps, c);
}

void bo_tisp_msca_t33_thr_point_normalized(u32 n, u32 taps, short *c)
{
	t33_msca_thr_point_v(n, taps, c);
}

void bo_tisp_msca_t33_four_ponit_normalized(u32 n, u32 taps, short *c)
{
	t33_msca_four_point_v(n, taps, c);
}

void bo_tisp_msca_t33_six_point_normalized(u32 n, u32 taps, short *c)
{
	t33_msca_six_point_v(n, taps, c);
}
