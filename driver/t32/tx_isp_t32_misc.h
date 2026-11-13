/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 small helpers of the vendor tisp core: 64-bit min / max, the 11 point
 * table interpolation for s32 / u32 tables, byte array compare, string test,
 * LSC table row / entry swap, the 12-byte -> 4 x 24-bit unpack (u3bw), and
 * the empty vendor hooks that only return a constant.
 *
 * Flow of the vendor functions tisp_max, tisp_min, tisp_simple_intp,
 * tisp_simple_intp_uint32, dfg_array_cmp, isStringAssigned,
 * lsc_exchange_data, lsc_exchange_line, u3bw, tisp_bin_data_process,
 * tisp_channel_main_stop, tisp_channel_main_fifo_clear,
 * tisp_rdma_fra_down_interrupt, tisp_wdma_fra_down_interrupt,
 * add_sensor_chose, tisp_process_init, release_sensor_setting.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15)
 * by tests/t32_oracle (t32_misc_oracle).
 */
#ifndef TX_ISP_T32_MISC_H
#define TX_ISP_T32_MISC_H

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
#ifndef T32_HOST_S64
#define T32_HOST_S64
typedef int64_t s64;
typedef int32_t s32;
#endif
#endif

/* tisp_max / tisp_min: signed 64-bit; on a tie the second argument */
static inline s64 t32_tisp_max(s64 a, s64 b)
{
	return a > b ? a : b;
}

static inline s64 t32_tisp_min(s64 a, s64 b)
{
	return a < b ? a : b;
}

/*
 * tisp_simple_intp: t[idx] towards t[idx + 1] by frac / 65536, the step
 * |t1 - t0| * frac rounded at bit 15 (32-bit product, arithmetic shift);
 * idx >= 10 gives t[10].
 */
static inline s32 t32_simple_intp(u32 idx, u32 frac, const s32 *t)
{
	s32 a, b;
	u32 m;
	s32 step;

	if (idx >= 10)
		return t[10];
	a = t[idx];
	b = t[idx + 1];
	if (a == b)
		return a;
	m = (a < b ? (u32)b - (u32)a : (u32)a - (u32)b) * frac;
	step = (s32)(((s32)m >> 16) + ((m >> 15) & 1));
	return a < b ? (s32)((u32)a + (u32)step) : (s32)((u32)a - (u32)step);
}

/* tisp_simple_intp_uint32: the same with an unsigned compare and shift */
static inline u32 t32_simple_intp_u32(u32 idx, u32 frac, const u32 *t)
{
	u32 a, b, m, step;

	if (idx >= 10)
		return t[10];
	a = t[idx];
	b = t[idx + 1];
	if (a == b)
		return a;
	m = (a < b ? b - a : a - b) * frac;
	step = (m >> 16) + ((m >> 15) & 1);
	return a < b ? a + step : a - step;
}

/* dfg_array_cmp: 1 when the first n bytes are equal (n <= 0: 1), else 0 */
static inline int t32_dfg_array_cmp(const u8 *a, const u8 *b, s32 n)
{
	s32 i;

	for (i = 0; i < n; i++)
		if (a[i] != b[i])
			return 0;
	return 1;
}

/* isStringAssigned: a non-NULL string with a first character */
static inline int t32_is_string_assigned(const char *s)
{
	return s && s[0] != 0;
}

/* lsc_exchange_data: swap entries i and j of a u16 table */
static inline void t32_lsc_exchange_data(u16 *t, u32 i, u32 j)
{
	u16 v = t[i];

	t[i] = t[j];
	t[j] = v;
}

/*
 * lsc_exchange_line: swap the bytes rows of a u16 table that start at entry
 * i and j through the scratch buffer tmp (memcpy, memmove, memcpy).
 */
static inline void t32_lsc_exchange_line(u16 *t, void *tmp, u32 i, u32 j, u32 bytes)
{
	memcpy(tmp, t + i, bytes);
	memmove(t + i, t + j, bytes);
	memcpy(t + j, tmp, bytes);
}

/*
 * u3bw: unpack groups of 12 bytes into 4 words of 24 bits, `bytes` input
 * bytes (12 per group, the last group may run over).  Vendor quirk kept:
 * the fourth word is read with a single lwr at +9, so its top byte is the
 * top byte of the group's address (the register held the pointer before).
 */
static inline void t32_u3bw(u32 *out, const u8 *in, u32 bytes)
{
	u32 k;

	for (k = 0; k < bytes; k += 12, out += 4) {
		const u8 *p = in + k;

		out[0] = p[0] | (u32)p[1] << 8 | (u32)p[2] << 16;
		out[1] = p[3] | (u32)p[4] << 8 | (u32)p[5] << 16;
		out[2] = p[6] | (u32)p[7] << 8 | (u32)p[8] << 16;
		out[3] = p[9] | (u32)p[10] << 8 | (u32)p[11] << 16 |
			 ((u32)(uintptr_t)p & 0xff000000U);
	}
}

/* vendor hooks without a body: only the return value */
static inline int t32_tisp_bin_data_process(void) { return -1; }
static inline int t32_tisp_channel_main_stop(void) { return 0; }
static inline int t32_tisp_channel_main_fifo_clear(void) { return 0; }
static inline int t32_tisp_rdma_fra_down_interrupt(void) { return 1; }
static inline int t32_tisp_wdma_fra_down_interrupt(void) { return 1; }
static inline int t32_add_sensor_chose(void) { return 0; }
static inline int t32_tisp_process_init(void) { return 0; }
static inline int t32_release_sensor_setting(void) { return 0; }

#endif /* TX_ISP_T32_MISC_H */
