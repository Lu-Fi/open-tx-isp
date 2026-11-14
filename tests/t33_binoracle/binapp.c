/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "binapp": tisp_bin_append_data_parse with the
 * vendor calling convention, built for mipsel and run against the vendor
 * tx-isp-t33.ko instructions (spec: binapp.conf, FUNCS).
 */
#include <stdint.h>
#include "tx_isp_t33_binapp.h"

u32 lutMaxSize[28];
u32 stLutInfo[4];
u8 *tparamsP[4];

int bo_tisp_bin_append_data_parse(u32 ch, u8 *bin, u32 len)
{
	return t33_bin_append_data_parse(ch, bin, len);
}
