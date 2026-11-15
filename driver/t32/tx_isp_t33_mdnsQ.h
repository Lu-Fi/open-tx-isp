/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) MDNS teardown / power: tisp_mdns_deinit (free the block
 * state, release module list 15, clear the per-channel statistics tables
 * and riscvMdnsEn), tisp_mdns_sleep / tisp_mdns_wake_up (save / restore
 * the four 0xd080..0xd090 unit-count registers when the single-sensor
 * path is active, stIspInitPar byte 904 = 1).
 *
 * The statistics tables are the vendor's global_* arrays, 11 bytes per
 * channel; the saved registers live in tisp_mdns_ref_info (5 words).
 */
#ifndef TX_ISP_T33_MDNSQ_H
#define TX_ISP_T33_MDNSQ_H

#ifndef __KERNEL__
#include <string.h>
#endif
#include "tx_isp_t33_blk.h"

#define T33_MDNSQ_CH_BYTES	11

#ifndef __KERNEL__
/* the vendor's per-channel statistics tables (11 bytes per channel) */
extern u8 global_ybsnthres[];
extern u8 global_ypbv0stathres[];
extern u8 global_ypbv1stathres[];
extern u8 global_yrefweimin[];
extern u8 global_yrefweiminadjthres[];
/* mdnsInfo[4]: the block state pointers */
extern u32 mdnsInfo[4];
/* riscvMdnsEn: the RISC-V MDNS enable byte */
extern u8 riscvMdnsEn;
/* tisp_mdns_ref_info[5]: the saved 0xd080..0xd090 values */
extern u32 tisp_mdns_ref_info[5];
/* stIspInitPar: the ISP init parameters (byte 904 = 1: single sensor) */
extern u8 stIspInitPar[];

/* kernel imports / proven runtime, not run by the oracle */
extern void private_kfree(void *p);
extern int tisp_rcfg_module_release(u32 chn, u32 blk);
extern int system_reg_read(u32 reg);
extern void system_reg_write(u32 reg, u32 val);
extern int printk(const char *fmt, ...);
#endif

/* tisp_mdns_deinit: free mdnsInfo[chn], release list 15, clear the tables */
static inline void t33_mdns_deinit(u32 chn)
{
	u32 c = chn & 0xff;
	u8 *p;

	if (mdnsInfo[c]) {
		private_kfree((void *)mdnsInfo[c]);
		mdnsInfo[c] = 0;
	}
	if (tisp_rcfg_module_release(c, 15) < 0)
		printk("tisp_mdns_deinit: release failed\n");
	riscvMdnsEn = 0;
	p = global_ybsnthres + c * T33_MDNSQ_CH_BYTES;
	memset(p, 0, T33_MDNSQ_CH_BYTES);
	p = global_ypbv0stathres + c * T33_MDNSQ_CH_BYTES;
	memset(p, 0, T33_MDNSQ_CH_BYTES);
	p = global_ypbv1stathres + c * T33_MDNSQ_CH_BYTES;
	memset(p, 0, T33_MDNSQ_CH_BYTES);
	p = global_yrefweimin + c * T33_MDNSQ_CH_BYTES;
	memset(p, 0, T33_MDNSQ_CH_BYTES);
	p = global_yrefweiminadjthres + c * T33_MDNSQ_CH_BYTES;
	memset(p, 0, T33_MDNSQ_CH_BYTES);
}

/* tisp_mdns_sleep: save the four unit-count registers (single sensor only) */
static inline void t33_mdns_sleep(void)
{
	if (*(u32 *)(stIspInitPar + 904) != 1)
		return;
	tisp_mdns_ref_info[0] = (u32)system_reg_read(0xd080);
	tisp_mdns_ref_info[1] = (u32)system_reg_read(0xd084);
	tisp_mdns_ref_info[2] = (u32)system_reg_read(0xd088);
	tisp_mdns_ref_info[3] = (u32)system_reg_read(0xd08c);
	tisp_mdns_ref_info[4] = (u32)system_reg_read(0xd090);
}

/* tisp_mdns_wake_up: restore the four unit-count registers (single sensor only) */
static inline void t33_mdns_wake_up(void)
{
	if (*(u32 *)(stIspInitPar + 904) != 1)
		return;
	system_reg_write(0xd080, tisp_mdns_ref_info[0]);
	system_reg_write(0xd084, tisp_mdns_ref_info[1]);
	system_reg_write(0xd088, tisp_mdns_ref_info[2]);
	system_reg_write(0xd08c, tisp_mdns_ref_info[3]);
	system_reg_write(0xd090, tisp_mdns_ref_info[4]);
}

#endif /* TX_ISP_T33_MDNSQ_H */
