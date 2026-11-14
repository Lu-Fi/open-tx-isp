/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "cscmisc": tisp_t33_csc_write_reg, tisp_csccr_bin_par_update,
 * tisp_sdns_other_params_transform (tx_isp_t33_csc.h, tx_isp_t33_blk.h) with the vendor calling convention. */
#include <stdint.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;

extern int isp_printf(unsigned int level, const char *fmt, ...);
extern int tisp_reg_write_t33(u32 ch, u32 mod, u32 reg, u32 val);
extern int tisp_rcfg_get_dma_state(u32 mod);

#include "tx_isp_t33_blk.h"
#include "tx_isp_t33_csc.h"

u8 *o_csc_para[4], *o_csccrInfo[4], *o_sdnsInfo[4];
u8 o_cscCfgMode[4], o_tispCscRefresh[4];

static const struct t33_csc_ops o_ops = { tisp_reg_write_t33, tisp_rcfg_get_dma_state };

int bo_csc_write_reg(u32 ch)
{
	return t33_csc_write_reg(ch, *(u8 **)(o_csc_para[ch & 0xff] + 4), o_cscCfgMode[0], o_tispCscRefresh, &o_ops);
}

int bo_csccr_bin_par_update(u32 ch)
{
	u8 *inf = o_csccrInfo[ch & 0xff];

	return t33_csccr_bin_par_update(*(u8 **)(inf + 0xc), inf + 0x14);
}

void bo_sdns_other_params(u32 ch)
{
	u32 *inf = (u32 *)o_sdnsInfo[ch & 0xff];

	t33_sdns_other_params((const u8 *)inf[0x24], inf);
}
