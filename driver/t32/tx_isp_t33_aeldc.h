/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) AE day/night switch, online LDC LUT refresh and the LSC
 * mirror/flip helper, ported from the vendor tx-isp-t33.ko.
 *
 * The AE context (g_ae_contexts[ch]) is a byte block; tisp_ae_switch_para
 * pushes its day/night fields to the AE registers (tisp_reg_write_t33) and
 * then refreshes the day/night parameters (normal_ae_dn_par_refresh).
 * tisp_online_ldc_reflesh_lut either writes the LDC LUT base registers
 * (system_reg_write) when stIspInitPar[904] == 1, or triggers the LDC
 * register list (tisp_reg_write_t33) with the LUT address.
 * do_mirror_flip mirrors/flips a 16-bit image in place (memcpy/memmove
 * through a private_vmalloc buffer).
 */
#ifndef TX_ISP_T33_AELDC_H
#define TX_ISP_T33_AELDC_H

#include "tx_isp_t33_blk.h"

/* vendor: tisp_reg_write_t33 (proven runtime, tx_isp_t32_hw.c) */
extern int tisp_reg_write_t33(u32 id, u32 reg, u32 n, u32 val);
/* vendor: normal_ae_dn_par_refresh (open ioctl) */
extern int normal_ae_dn_par_refresh(u32 ch);
/* vendor: system_reg_write (shared-t32 runtime, tx_isp_t33_msca_api6.h) */
extern int system_reg_write(u32 reg, u32 val);
/* vendor: private_vmalloc / private_vfree (shared-t32 runtime) */
extern void *private_vmalloc(u32 size);
extern void private_vfree(void *p);
/* vendor: memcpy / memmove (kernel imports; declared by <string.h> in user space) */
#ifndef __KERNEL__
#include <string.h>
#else
#include <linux/string.h>
#endif

/* module data objects (vendor symbols) */
extern u8 ldcCfgMode;
extern u8 stIspInitPar[];

/* g_ae_contexts: the vendor's per-channel AE context pointer array (.bss+0xa1d0) */
extern u32 g_ae_contexts[];

/*
 * tisp_ae_switch_para: write the day/night fields of the channel's AE
 * context to the AE registers (reg 30, sub-addresses 3, 4, 6, 15, 1) and
 * refresh the day/night parameters.
 */
static inline int t33_ae_switch_para(u32 ch)
{
	u8 *c = (u8 *)g_ae_contexts[ch];
	u32 id = ch & 0xff;

	tisp_reg_write_t33(id, 30, 3, ((u32)(t32_tp_le16(c) & 0xfff) << 16) |
				     (t32_tp_le16(c + 2) & 0xfff));
	tisp_reg_write_t33(id, 30, 4, ((u32)c[7] << 16) | c[12]);
	tisp_reg_write_t33(id, 30, 6, ((u32)c[14] << 8) | c[5] | 0x110000);
	tisp_reg_write_t33(id, 30, 15, 0x10010000 | (c[16] & 3) | ((c[8] & 3) << 4));
	tisp_reg_write_t33(id, 30, 1, 1);
	return normal_ae_dn_par_refresh(ch);
}

/*
 * tisp_online_ldc_reflesh_lut: when stIspInitPar[904] == 1 write the LDC
 * LUT base registers directly, else trigger the LDC register list with the
 * LUT address (0x22870c when ldcCfgMode is set, else 5).
 */
static inline int t33_online_ldc_refresh(u32 ch, u32 addr)
{
	if (t33_le32(stIspInitPar + 904) == 1) {
		system_reg_write(0x2706c, addr);
		system_reg_write(0x27020, 0xffffffff);
	} else {
		tisp_reg_write_t33(ch & 0xff, 25, ldcCfgMode ? 5 : 0x2706c, addr);
	}
	return 0;
}

/*
 * do_mirror_flip: mirror and/or flip a column-major w x h image of 16-bit
 * pixels in place.  mirror swaps the left/right halves by exchanging whole
 * columns; flip swaps the top/bottom halves of each column.
 */
static inline void t33_mirror_flip(u8 *img, u32 w, u32 h, u32 mirror, u32 flip)
{
	u32 half, x, y, colbytes;
	u8 *left, *right, *top, *bot, *tmp;
	u16 ta, tb;

	if (mirror == 1) {
		half = w >> 1;
		colbytes = h << 1;
		left = img;
		right = img + (w - 1) * colbytes;
		for (x = 0; x < half; x++) {
			tmp = (u8 *)private_vmalloc(colbytes);
			memcpy(tmp, left, colbytes);
			memmove(left, right, colbytes);
			memcpy(right, tmp, colbytes);
			private_vfree(tmp);
			left += colbytes;
			right -= colbytes;
		}
	}
	if (flip == 1) {
		half = h >> 1;
		colbytes = h << 1;
		for (y = 0; y < w; y++) {
			top = img + y * colbytes;
			bot = top + colbytes - 2;
			for (x = 0; x < half; x++) {
				ta = t32_tp_le16(top);
				tb = t32_tp_le16(bot);
				top[0] = (u8)tb;
				top[1] = (u8)(tb >> 8);
				bot[0] = (u8)ta;
				bot[1] = (u8)(ta >> 8);
				top += 2;
				bot -= 2;
			}
		}
	}
}

#endif /* TX_ISP_T33_AELDC_H */
