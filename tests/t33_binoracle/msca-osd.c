/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "msca-osd": MSCA OSD / mask setters with the vendor calling convention.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_mscaosd.h"

extern int tisp_reg_write_t33(u32 ch, u32 blk, u32 reg, u32 val);
extern int isp_printf(int level, const char *fmt, ...);

u8 *stMscaAttr[2];
u8 mscaCfgMode;
u8 tispMscaRefresh;
u32 MscaOsdInfo;

static void wr_cb(u32 ch, u32 blk, u32 reg, u32 val) { tisp_reg_write_t33(ch, blk, reg, val); }
static int msg_cb(u32 level, const char *fmt, ...) { return isp_printf((int)level, fmt); }

static void env_get(struct t33_mo_env *e, u32 ch)
{
	e->attr = stMscaAttr[ch & 0xff];
	e->cfg_mode = mscaCfgMode;
	e->refresh = &tispMscaRefresh;
	e->osd_info = &MscaOsdInfo;
	e->wr = wr_cb;
	e->msg = msg_cb;
}

int bo_tisp_msca_t33_api_set_osd_out_cfg(u32 ch, u8 *p)
{
	struct t33_mo_env e;

	ch &= 0xff;
	env_get(&e, ch);
	return t33_msca_osd_out_cfg(&e, ch, p);
}

int bo_tisp_msca_t33_api_set_mask_out_cfg(u32 ch, u8 *p)
{
	struct t33_mo_env e;

	ch &= 0xff;
	env_get(&e, ch);
	return t33_msca_mask_out_cfg(&e, ch, p);
}

void bo_tisp_msca_t33_api_set_mask_reg(u32 ch)
{
	struct t33_mo_env e;

	ch &= 0xff;
	env_get(&e, ch);
	t33_msca_mask_reg(&e, ch);
}

void bo_tisp_msca_t33_api_set_osd_reg(u32 ch)
{
	struct t33_mo_env e;

	ch &= 0xff;
	env_get(&e, ch);
	t33_msca_osd_reg(&e, ch);
}
