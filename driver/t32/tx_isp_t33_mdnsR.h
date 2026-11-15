/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) tisp_mdns_init as the vendor module runs it: register the
 * module list 15, allocate and clear the block state (8808 bytes), copy the
 * five statistics tables out of the IQ bin, derive the compression setup from
 * the zip-ratio byte of the channel's init parameters and program the
 * RISC-V / unit-count registers.  The callees are ported and proven elsewhere
 * or touch hardware: they are declared extern here with the vendor names.
 */
#ifndef TX_ISP_T33_MDNSR_H
#define TX_ISP_T33_MDNSR_H

#ifndef __KERNEL__
#include <string.h>
#endif
#include "tx_isp_t33_blk.h"

#ifndef __KERNEL__
extern u32 mdnsInfo[4];
extern u32 tparamsP[4];
extern u8 stMemInfo[];
extern u8 uvLimitFlag[];
extern u8 mdnsCfgMode;
extern u8 riscvEnable[];
extern u8 riscvMdnsEn;
extern u8 stRiscvPara[];
extern u8 stIspInitPar[];
extern u8 mdnsInit;
extern u8 global_ybsnthres[];
extern u8 global_ypbv0stathres[];
extern u8 global_ypbv1stathres[];
extern u8 global_yrefweimin[];
extern u8 global_yrefweiminadjthres[];

extern u32 tisp_rcfg_mdns_reg_init(u32 *e);
extern int tisp_mdns_t33_frame_done_interrupt(u32 id);
extern int tisp_rcfg_module_register(u32 chn, u32 blk, u32 (*init)(u32 *e));
extern int printk(const char *fmt, ...);
extern int isp_printf(u32 level, const char *fmt, ...);
extern void *private_kmalloc(u32 size, u32 flags);
extern void private_raw_mutex_init(void *lock, const char *name, void *key);
extern void tisp_mdns_cmp_par_calc(u32 chn, u32 h, u32 w);
extern void tisp_mdns_t33_cmp_reg_write(u32 chn);
extern int tisp_reg_write_t33(u32 id, u32 blk, u32 reg, u32 val);
extern void system_reg_write(u32 reg, u32 val);
extern int tisp_mdns_refresh(u32 chn, u32 gain);
extern int system_irq_func_set(u32 id, u32 irq, int (*fn)(u32 id));
#endif

static u8 t33_mdns_lock_key[4];

static inline void t33_mdns_put16(u8 *p, u32 v)
{
	p[0] = v & 0xff;
	p[1] = v >> 8 & 0xff;
}

/* tisp_reg_write_t33 with the list slot (CfgMode) or the register */
static inline void t33_mdns_rw(u32 c, u32 mode, u32 slot, u32 reg, u32 val)
{
	tisp_reg_write_t33(c, 15, mode ? slot : reg, val);
}

/* tisp_mdns_init: chn_in channel, ip the channel's init parameter block (1008 bytes) */
static inline int t33_tisp_mdns_init(u32 chn_in, const u8 *ip)
{
	u32 c = chn_in & 0xff, w = ip[12] | ip[13] << 8, h = ip[14] | ip[15] << 8, zr = ip[46];
	u32 r100 = zr * 100, mode, gain;
	volatile u32 n11 = 11;
	u8 *blk, *rp;
	u32 *bw;
	int ret;

	ret = tisp_rcfg_module_register(c, 15, tisp_rcfg_mdns_reg_init);
	if (ret < 0)
		printk("%s[%d] register failed %d\n", "tisp_mdns_init", 7403, ret);
	blk = (u8 *)(unsigned long)private_kmalloc(8808, 208);
	mdnsInfo[c] = (u32)(unsigned long)blk;
	if (!blk)
		isp_printf(2, "%s[%d] malloc failed\n", "tisp_mdns_init", 7408);
	memset(blk, 0, 8808);
	bw = (u32 *)blk;
	bw[8720 / 4] = tparamsP[c] + 25144;
	memcpy(global_ybsnthres + 11 * c, (void *)(unsigned long)(tparamsP[c] + 29526), n11);
	memcpy(global_ypbv0stathres + 11 * c, (void *)(unsigned long)(tparamsP[c] + 29570), n11);
	memcpy(global_ypbv1stathres + 11 * c, (void *)(unsigned long)(tparamsP[c] + 29636), n11);
	memcpy(global_yrefweimin + 11 * c, (void *)(unsigned long)(tparamsP[c] + 30316), n11);
	memcpy(global_yrefweiminadjthres + 11 * c, (void *)(unsigned long)(tparamsP[c] + 30349), n11);
	uvLimitFlag[c] = 1;
	if (zr - 11 < 10) {
		bw[8772 / 4] = 0;
		bw[8760 / 4] = 1;
		t33_mdns_put16(blk + 8804, r100);
		tisp_mdns_cmp_par_calc(c, h, w);
		tisp_mdns_t33_cmp_reg_write(c);
		t33_mdns_put16(stMemInfo + c * 76 + 8, r100);
	} else {
		bw[8760 / 4] = 0;
		t33_mdns_put16(stMemInfo + c * 76 + 8, 1000);
		if (zr != 10)
			printk("%s[%d] bad zip ratio %d\n", "tisp_mdns_init", 7438, zr);
	}
	isp_printf(0, "%s[%d] zip %d %d\n", "tisp_mdns_init", 7441, zr, bw[8760 / 4]);
	bw[8736 / 4] = w;
	bw[8740 / 4] = h;
	bw[8724 / 4] = 0xffffffffU;
	bw[8728 / 4] = t33_le32(ip + 1000);
	mode = mdnsCfgMode;
	if (riscvEnable[c] == 1 && riscvMdnsEn == 1) {
		rp = stRiscvPara + c * 12800;
		t33_mdns_rw(c, mode, 20, 0xd088, t33_le32(rp + 12624));
		t33_mdns_rw(c, mode, 21, 0xd08c, t33_le32(rp + 12628));
		t33_mdns_rw(c, mode, 22, 0xd090, t33_le32(rp + 12632));
	} else {
		t33_mdns_rw(c, mode, 20, 0xd088, 0);
		t33_mdns_rw(c, mode, 21, 0xd08c, 0);
		t33_mdns_rw(c, mode, 22, 0xd090, 0);
	}
	gain = t33_le32(ip + 928);
	if (t33_le32(stIspInitPar + 904) == 1) {
		system_reg_write(0xd084, 2);
	} else {
		t33_mdns_rw(c, mode, 19, 0xd084, 2);
	}
	tisp_mdns_refresh(c, gain);
	private_raw_mutex_init(blk + 8748, "mdns_lock", t33_mdns_lock_key);
	t33_mdns_rw(c, mode, 2, 0xd018, 1);
	mdnsInit = 1;
	system_irq_func_set(c, 50, tisp_mdns_t33_frame_done_interrupt);
	return 0;
}

#endif /* TX_ISP_T33_MDNSR_H */
