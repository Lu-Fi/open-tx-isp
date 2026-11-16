/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "mdnspr" (openimp #99): tisp_mdns_init (tx_isp_t33_mdnsR.h) -> tisp_mdns_refresh
 * (tx_isp_t33_mdnsP.h) -> tisp_mdns_par_refresh (tx_isp_t33_swmdns.h) and tisp_mdns_switch_para with the whole
 * callee tree in our form: tisp_mdns_cmp_par_calc (mdnsP), tisp_mdns_t33_cmp_reg_write (mdnsA),
 * tisp_mdns_binpara_update + func_mdns_para_update (tx_isp_t33_mdns.h), the gain interpolations
 * (tx_isp_t32_tp.h).  Built for mipsel and run against the vendor tx-isp-t33.ko by
 * tools/ko_emu/diff_t33_mdnspr.py on the same module state.  Module statics are globals named like the vendor's;
 * hardware / kernel calls (tisp_reg_write_t33, system_reg_write, tisp_mdns_t33_reg_trig, the allocator, the
 * rcfg / irq registration) stay undefined and are recorded by the emulator on both sides.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_blk.h"
#include "tx_isp_t32_tp.h"
#include "tx_isp_t33_mdns.h"
#include "tx_isp_t33_mdnsP.h"
#include "tx_isp_t33_swmdns.h"
#include "tx_isp_t33_mdnsR.h"

u32 mdnsInfo[4];
u32 tparamsP[4];
u8 stMemInfo[304] __attribute__((aligned(4)));
u8 uvLimitFlag[4];
u8 mdnsCfgMode;
u8 riscvEnable[4];
u8 riscvMdnsEn;
u8 stRiscvPara[51200] __attribute__((aligned(4)));
u8 stIspInitPar[4032] __attribute__((aligned(4)));
u8 mdnsInit;
u8 tispMdnsRefresh;
u8 global_ybsnthres[44];
u8 global_ypbv0stathres[44];
u8 global_ypbv1stathres[44];
u8 global_yrefweimin[44];
u8 global_yrefweiminadjthres[44];

extern void tisp_mdns_t33_reg_trig(u32 ch);

static u32 *o_inf(u32 ch) { return (u32 *)(unsigned long)mdnsInfo[ch & 3]; }
static void o_regw(u32 ch, u32 blk, u32 reg, u32 val) { tisp_reg_write_t33(ch, blk, reg, val); }
static int o_wr(u32 id, u32 blk, u32 reg, u32 val) { return tisp_reg_write_t33(id, blk, reg, val); }
static void o_bin(u32 ch)
{
	u32 *v = o_inf(ch);

	t33_vb_bin(v, (const u8 *)(unsigned long)v[2180], t33_mdns_bin, ARRAY_SIZE(t33_mdns_bin));
}
static void o_upd(u32 ch) { t33_mdns_para(o_inf(ch)); }
static void o_trig(u32 ch) { tisp_mdns_t33_reg_trig(ch); }
static u32 o_i8(u32 i, u32 f, const u8 *p) { return t32_tp_intp_u8(i, f, p); }
static u32 o_i16(u32 i, u32 f, const u8 *p) { return t32_tp_intp_u16(i, f, p); }

static const struct t33_swmdns_ops ops = { o_regw, o_bin, o_upd, o_trig, o_i8, o_i16 };

void tisp_mdns_cmp_par_calc(u32 chn, u32 h, u32 w) { t33_mdns_cmp_par(o_inf(chn), h, w); }
void tisp_mdns_t33_cmp_reg_write(u32 chn) { t33_mdns_cmp_reg_write(chn, o_inf(chn), mdnsCfgMode, o_wr); }

void tisp_mdns_par_refresh(u32 chn, u32 gain, u32 thr)
{
	t33_mdns_par_refresh(&ops, chn, (u32 *)(unsigned long)mdnsInfo[chn], gain, thr, mdnsCfgMode, uvLimitFlag[chn],
			     stMemInfo, riscvEnable[chn] && riscvMdnsEn, &tispMdnsRefresh);
}

int tisp_mdns_refresh(u32 chn, u32 gain) { return t33_mdns_refresh(chn, gain, o_inf(chn)); }

u32 tisp_rcfg_mdns_reg_init(u32 *e) { (void)e; return 0; }
int tisp_mdns_t33_frame_done_interrupt(u32 id) { (void)id; return 0; }

int emu_mdns_init(u32 chn, const u8 *ip) { return t33_tisp_mdns_init(chn, ip); }

int emu_mdns_switch(u32 ch)
{
	u8 *g[5] = { global_ybsnthres, global_ypbv0stathres, global_ypbv1stathres, global_yrefweimin,
		     global_yrefweiminadjthres };

	return t33_mdns_switch_para(&ops, ch, o_inf(ch), (const u8 *)(unsigned long)tparamsP[ch & 0xff & 3], g,
				    mdnsCfgMode, uvLimitFlag[ch & 0xff & 3], stMemInfo);
}
