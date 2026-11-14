/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "msca-api3": MSCA frame drop / source crop API. */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_msca_api3.h"

extern u32 v_system_reg_read(u32 a) __asm__("system_reg_read");
extern void v_system_reg_write(u32 a, u32 v) __asm__("system_reg_write");
extern u32 v_tisp_reg_read_t33(u32 id, u32 blk, u32 a) __asm__("tisp_reg_read_t33");
extern u32 v_tisp_reg_write_t33(u32 id, u32 blk, u32 a, u32 v) __asm__("tisp_reg_write_t33");
extern int v_isp_printf(int lvl, const char *fmt, ...) __asm__("isp_printf");

u32 our_stIspInitPar[1008];
u8 our_mscaCfgMode;

static const struct t33_msca_io io = {
	v_system_reg_read, v_system_reg_write, v_tisp_reg_read_t33, v_tisp_reg_write_t33, v_isp_printf,
};

int bo_tisp_msca_api_get_frame_drop(u32 id, u32 chan, u8 *out)
{
	return t33_msca_get_frame_drop(&io, our_stIspInitPar[226], id, chan, out);
}

int bo_tisp_msca_api_set_frame_drop(u32 id, u32 chan, u8 *in)
{
	return t33_msca_set_frame_drop(&io, our_stIspInitPar[226], chan, in);
}

int bo_tisp_msca_refresh_srcrop(u32 id, u32 x, u32 y)
{
	return t33_msca_refresh_srcrop(&io, our_mscaCfgMode, id, x, y);
}
