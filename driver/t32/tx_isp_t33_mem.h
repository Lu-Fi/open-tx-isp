/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) ISP working memory size (openimp #99): tisp_mdns_t33_get_mem_size, which the T33
 * lib_tisp_get_isp_memory_space always takes (core init sets stIspInitPar word 0 = 1).  Differs from the
 * T32 tisp_mdns_get_mem_size (tx_isp_t32_mem.h) in the uncompressed plane (w * h * 3 / 2), the statistics
 * plane (16-group rows of 8 bytes, (h + 7) / 8 rows) and the second plane ((w / 2 + 7) & ~7 per row pair).
 * Proven by tests/t33_binoracle block "t33mem".
 */
#ifndef TX_ISP_T33_MEM_H
#define TX_ISP_T33_MEM_H

#define T33_MDNS_E_MEMOPT	0x80032002U	/* vendor: compression ratio > 2000 */

/* memopt: the compression ratio (1/1000, u16); sel: 1 = no second plane; pad_mb: whole MiB on top */
static inline u32 t33_mdns_get_mem_size(u32 memopt, u32 width, u32 height, u32 sel, u32 pad_mb)
{
	u32 w = width & 0xffff, h = height & 0xffff, z, t;

	memopt &= 0xffff;
	if (memopt <= 1000) {
		z = (u32)(((int)(w * h * 3) / 2 + 0xff) / 0x100);
	} else if (memopt <= 2000) {
		z = (((((u32)((int)(w + 31) >> 5) * h * (u32)(0x5dc00 / (int)memopt) + 0x3f) >> 6) << 3) + 0xff) >> 8;
	} else {
		return T33_MDNS_E_MEMOPT;
	}
	t = ((((((((w + 7) >> 3) + 15) >> 4) << 3) * (h + 7)) >> 3) + 0x3ff) & ~0x3ffU;
	t += (z + 2) << 8;
	if ((sel & 0xff) != 1)
		t += ((((((w >> 1) + 7) & 0xfff8) * h) >> 1) + 0x3ff) & ~0x3ffU;
	return ((0x1000 - (t & 0xfff)) & 0xffff) + ((pad_mb & 0xff) << 20) + t;
}

#endif /* TX_ISP_T33_MEM_H */
