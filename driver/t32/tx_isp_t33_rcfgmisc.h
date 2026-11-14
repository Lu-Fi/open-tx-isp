/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) rcfg misc: the trigger counter, the register-list reset and
 * the interrupt switch (vendor lib_tisp_rcfg_cfg_trig,
 * tisp_rcfg_t33_reset_reg_cfg, tisp_rcfg_t33_switch_interrupt).
 *
 * The vendor keeps the per-channel trigger counters in g_rcfg_trig_start_cnt
 * (16 bytes, one u32 per channel), the per-channel module attributes in
 * stModuleAttr (1792 bytes per channel, the ISP attribute pointer at +428)
 * and the init parameters in stIspInitPar (the channel count at +904).
 */
#ifndef TX_ISP_T33_RCFGMISC_H
#define TX_ISP_T33_RCFGMISC_H

#include "tx_isp_t33_blk.h"

/* vendor module data objects (defined by the module / the wrapper) */
extern u32 g_rcfg_trig_start_cnt[4];
extern u8 stIspInitPar[];
extern u8 stModuleAttr[];

/* kernel imports / runtime helpers (hardware access, ported elsewhere) */
extern void tisp_rcfg_t33_write_all_reg(u32 id);
extern u32 system_reg_read(u32 reg);
extern void system_reg_write(u32 reg, u32 val);
extern u8 *rcfg_buf_info[];

#define T33_MODATTR_STRIDE	1792U
#define T33_MODATTR_ISP_OFF	428U
#define T33_ISPINIT_CHAN_OFF	904U

/*
 * lib_tisp_rcfg_cfg_trig: write the whole register list of channel id, then
 * count the trigger of that channel.
 */
static inline void t33_rcfg_cfg_trig(u32 id)
{
	tisp_rcfg_t33_write_all_reg(id & 0xff);
	g_rcfg_trig_start_cnt[id]++;
}

/*
 * tisp_rcfg_t33_reset_reg_cfg: reset the register list of the current
 * channel (rcfg_buf_info[0] + 4 holds the list base) and the four reset
 * registers 0x2020..0x202c.
 */
static inline void t33_rcfg_reset_reg_cfg(void)
{
	u32 base = *(u32 *)((u8 *)rcfg_buf_info[0] + 4);

	system_reg_write(0x22020, base);
	system_reg_write(0x22024, 1);
	system_reg_write(0x22018, 0xffffffff);
	system_reg_write(0x2201c, 0xffffffff);
}

/*
 * tisp_rcfg_t33_switch_interrupt: switch the interrupt state of channel id
 * (0: the last channel, else id - 1), clear the ISP attribute word 3 and
 * latch the three interrupt status registers into words 21..23, then
 * enable the six interrupt mask registers.
 */
static inline void t33_rcfg_switch_interrupt(u32 id)
{
	u32 ch, isp;

	id &= 0xff;
	if (!id)
		ch = (*(u32 *)(stIspInitPar + T33_ISPINIT_CHAN_OFF) - 1) & 0xff;
	else
		ch = (id - 1) & 0xff;
	isp = *(u32 *)(stModuleAttr + ch * T33_MODATTR_STRIDE + T33_MODATTR_ISP_OFF);
	*(u32 *)(isp + 12) = 0;
	*(u32 *)(isp + 84) = system_reg_read(0xd088);
	*(u32 *)(isp + 88) = system_reg_read(0xd08c);
	*(u32 *)(isp + 92) = system_reg_read(0xd090);
	system_reg_write(0xf0144, 1);
	system_reg_write(0xf0164, 1);
	system_reg_write(0xf0244, 1);
	system_reg_write(0xf0264, 1);
	system_reg_write(0xf0344, 1);
	system_reg_write(0xf0364, 1);
}

#endif /* TX_ISP_T33_RCFGMISC_H */
