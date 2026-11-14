/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "mipi": get_settle_time (driver/t32/tx_isp_t33_mipi.h) with the vendor calling convention. */
#include <stdint.h>
#include "tx_isp_t33_mipi.h"

unsigned int bo_get_settle_time(int clk) { return t33_settle_time(clk); }
