/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) IQ bin parse (vendor tisp_bin_data_parse), binary oracle block "i9I".
 *
 * bin: +0 magic (8 bytes), +8 a 152 byte header (+0 CRC length, +8 CRC, +12 the 112 byte stParSel entry of
 * the channel, +144 version string), +160 the sections in a fixed order.  stParSel[ch] holds per section a u16
 * flag (1: present) and a u16 size; each present section is copied into tparamsP[ch] at its offset.  Five
 * sections (the LUT ones) are followed by table data kept in stLutInfo[ch] (u16 sizes at +2..+12, pointers at
 * +20..+40), whose buffers grow with lutMaxSize.  What follows is handed to tisp_bin_append_data_parse.
 */
#ifndef TX_ISP_T33_I9I_H
#define TX_ISP_T33_I9I_H

#include "tx_isp_t33_blk.h"
#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

extern u32 lutMaxSize[28];
extern u32 stLutInfo[4];
extern u8 *tparamsP[4];
extern u8 stParSel[448];
extern u8 st_tisp_com_par[1344];

extern int isp_printf(int level, const char *fmt, ...);
extern void *private_kmalloc(u32 size, u32 flags);
extern void private_kfree(void *p);
extern void *private_vmalloc(u32 n);
extern void private_vfree(void *p);
/* the kernel's snprintf under our own name (no compiler folding into a string copy) */
extern int t33_bdp_snprintf(char *buf, u32 n, const char *fmt, ...) __asm__("snprintf");
extern void tisp_ldc_reset_par_state(u32 ch);
extern int tisp_bin_append_data_parse(u32 ch, const u8 *bin, u32 size);

/* vendor format strings $LC43..$LC45 are not in the disassembly: placeholders */
#define T33_BDP_MAGIC	"T33"
#define T33_BDP_VERSION	"T33"

struct t33_bdp_sec {
	u8 sel;		/* byte offset of the flag / size pair in the stParSel entry */
	u8 kind;	/* 0 plain, 1..4 one table store (t33_bdp_luts[kind - 1]), 5 two stores (luts 4, 5) */
	u16 dst;	/* byte offset in tparamsP[ch] */
};

static const struct t33_bdp_sec t33_bdp_secs[26] = {
	{ 0, 0, 0 }, { 4, 0, 112 }, { 8, 0, 3400 }, { 16, 0, 10140 }, { 20, 0, 15916 }, { 24, 0, 16520 },
	{ 28, 1, 17488 }, { 32, 0, 17640 }, { 36, 0, 17776 }, { 40, 0, 17880 }, { 44, 0, 17900 },
	{ 48, 2, 21536 }, { 52, 3, 23424 }, { 56, 0, 23496 }, { 60, 0, 24156 }, { 64, 0, 25144 },
	{ 68, 0, 0x88ac }, { 72, 0, 0x892c }, { 76, 4, 0x8b78 }, { 80, 5, 0x903c }, { 84, 0, 0x907c },
	{ 88, 0, 0x9b20 }, { 92, 0, 0x9bb8 }, { 96, 0, 0x9fec }, { 100, 0, 0xa30c }, { 108, 0, 0xa3e8 },
};

/* table stores: count byte in tparamsP, bytes per entry, u16 size / pointer offset in stLutInfo, lutMaxSize word */
struct t33_bdp_lut {
	u16 boff;
	u16 mul;
	u8 sz;
	u8 ptr;
	u8 mw;
	u8 check;	/* LSC: stop when no buffer */
};

static const struct t33_bdp_lut t33_bdp_luts[6] = {
	{ 17636, 13826, 2, 20, 1, 1 },
	{ 23422, 40, 6, 24, 2, 0 },
	{ 23494, 266, 4, 32, 3, 0 },
	{ 36919, 40, 8, 28, 4, 0 },
	{ 36983, 2102, 10, 36, 5, 0 },
	{ 36983, 1050, 12, 40, 6, 0 },
};

/* the vendor's crc_table (.rodata, 8 words used): assumed CRC-32 table head, not verified by the oracle */
static const u32 t33_bdp_crc_tab[8] = {
	0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3,
};

static inline void t33_bdp_put(u8 *p, u32 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
	p[2] = (u8)(v >> 16);
	p[3] = (u8)(v >> 24);
}

static inline void t33_bdp_log(int level, int line)
{
	isp_printf(level, "[ISP][tisp_bin_data_parse, %d]: error\n", line);
}

/* strncmp(a, b, 8) != 0 */
static inline int t33_bdp_cmp8(const u8 *a, const u8 *b)
{
	int i;

	for (i = 0; i < 8; i++) {
		if (a[i] != b[i])
			return (int)a[i] - (int)b[i];
		if (!a[i])
			return 0;
	}
	return 0;
}

static inline u8 *t33_bdp_info(u32 ch)
{
	return (u8 *)(unsigned long)stLutInfo[ch];
}

static inline void t33_bdp_lut_size(u32 ch, const struct t33_bdp_lut *l)
{
	u8 *info = t33_bdp_info(ch);
	u32 v = tparamsP[ch][l->boff] * l->mul;

	info[l->sz] = (u8)v;
	info[l->sz + 1] = (u8)(v >> 8);
}

static inline void t33_bdp_lut_alloc(u32 ch, const struct t33_bdp_lut *l)
{
	u8 *info = t33_bdp_info(ch);

	if (!t33_le32(info + l->ptr))
		t33_bdp_put(info + l->ptr, (u32)(unsigned long)private_vmalloc(t32_tp_le16(info + l->sz)));
}

static inline void t33_bdp_lut_grow(u32 ch, const struct t33_bdp_lut *l)
{
	u8 *info = t33_bdp_info(ch);
	u32 need = t32_tp_le16(info + l->sz), p;

	if (lutMaxSize[ch * 7 + l->mw] >= need)
		return;
	lutMaxSize[ch * 7 + l->mw] = need;
	p = t33_le32(info + l->ptr);
	if (p) {
		private_vfree((void *)(unsigned long)p);
		t33_bdp_put(t33_bdp_info(ch) + l->ptr, 0);
	}
	info = t33_bdp_info(ch);
	t33_bdp_put(info + l->ptr, (u32)(unsigned long)private_vmalloc(t32_tp_le16(info + l->sz)));
}

/* copy the table data; the size is read again after the copy (as the vendor) */
static inline void t33_bdp_lut_copy(u32 ch, const struct t33_bdp_lut *l, const u8 **s0)
{
	u8 *info = t33_bdp_info(ch);

	memcpy((void *)(unsigned long)t33_le32(info + l->ptr), *s0, t32_tp_le16(info + l->sz));
	*s0 += t32_tp_le16(t33_bdp_info(ch) + l->sz);
}

static inline int t33_bdp_lut_one(u32 ch, const struct t33_bdp_lut *l, const u8 **s0)
{
	t33_bdp_lut_size(ch, l);
	t33_bdp_lut_alloc(ch, l);
	t33_bdp_lut_grow(ch, l);
	if (l->check && !t33_le32(t33_bdp_info(ch) + l->ptr)) {
		t33_bdp_log(2, 1091);
		return -1;
	}
	t33_bdp_lut_copy(ch, l, s0);
	return 0;
}

/* tisp_bin_data_parse(ch, size, bin) */
static inline int t33_bin_data_parse(u32 ch, u32 size, const u8 *bin)
{
	u8 *hdr, *tmp;
	const u8 *s0;
	u32 len, crc, i, used;

	ch &= 0xff;
	hdr = (u8 *)private_kmalloc(172, 208);
	if (!hdr) {
		t33_bdp_log(2, 1006);
		return -1;
	}
	memset(hdr, 0, 172);
	tmp = (u8 *)private_vmalloc(152);
	if (!tmp) {
		t33_bdp_log(2, 1013);
		return -1;
	}
	memset(tmp, 0, 152);
	t33_bdp_put(st_tisp_com_par + ch * 336 + 268, size);
	t33_bdp_snprintf((char *)hdr, 8, "%s", T33_BDP_MAGIC);
	t33_bdp_snprintf((char *)hdr + 152, 8, T33_BDP_VERSION);
	t33_bdp_put(hdr + 160, (u32)(unsigned long)bin);
	if (t33_bdp_cmp8(hdr, bin)) {
		t33_bdp_log(1, 1028);
		t33_bdp_log(2, 1029);
		private_kfree(hdr);
		return -1;
	}
	memcpy(tmp, bin + 8, 152);
	if (t33_bdp_cmp8(hdr + 152, tmp + 144)) {
		t33_bdp_log(1, 1038);
		t33_bdp_log(2, 1039);
		private_kfree(hdr);
		return -1;
	}
	s0 = bin + 160;
	len = t33_le32(tmp) >> 2;
	crc = 0;
	for (i = 0; i < len; i++) {
		crc ^= t33_le32(s0 + 4 * i);
		crc ^= t33_bdp_crc_tab[crc & 7];
	}
	if (crc != t33_le32(tmp + 8)) {
		t33_bdp_log(2, 1046);
		private_kfree(hdr);
		return -1;
	}
	memcpy(stParSel + ch * 112, tmp + 12, 112);
	if (!stLutInfo[ch]) {
		stLutInfo[ch] = (u32)(unsigned long)private_vmalloc(44);
		memset(t33_bdp_info(ch), 0, 44);
	}
	if (!tparamsP[ch]) {
		tparamsP[ch] = (u8 *)private_vmalloc(size);
		if (!tparamsP[ch])
			t33_bdp_log(2, 1062);
		memset(tparamsP[ch], 0, size);
	}
	for (i = 0; i < ARRAY_SIZE(t33_bdp_secs); i++) {
		const struct t33_bdp_sec *s = &t33_bdp_secs[i];
		const u8 *e = stParSel + ch * 112 + s->sel;

		if (t32_tp_le16(e) != 1)
			continue;
		memcpy(tparamsP[ch] + s->dst, s0, t32_tp_le16(e + 2));
		s0 += t32_tp_le16(e + 2);
		if (s->kind == 5) {
			t33_bdp_lut_size(ch, &t33_bdp_luts[4]);
			t33_bdp_lut_size(ch, &t33_bdp_luts[5]);
			t33_bdp_lut_alloc(ch, &t33_bdp_luts[4]);
			t33_bdp_lut_grow(ch, &t33_bdp_luts[4]);
			t33_bdp_lut_alloc(ch, &t33_bdp_luts[5]);
			t33_bdp_lut_grow(ch, &t33_bdp_luts[5]);
			t33_bdp_lut_copy(ch, &t33_bdp_luts[4], &s0);
			t33_bdp_lut_copy(ch, &t33_bdp_luts[5], &s0);
		} else if (s->kind && t33_bdp_lut_one(ch, &t33_bdp_luts[s->kind - 1], &s0)) {
			return -1;
		}
	}
	tisp_ldc_reset_par_state(ch);
	used = (u32)(s0 - (const u8 *)(unsigned long)t33_le32(hdr + 160));
	if (used < size)
		tisp_bin_append_data_parse(ch, s0, size - used);
	else if (size < used)
		t33_bdp_log(1, 1204);
	else
		t33_bdp_log(0, 1206);
	private_kfree(hdr);
	private_vfree(tmp);
	memcpy(st_tisp_com_par + ch * 336 + 232, t33_bdp_info(ch), 14);
	return 0;
}

#endif /* TX_ISP_T33_I9I_H */
