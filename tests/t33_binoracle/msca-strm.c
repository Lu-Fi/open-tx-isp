/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "msca-strm": MSCA stream on/off and OSD / line out-config getters. */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_mscastrm.h"

u8 *stMscaAttr[3];
u8 tispMscaRefresh;

extern void tisp_msca_t33_chx_en(u32 a, u32 b, u32 c);

void bo_tisp_msca_t33_api_chx_stream_on(u32 ch)
{
	t33_msca_stream_on(stMscaAttr, &tispMscaRefresh, ch, tisp_msca_t33_chx_en);
}

void bo_tisp_msca_t33_api_chx_stream_off(u32 ch)
{
	t33_msca_stream_off(stMscaAttr, &tispMscaRefresh, ch, tisp_msca_t33_chx_en);
}

void bo_tisp_msca_t33_api_get_osd_out_cfg(u32 ch, u8 *p)
{
	t33_msca_get_osd_cfg(stMscaAttr, ch, p);
}

void bo_tisp_msca_t33_api_get_line_out_cfg(u32 ch, u8 *p)
{
	t33_msca_get_line_cfg(stMscaAttr, ch, p);
}
