/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) IQ bin appended-data trailer parser (vendor tisp_bin_append_data_parse).
 *
 * Header: +4 = non-zero when records follow; the first record is at +8.
 * Record: +0 module id (tisp_get_module_enum), +16 LUT present flag,
 * +20 absolute offset of the next record (0 or >= length ends the walk),
 * +24 LUT data size, +28 payload.
 *
 * LUT entries are 13830 bytes in the bin, but are stored into the private
 * buffer with a 13826 byte stride (6913 u16 per entry).  Per channel: a
 * 28-byte lutMaxSize record (allocated size at +4), stLutInfo[ch] (pointer to
 * an info object: u16 used bytes at +2, data pointer at +20) and the
 * tparamsP[ch] block (LUT count byte at 17636, flags at 17579.., u16 at 8+..).
 */
#ifndef TX_ISP_T33_BINAPP_H
#define TX_ISP_T33_BINAPP_H

#include "tx_isp_t33_blk.h"
#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

#define T33_BINAPP_MAX_SECTIONS	32
#define T33_BINAPP_LUT_DIV	13830
#define T33_BINAPP_LUT_STRIDE	13826
#define T33_BINAPP_LUT_HALF	6913

#define T33_BINAPP_MOD_LDC	15
#define T33_BINAPP_MOD_LUT	16

/* vendor module data objects (defined by the caller / wrapper) */
extern u32 lutMaxSize[28];
extern u32 stLutInfo[4];
extern u8 *tparamsP[4];

/* callees: ported elsewhere or kernel imports */
extern int tisp_get_module_enum(const u8 *p);
extern int tisp_ldc_get_extra_par(u32 ch, const u8 *info);
extern void *private_vmalloc(u32 n);
extern void private_vfree(void *p);
extern int isp_printf(int level, const char *fmt, ...);

static inline u32 t33_binapp_le16(const u8 *p)
{
	return p[0] | p[1] << 8;
}

static inline void t33_binapp_put32(u8 *p, u32 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
	p[2] = (u8)(v >> 16);
	p[3] = (u8)(v >> 24);
}

/* grow the LUT store of channel c to need bytes, keeping the used part */
static inline void t33_binapp_grow(u32 c, u32 need)
{
	u8 *info = (u8 *)(unsigned long)stLutInfo[c];
	u8 *old, *newp;
	u32 used;

	lutMaxSize[c * 7 + 1] = need;
	newp = (u8 *)private_vmalloc(need);
	used = t33_binapp_le16(info + 2);
	old = (u8 *)(unsigned long)t33_le32(info + 20);
	memcpy(newp, old, used);
	if (old) {
		private_vfree(old);
		t33_binapp_put32(info + 20, 0);
	}
	t33_binapp_put32(info + 20, (u32)(unsigned long)newp);
}

/* tisp_bin_append_data_parse: returns 0, or -1 on a bad argument */
static inline int t33_bin_append_data_parse(u32 ch, const u8 *bin, u32 len)
{
	u32 c = ch & 0xff, left = T33_BINAPP_MAX_SECTIONS, next, mod, size, cnt, b, need, k, idx;
	const u8 *sec;

	if (len < 8 || !bin) {
		isp_printf(2, (const char *)0, (const char *)0, 913);
		return -1;
	}
	sec = bin + 8;
	next = t33_le32(bin + 4);

	for (;;) {
		if (!next)
			return 0;
		mod = (u32)tisp_get_module_enum(sec);
		if (mod == T33_BINAPP_MOD_LDC) {
			tisp_ldc_get_extra_par(c, sec);
		} else if (mod == T33_BINAPP_MOD_LUT) {
			if (t33_le32(sec + 16) == 1) {
				size = t33_le32(sec + 24);
				if (!size) {
					isp_printf(1, (const char *)0, (const char *)0, 946);
				} else {
					u8 *tp = tparamsP[c];
					const u8 *src = sec + 28;

					cnt = (size / T33_BINAPP_LUT_DIV) & 0xffff;
					b = tp[17636];
					b = (b + cnt) & 0xff;
					tp[17636] = (u8)b;
					need = b * T33_BINAPP_LUT_STRIDE;
					if (lutMaxSize[c * 7 + 1] < need)
						t33_binapp_grow(c, need);
					for (k = 0; k < cnt; k++) {
						u8 *info = (u8 *)(unsigned long)stLutInfo[c];
						u32 base = t33_le32(info + 20);
						u32 half = t33_binapp_le16(info + 2) >> 1;
						u32 o2;

						memcpy((void *)(unsigned long)(((k * T33_BINAPP_LUT_HALF + half) << 1) + base),
						       src, T33_BINAPP_LUT_STRIDE);
						idx = (u32)tp[17636] - cnt + k;
						tp[idx + 17579] = src[13828];
						o2 = (idx + 8760) << 1;
						tp[o2 + 8] = src[13826];
						tp[o2 + 9] = src[13827];
						src += T33_BINAPP_LUT_DIV;
					}
				}
			}
		} else {
			isp_printf(1, (const char *)0, (const char *)0, 982);
		}

		next = t33_le32(sec + 20);
		if (next >= len)
			return 0;
		sec = bin + next;
		left = (left - 1) & 0xff;
		if (!left)
			return 0;
	}
}

#endif /* TX_ISP_T33_BINAPP_H */
