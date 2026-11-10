/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 parameter getters of the vendor tisp core: the 516-byte version block
 * (tisp_version_param_array_get), the 336-byte common parameter block of a
 * channel (tisp_comn_param_array_get) and the LUT info copy of a mode
 * (tisp_lut_params_copy).
 *
 * Vendor objects (32-bit kernel layout, offsets in bytes):
 *   channel state (dc284[ch], 904 B): +0/+4/+8 words, +20 16-byte name,
 *     +36 WDR byte (bit 0), +37 byte;
 *   common block (da850[ch], 336 B): +128 name copy, +144 project string,
 *     +225/+226/+228/+229 bytes, +256..+264 words, +292 AE word;
 *   AE state (dd2d4[ch], 232 B): +56 word;
 *   LUT info (stLutInfo, dc274[ch] and the mode table dc214[ch * 6 + mode]):
 *     +0..+12 seven u16 sizes, +16..+40 seven 32-bit table pointers.
 *
 * Vendor behaviour (named): the version block's bytes 108..131 and 388..515
 * are never written by the vendor (it copies uninitialised stack); ours
 * leaves them 0.  tisp_lut_params_copy takes each table's length from the
 * destination after the size copy (so from the source), in the vendor's
 * order of sizes 0, 1, 3, 2, 4, 5, 6.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15)
 * by tests/t32_oracle (t32_param_oracle).
 */
#ifndef TX_ISP_T32_PARAM_H
#define TX_ISP_T32_PARAM_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#else
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifndef T32_HOST_U32
#define T32_HOST_U32
typedef uint32_t u32;
#endif
#ifndef T32_HOST_U8
#define T32_HOST_U8
typedef uint8_t u8;
typedef uint16_t u16;
#endif
#endif

#define T32_TISP_VERSION_ID	0x0134da11U
#define T32_VERSION_BLOCK_BYTES	516
#define T32_COMN_BLOCK_BYTES	336
#define T32_ISP_VERSION_STR	"isp_20260618a"
#define T32_HW_VERSION_STR	"H20260618a"
#define T32_PROJECT_STR		"PRJ007"

/* tisp_version_param_array_get: 27 block ids, two version strings */
static inline int t32_version_param_array_get(u8 *out, u32 *len)
{
	static const int delta[27] = {
		0, 0, 0, 0, -2, 0, 188, 0, 0, 0, 0, 0, 188, 9600, -2, 188, -94, 0, 0, 209, 0, 0, 0, 0, 0, 0, 0
	};
	u8 blk[T32_VERSION_BLOCK_BYTES];
	int i;

	memset(blk, 0, sizeof(blk));
	snprintf((char *)blk + 132, 128, T32_ISP_VERSION_STR);
	snprintf((char *)blk + 260, 128, T32_HW_VERSION_STR);
	for (i = 0; i < 27; i++)
		*(u32 *)(blk + 4 * i) = T32_TISP_VERSION_ID + (u32)delta[i];
	memcpy(out, blk, T32_VERSION_BLOCK_BYTES);
	*len = T32_VERSION_BLOCK_BYTES;
	return T32_VERSION_BLOCK_BYTES;
}

/* tisp_comn_param_array_get (ch: low byte; blk = da850[ch], chn = dc284[ch], ae = dd2d4[ch]) */
static inline int t32_comn_param_array_get(u8 *blk, const u8 *chn, const u8 *ae, u8 cfg_mode, u32 sensor_on,
					   u8 *out, u32 *len)
{
	memcpy(blk + 128, chn + 20, 16);
	snprintf((char *)blk + 144, 16, T32_PROJECT_STR);
	memcpy(blk + 256, chn, 12);
	blk[225] = chn[36] & 1;
	blk[226] = chn[37];
	memcpy(blk + 292, ae + 56, 4);
	blk[228] = cfg_mode;
	blk[229] = sensor_on != 0;
	memcpy(out, blk, T32_COMN_BLOCK_BYTES);
	*len = T32_COMN_BLOCK_BYTES;
	return 0;
}

/* stLutInfo: seven sizes and seven 32-bit table pointers */
struct t32_lut_info {
	u16 size[7];
	u16 rsvd;
	u32 tab[7];
};

static inline void *t32_lut_ptr(u32 p)
{
	return (void *)(uintptr_t)p;
}

/* tisp_lut_params_copy: dst = dc274[ch], src = dc214[ch * 6 + mode] */
static inline void t32_lut_params_copy(struct t32_lut_info *dst, const struct t32_lut_info *src)
{
	/* { size index, table index } in the vendor's order */
	static const u8 order[7][2] = { { 0, 0 }, { 1, 1 }, { 3, 2 }, { 2, 4 }, { 4, 3 }, { 5, 5 }, { 6, 6 } };
	static const u8 size_order[7] = { 0, 1, 3, 2, 4, 5, 6 };
	int i;

	for (i = 0; i < 7; i++)
		dst->size[size_order[i]] = src->size[size_order[i]];
	for (i = 0; i < 7; i++)
		memcpy(t32_lut_ptr(dst->tab[order[i][1]]), t32_lut_ptr(src->tab[order[i][1]]), dst->size[order[i][0]]);
}

#endif /* TX_ISP_T32_PARAM_H */
