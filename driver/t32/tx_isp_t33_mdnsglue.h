/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) wiring group 16 (openimp #99): MDNS on the vendor layout.
 *
 * - tisp_mdns_init (tx_isp_t33_mdnsR.h, oracle mdnsR) at lib_tisp_init:
 *   mdnsInfo[0] = t33_mdns_v (8808 bytes), its section word 2180 at the
 *   section copy t33_mdns_sec (tparamsP + 25144), the five statistics tables
 *   (t33_swg_mdns_g), uvLimitFlag = 1, the compression set-up for zip ratios
 *   11..20 (tisp_mdns_cmp_par_calc of mdnsP + tisp_mdns_t33_cmp_reg_write of
 *   mdnsA), the RISC-V words 0 (riscvEnable 0), 0xd084 = 2 direct (single
 *   sensor), tisp_mdns_refresh at the init gain, start word, mdnsInit;
 * - tisp_mdns_refresh (mdnsP) -> tisp_mdns_par_refresh (tx_isp_t33_swmdns.h)
 *   at every long-gain update, with the vendor's own gain gate (last gain
 *   word 2181, tolerance word 2182 = stIspInitPar + 1000) and tispMdnsRefresh
 *   = 1 on every call: system irq 50 (tisp_mdns_t33_frame_done_interrupt,
 *   t33_mdns_fd) then runs list 15;
 * - the day / night switch (tx_isp_t33_swglue.h) copies the new section into
 *   t33_mdns_sec first (the vendor's bin parse into tparamsP).
 * Oracle mdnspr (tests/t33_binoracle/mdnspr.sh, tools/ko_emu/diff_t33_mdnspr.py):
 * the init -> refresh -> par_refresh / switch chain with all callees in this
 * form against the vendor module in Unicorn, real PRJ008 day / night sections
 * and gain traces of QEMU vendor runs.
 *
 * The init parameters the port reads (sensor size bytes 12 / 14, zip ratio
 * byte 46) are not kept in the driver's stIspInitPar; the init gets its own
 * copy with them (the driver's memopt in 1/1000 = the ratio in tenths * 100).
 * tisp_mdns_t33_set_mem_cfg and the YSP decode words stay where the driver
 * runs them (after the init, tx_isp_t32_hw.c).  Only tx_isp_t32_hw.c
 * (TX_ISP_T33) includes this file, after tx_isp_t33_g14glue.h.
 */
#ifndef TX_ISP_T33_MDNSGLUE_H
#define TX_ISP_T33_MDNSGLUE_H

/* t33_mdns_sec (the section copy, tparamsP + 25144) and t33_swg_stmem (stMemInfo): tx_isp_t33_swglue.h */

static u32 t33_mdnsg_info[4], t33_mdnsg_tp[4];	/* mdnsInfo, tparamsP of the ports (u32 words) */
static u8 t33_mdnsg_riscv[4], t33_mdnsg_riscv_mdns, t33_mdnsg_rpar[4];	/* riscvEnable, riscvMdnsEn, stRiscvPara */
static u32 t33_mdnsg_ip[252];			/* the init parameters tisp_mdns_init reads (channel 0) */
u32 t33_mdnsg_runs[4];	/* init, refresh calls, refreshes past the gain gate, irq / alloc mismatches */

static void *t33_mdnsg_kmalloc(u32 size, u32 flags)
{
	(void)flags;
	return size == sizeof(t33_mdns_v) ? t33_mdns_v : NULL;
}

static void t33_mdnsg_lock_init(void *lock, const char *name, void *key)
{
	(void)lock;
	(void)name;
	(void)key;
}

static int t33_mdnsg_irq(u32 ch, u32 irq, void *fn)
{
	(void)ch;
	(void)fn;
	if (irq != 50)		/* t33_bst_irq dispatches irq 50 (msca bit 18) to t33_mdns_fd */
		t33_mdnsg_runs[3]++;
	return 0;
}

static void t33_mdnsg_sysw(u32 reg, u32 val)
{
	isp_wr(reg, val);
}

static int t33_mdnsg_printf(u32 level, const char *fmt, ...)
{
	(void)fmt;
	if (level)
		t33_mdnsg_runs[3]++;	/* the alloc failure (level 2); level 0 is the zip info line */
	return 0;
}

/* tisp_mdns_par_refresh(ch, gain, thr) on mdnsInfo[ch]: the swmdns port with the switch's ops */
static void t33_mdnsg_par_refresh(u32 ch, u32 gain, u32 thr)
{
	u32 *inf = (u32 *)(unsigned long)t33_mdnsg_info[ch & 3], last = inf[2181];

	t33_mdns_par_refresh(&t33_swg_mdns_ops, ch, inf, gain, thr, t33_bst_cfg_mode, t33_dn_uv_limit[0], t33_swg_stmem,
			     t33_mdnsg_riscv[ch & 3] && t33_mdnsg_riscv_mdns, &t33_bst_refresh_mdns);
	t33_mdnsg_runs[1]++;
	if (inf[2181] != last || last == 0xffffffffU)
		t33_mdnsg_runs[2]++;
}

#define tisp_mdns_par_refresh	t33_mdnsg_par_refresh
#include "tx_isp_t33_mdnsP.h"
#undef tisp_mdns_par_refresh

static void t33_mdnsg_cmp_par_calc(u32 c, u32 h, u32 w)
{
	t33_mdns_cmp_par((u32 *)(unsigned long)t33_mdnsg_info[c & 3], h, w);	/* tisp_mdns_cmp_par_calc (mdnsP) */
}

static void t33_mdnsg_cmp_reg_write(u32 c)
{
	t33_mdns_cmp_reg_write(c, (const u32 *)(unsigned long)t33_mdnsg_info[c & 3], t33_bst_cfg_mode,
			       tisp_reg_write_t33);	/* tisp_mdns_t33_cmp_reg_write (mdnsA) */
}

static int t33_mdnsg_refresh(u32 c, u32 gain)
{
	return t33_mdns_refresh(c, gain, (u32 *)(unsigned long)t33_mdnsg_info[c & 3]);	/* tisp_mdns_refresh */
}

#define mdnsInfo			t33_mdnsg_info
#define tparamsP			t33_mdnsg_tp
#define stMemInfo			t33_swg_stmem
#define uvLimitFlag			t33_dn_uv_limit
#define mdnsCfgMode			t33_bst_cfg_mode
#define riscvEnable			t33_mdnsg_riscv
#define riscvMdnsEn			t33_mdnsg_riscv_mdns
#define stRiscvPara			t33_mdnsg_rpar
#define mdnsInit			t33_mdns_init_flag
#define global_ybsnthres		t33_swg_mdns_g[0]
#define global_ypbv0stathres		t33_swg_mdns_g[1]
#define global_ypbv1stathres		t33_swg_mdns_g[2]
#define global_yrefweimin		t33_swg_mdns_g[3]
#define global_yrefweiminadjthres	t33_swg_mdns_g[4]
#define tisp_rcfg_mdns_reg_init		NULL
#define tisp_mdns_t33_frame_done_interrupt	NULL
#define tisp_rcfg_module_register	t33_g14_reg
#define isp_printf			t33_mdnsg_printf
#define private_kmalloc			t33_mdnsg_kmalloc
#define private_raw_mutex_init		t33_mdnsg_lock_init
#define tisp_mdns_cmp_par_calc		t33_mdnsg_cmp_par_calc
#define tisp_mdns_t33_cmp_reg_write	t33_mdnsg_cmp_reg_write
#define system_reg_write		t33_mdnsg_sysw
#define tisp_mdns_refresh		t33_mdnsg_refresh
#define system_irq_func_set		t33_mdnsg_irq
#include "tx_isp_t33_mdnsR.h"
#undef mdnsInfo
#undef tparamsP
#undef stMemInfo
#undef uvLimitFlag
#undef mdnsCfgMode
#undef riscvEnable
#undef riscvMdnsEn
#undef stRiscvPara
#undef mdnsInit
#undef global_ybsnthres
#undef global_ypbv0stathres
#undef global_ypbv1stathres
#undef global_yrefweimin
#undef global_yrefweiminadjthres
#undef tisp_rcfg_mdns_reg_init
#undef tisp_mdns_t33_frame_done_interrupt
#undef tisp_rcfg_module_register
#undef isp_printf
#undef private_kmalloc
#undef private_raw_mutex_init
#undef tisp_mdns_cmp_par_calc
#undef tisp_mdns_t33_cmp_reg_write
#undef system_reg_write
#undef tisp_mdns_refresh
#undef system_irq_func_set

/*
 * lib_tisp_init's tisp_mdns_init (channel 0) on the bin section sec (copied into the tparamsP slot), sensor
 * w x h, zip ratio from the driver's memopt.  Returns 0 when the block is set up (mdnsInfo[0] set).
 */
static int t33_mdnsg_init(const u8 *sec, u32 w, u32 h, u32 memopt)
{
	unsigned long flags;
	u8 *ip = (u8 *)t33_mdnsg_ip;

	/* the list page is allocated (GFP_KERNEL) before the interrupts go off; reg_register then finds it */
	if (!t33_mod_buf(T33_MOD_MDNS))
		return -1;
	memcpy(t33_mdns_sec, sec, sizeof(t33_mdns_sec));
	memcpy(t33_mdnsg_ip, t33_st_isp_init_par_mode, sizeof(t33_mdnsg_ip));
	ip[12] = w & 0xff;
	ip[13] = w >> 8 & 0xff;
	ip[14] = h & 0xff;
	ip[15] = h >> 8 & 0xff;
	ip[46] = memopt / 100 > 0xff ? 0xff : memopt / 100;	/* the ratio in tenths (10: off, 11..20) */
	t33_mdnsg_tp[0] = (u32)(unsigned long)t33_mdns_sec - T33_TP_OFF_MDNS;
	t33_mdnsg_info[0] = 0;
	local_irq_save(flags);
	t33_tisp_mdns_init(0, ip);
	/* the init's tispMdnsRefresh = 1 is the stream-on mark of T33_INIT_REFRESH (as group 14) */
	t33_bst_refresh_mdns = 0;
	local_irq_restore(flags);
	t33_mdnsg_runs[0]++;
	return t33_mdnsg_info[0] ? 0 : -1;
}

/* tisp_long_tgain_update's tisp_mdns_refresh (process context, t33_gr_lock held) */
static void t33_mdnsg_gain(u32 gain)
{
	unsigned long flags;

	if (!t33_mdnsg_info[0] || !(t33_mod_built & (1ULL << T33_MOD_MDNS)))
		return;
	local_irq_save(flags);
	t33_mdnsg_refresh(0, gain);
	local_irq_restore(flags);
}

#endif /* TX_ISP_T33_MDNSGLUE_H */
