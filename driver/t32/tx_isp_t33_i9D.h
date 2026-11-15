/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) block "i9D": tisp_top_init, tisp_top_ipc_init, tisp_af_init
 * (the vendor's module bring-up of the TOP, TOP-IPC and AF blocks).
 *
 * Module data the vendor keeps as statics are plain globals here (t33_top_*,
 * t33_af_*); callees ported elsewhere or touching hardware are declared with
 * their vendor names.
 */
#ifndef TX_ISP_T33_I9D_H
#define TX_ISP_T33_I9D_H

#include "tx_isp_t33_blk.h"
#include "tx_isp_t33_top.h"

#ifdef __KERNEL__
#include <linux/printk.h>
#include <linux/string.h>
#else
extern int printk(const char *fmt, ...);
#endif

/* module data (vendor: tparamsP, deir_en, stIspInitPar, bayer_remap, tispAfRefresh, af_info) */
extern u32 *t33_top_tparams[256];
extern u32 t33_top_deir_en[256];
extern u32 t33_top_isp_init_par[256 * 252];	/* 1008 bytes per channel */
extern u32 t33_top_bayer_remap[32];
extern u8 tispAfRefresh;
extern u8 *t33_af_info[256];

extern int tisp_rcfg_module_register(u32 id, u32 mod, void *init);
extern void tisp_rcfg_ipc_reg_init(void);
extern void tisp_rcfg_af_reg_init(void);
extern int tisp_reg_write_t33(u32 id, u32 mod, u32 reg, u32 val);
extern void system_reg_write(u32 reg, u32 val);
extern void system_irq_func_set(u32 id, u32 n, void *fn);
extern void tisp_top_restart_cfg(u32 id);
extern void tisp_top_t33_pipe_null_down_interrupt(void);
extern void tisp_af_malloc_cfg(u32 id);
extern void *private_vmalloc(u32 size);
extern int isp_printf(u32 lvl, const char *fmt, ...);
extern void tisp_af_par_init(u32 id);
extern void tisp_af_t33_all_reg_write(u32 id, void *par);
extern void tisp_af_interrupt_static(void);
extern void tisp_af_calc_process(void);
extern void tisp_event_set_cb(u32 id, u32 ev, void *fn);

#define T33_AF_INFO_BYTES	1264U

/* tisp_top_init: info = sensor info (size +12 / +14, bayer +4, bytes 40 / 43) */
static inline int t33_top_start(u32 id, const u8 *info)
{
	u8 ch = id & 0xff;
	u32 bayer, idx, deir, remap, hi, v, a3;
	int r;

	r = tisp_rcfg_module_register(ch, 33, (void *)tisp_rcfg_ipc_reg_init);
	tispTopRefresh = 0;
	if (r < 0)
		printk("t33 top_init: module register failed %d\n", r);
	stTopPar[ch] = t33_top_tparams[ch];
	tisp_reg_write_t33(ch, 33, ipcCfgMode ? 6 : 112,
			   (u32)t32_tp_le16(info + 12) << 16 | t32_tp_le16(info + 14));
	bayer = t33_le32(info + 4);
	idx = bayer & 0x1f;
	remap = t33_top_bayer_remap[idx];
	deir = idx < 4 ? 0 : 1;
	t33_top_deir_en[ch] = deir;
	t33_top_isp_init_par[ch * 252 + 2] = deir;
	hi = ((bayer >> 16) & 3) << 8;
	v = (u32)info[40] << 16 | remap | hi;
	tisp_reg_write_t33(ch, 33, ipcCfgMode ? 8 : 120, v | t33_top_deir_en[0] << 20);
	if ((ch & 0xfd) == 0)
		system_reg_write(136, v | t33_top_deir_en[0] << 20);
	else if ((ch & 0xfd) == 1)
		system_reg_write(392, v | t33_top_deir_en[1] << 20);
	else
		printk("t33 top_init: bad id %u\n", (u32)ch);
	switch (info[43]) {
	case 0:
		a3 = 4;
		break;
	case 1:
		a3 = ch == 0 ? 5 : 4;
		break;
	case 2:
		a3 = ch < 2 ? 5 : 4;
		break;
	case 3:
		a3 = ch < 3 ? 5 : 4;
		break;
	default:
		a3 = 5;
		break;
	}
	tisp_reg_write_t33(ch, 33, ipcCfgMode ? 10 : 256, a3);
	tisp_top_reg_cfg(ch);
	tisp_top_restart_cfg(ch);
	tisp_reg_write_t33(ch, 33, ipcCfgMode ? 12 : 756, 5);
	system_irq_func_set(ch, 0, (void *)tisp_top_t33_pipe_null_down_interrupt);
	return 0;
}

/* tisp_top_ipc_init: info = sensor info (width +12, height +14, mode word +904) */
static inline int t33_top_ipc_start(const u8 *info)
{
	u32 state = t33_le32(info + 904);
	int area;

	if (state == 1) {
		system_reg_write(4224, 0);
		system_reg_write(4228, 0);
		system_reg_write(4256, 0xffffffffU);
		system_reg_write(4260, 4);
		area = (int)((u32)t32_tp_le16(info + 12) * t32_tp_le16(info + 14)) / 6;
		system_reg_write(4292, (u32)area);
		area = (int)((u32)t32_tp_le16(info + 12) * t32_tp_le16(info + 14)) / 6;
		system_reg_write(4296, (u32)area);
	} else {
		system_reg_write(4224, state == 3 ? 2 : 3);
		system_reg_write(4228, state == 3 ? 2 : 10);
		system_reg_write(4256, 0xffffffffU);
		system_reg_write(4260, 17476);
	}
	system_reg_write(4300, 0xffffffffU);
	system_reg_write(4304, 0xffffffffU);
	return 0;
}

/* tisp_af_init: info = sensor info (size +12 / +14) */
static inline int t33_af_bringup(u32 id, const u8 *info)
{
	u8 ch = id & 0xff;
	u8 *blk;

	tisp_rcfg_module_register(ch, 31, (void *)tisp_rcfg_af_reg_init);
	tispAfRefresh = 0;
	tisp_af_malloc_cfg(id);
	blk = (u8 *)private_vmalloc(T33_AF_INFO_BYTES);
	t33_af_info[id] = blk;
	if (!blk)
		isp_printf(2, "t33 af_init: vmalloc failed [%d]\n", 1102);
	memset(t33_af_info[id], 0, T33_AF_INFO_BYTES);
	blk = t33_af_info[id];
	*(u32 *)(blk + 1256) = (u32)(unsigned long)t33_top_tparams[id] + 0x9fec;
	*(u16 *)(blk + 1260) = t32_tp_le16(info + 12);
	*(u16 *)(blk + 1262) = t32_tp_le16(info + 14);
	tisp_af_par_init(id);
	tisp_af_t33_all_reg_write(id, blk + 1028);
	system_irq_func_set(ch, 6, (void *)tisp_af_interrupt_static);
	tisp_event_set_cb(id, 15, (void *)tisp_af_calc_process);
	return 0;
}

#endif /* TX_ISP_T33_I9D_H */
