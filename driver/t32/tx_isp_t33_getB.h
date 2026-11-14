/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) block "getB": LSC info allocation and the AE sensor
 * trigger, ported from the vendor tx-isp-t33.ko (openimp #99).
 *
 * tisp_lsc_malloc_info(ch): allocate the LSC info block (24576 bytes,
 * 208-byte alignment) and store it at word 4 of lsc_para[ch]; the
 * 8-byte aligned address goes to word 4 and that + 0x80000000 to word 5.
 *
 * tisp_normal_ae_par_sensor_trig(ch): when ae_init_flag[ch] == 1, write 1
 * to byte 1285 of the block ae_cache[ch] points to.
 */
#ifndef TX_ISP_T33_GETB_H
#define TX_ISP_T33_GETB_H

#include "tx_isp_t33_blk.h"

/* the vendor's __kmalloc(size, flags) (kernel import, CALLS) */
extern void *__kmalloc(u32 size, u32 flags);

/* module statics (GLOBAL lines of the oracle) */
extern u32 *lsc_para[4];
extern u8 ae_init_flag[4];
extern u32 *ae_cache[4];

/* tisp_lsc_malloc_info: allocate lsc_para[ch]'s info block */
static inline void t33_lsc_malloc_info(u32 ch)
{
	u32 *p = lsc_para[ch & 0xff];
	u32 a;

	p[4] = (u32)__kmalloc(24576, 208);
	a = p[4] & 7;
	if (a)
		p[4] = p[4] + 8 - a;
	p[5] = p[4] + 0x80000000U;
}

/* tisp_normal_ae_par_sensor_trig: flag the AE sensor trigger for channel ch */
static inline void t33_normal_ae_par_sensor_trig(u32 ch)
{
	if (ae_init_flag[ch] != 1)
		return;
	((u8 *)ae_cache[ch])[1285] = 1;
}

#endif /* TX_ISP_T33_GETB_H */
