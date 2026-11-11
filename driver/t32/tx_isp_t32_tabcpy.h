/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 table lookup and block copy helpers of the vendor tisp core:
 * table_intp (piecewise-linear lookup in a { value, key } table through
 * fix_point_intp) and memcpy_simd (16-byte block copy, then the rest).
 *
 * fix_point_intp itself belongs to another block (it is handed in as a
 * function, the vendor's o32 arguments: a0 passed through, x0, x1, y0, y1,
 * x).  memcpy_simd's 16-byte MXU moves are plain byte copies here.
 *
 * Vendor behaviour kept (not fixed, a change needs the user's OK):
 * table_intp's index is a byte (tables of more than 255 entries wrap) and
 * a key above the last entry gives the last value without interpolation
 * (more than 255 entries with every key below the target never end).
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15)
 * by tests/t32_oracle (t32_tabcpy_oracle).
 */
#ifndef TX_ISP_T32_TABCPY_H
#define TX_ISP_T32_TABCPY_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/string.h>
#else
#include <stdint.h>
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
#ifndef T32_HOST_U64
#define T32_HOST_U64
typedef uint64_t u64;
#endif
#ifndef T32_HOST_S64
#define T32_HOST_S64
typedef int64_t s64;
typedef int32_t s32;
#endif
#endif

typedef u64 (*t32_fix_point_intp_fn)(u32 a0, u32 x0, u32 x1, u32 y0, u32 y1, u32 x);

struct t32_tab_entry {
	u32 value, key;
};

/* table_intp: n entries sorted by key */
static inline u64 t32_table_intp(t32_fix_point_intp_fn intp, u32 a0, const struct t32_tab_entry *t, u32 n, u32 key)
{
	u32 i;

	if (!(t[0].key < key))
		return t[0].value;
	for (i = 1; i < n; i = (i + 1) & 0xff)
		if (!(t[i].key < key))
			return intp(a0, t[i - 1].key, t[i].key, t[i - 1].value, t[i].value, key);
	return t[n - 1].value;
}

/* memcpy_simd */
static inline int t32_memcpy_simd(int (*printk)(const char *fmt, ...), u8 *dst, const u8 *src, s32 n)
{
	s32 k = 0;

	if (!src || !dst || n < 0) {
		printk("BAD_PARAM: src %p, dst %p, size %d\n", src, dst, n);
		return -1;
	}
	for (; k <= n - 16; k += 16)
		memcpy(dst + k, src + k, 16);
	if (n - k > 0)
		memcpy(dst + k, src + k, (u32)(n - k));
	return 0;
}

#endif /* TX_ISP_T32_TABCPY_H */
