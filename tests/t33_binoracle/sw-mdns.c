/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "sw-mdns": tisp_mdns_switch_para (driver/t32/tx_isp_t33_swmdns.h) with the vendor
 * calling convention; the callees are reached through the vendor names (CALLS lines of sw-mdns.conf).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_blk.h"
#include "tx_isp_t33_swmdns.h"

extern void tisp_mdns_binpara_update(u32 ch);
extern void func_mdns_para_update(u32 ch);
extern void tisp_mdns_t33_reg_trig(u32 ch);
extern int tisp_reg_write_t33(u32 ch, u32 blk, u32 reg, u32 val);
extern u32 tisp_simple_intp_uint8_t(u32 i, u32 f, const u8 *p);
extern u32 tisp_simple_intp_uint16_t(u32 i, u32 f, const u8 *p);

u32 t33_o_tparams[4];
u8 t33_o_ybsn[44] __attribute__((aligned(4)));
u8 t33_o_pb0[44] __attribute__((aligned(4)));
u8 t33_o_pb1[44] __attribute__((aligned(4)));
u8 t33_o_wmin[44] __attribute__((aligned(4)));
u8 t33_o_wadj[44] __attribute__((aligned(4)));
u8 t33_o_cfg;
u8 t33_o_uvflag[4] __attribute__((aligned(4)));
u8 t33_o_stmem[304] __attribute__((aligned(4)));

static void o_regw(u32 ch, u32 blk, u32 reg, u32 val) { tisp_reg_write_t33(ch, blk, reg, val); }
static void o_bin(u32 ch) { tisp_mdns_binpara_update(ch); }
static void o_upd(u32 ch) { func_mdns_para_update(ch); }
static void o_trig(u32 ch) { tisp_mdns_t33_reg_trig(ch); }
static u32 o_i8(u32 i, u32 f, const u8 *p) { return tisp_simple_intp_uint8_t(i, f, p); }
static u32 o_i16(u32 i, u32 f, const u8 *p) { return tisp_simple_intp_uint16_t(i, f, p); }

static const struct t33_swmdns_ops ops = { o_regw, o_bin, o_upd, o_trig, o_i8, o_i16 };

int bo_tisp_mdns_switch_para(u32 ch, u32 *inf)
{
	u8 *g[5] = { t33_o_ybsn, t33_o_pb0, t33_o_pb1, t33_o_wmin, t33_o_wadj };

	ch &= 0xff;
	return t33_mdns_switch_para(&ops, ch, inf, (const u8 *)(unsigned long)t33_o_tparams[ch & 3], g,
				    t33_o_cfg, t33_o_uvflag[ch & 3], t33_o_stmem);
}
