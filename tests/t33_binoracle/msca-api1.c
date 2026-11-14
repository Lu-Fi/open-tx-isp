/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "msca-api1": the vendor's lib_tisp_msca_* thin
 * wrappers (mask args to 8 bits, tail-call tisp_msca_t33_*) vs our port in
 * driver/t32/tx_isp_t33_msca_api1.h.  The callees are ported elsewhere
 * (tx_isp_t32_hw.c), so they are CALLS lines: declared extern with the
 * vendor name, neither side runs them, calls are recorded and compared.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_msca_api1.h"

int bo_lib_tisp_msca_addr_fifo_read(u32 addr, u32 data)
{
	return t33_msca_addr_fifo_read(addr, data);
}

int bo_lib_tisp_msca_addr_fifo_write(u32 addr, u32 data)
{
	return t33_msca_addr_fifo_write(addr, data);
}

int bo_lib_tisp_msca_chx_en(u32 ch, u32 x, u32 en)
{
	return t33_msca_chx_en(ch, x, en);
}
