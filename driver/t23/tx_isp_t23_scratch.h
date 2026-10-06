/* SPDX-License-Identifier: GPL-2.0 */
#ifndef TX_ISP_T23_SCRATCH_H
#define TX_ISP_T23_SCRATCH_H

/*
 * MSCA scratch target for a stopped frame channel whose output stays
 * enabled while the input runs (msca_keep_enabled=2, chan_stop_keep_input=1).
 * See driver/t23/docs/MSCA_SCRATCH.md.
 *
 * With an empty address FIFO the MSCA writes every further frame to the
 * last address it popped, a buffer of the stopped stream that user space
 * may already have freed.  STREAMOFF therefore queues one scratch address
 * behind the stream's buffers: after them the MSCA writes into the scratch
 * area and stays there until the next STREAMON queues real buffers again.
 *
 * The scratch area sits at the end of the ISP buffer libimp allocates in
 * rmem (GET_BUF asks for it, SET_BUF hands it over).  The kernel heap on a
 * 64 MiB T23 has no free block of the size (Jooan A6M: largest free order
 * 6 = 256 KiB, a 720p frame needs order 8), and the ISP buffer lives from
 * AddSensor to DelSensor, i.e. longer than any running input.  One area
 * serves all channels: Y and UV point at the same base, so it holds one
 * Y plane of the sensor size (the largest a channel can output).
 *
 * Pure helpers, shared with the host test tests/tx_isp_t23_scratch_test.c.
 */

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
typedef uint32_t u32;
typedef uint64_t u64;
#endif

#define T23_SCRATCH_PAGE 4096U

/* Y plane of the frame layout the MSCA writes (height aligned to 16). */
static inline u64 t23_scratch_plane_bytes(u32 width, u32 height)
{
	return (u64)width * (((u64)height + 15U) & ~(u64)15U);
}

/* Bytes GET_BUF adds for the scratch area; 0 when there is no frame size. */
static inline u32 t23_scratch_bytes(u32 sensor_width, u32 sensor_height)
{
	u64 n = t23_scratch_plane_bytes(sensor_width, sensor_height);

	if (!n || n > 0x10000000ULL)
		return 0;
	return (u32)((n + T23_SCRATCH_PAGE - 1U) & ~(u64)(T23_SCRATCH_PAGE - 1U));
}

/*
 * Scratch address inside the SET_BUF buffer [paddr, paddr + size): the last
 * scratch_bytes, behind the MDNS area (mdns_used) and the crumb page
 * (reserved).  0 when it does not fit or is not 8-byte aligned.
 */
static inline u32 t23_scratch_place(u32 paddr, u32 size, u32 mdns_used,
				    u32 reserved, u32 scratch_bytes)
{
	u64 need = (u64)mdns_used + reserved + scratch_bytes;
	u32 phys;

	if (!paddr || !scratch_bytes || need > size ||
	    (u64)paddr + size > 0x100000000ULL)
		return 0;
	phys = paddr + size - scratch_bytes;
	if (phys & 7U)
		return 0;
	return phys;
}

/* A channel frame fits the scratch area (Y and UV share the base). */
static inline int t23_scratch_fits(u32 width, u32 height, u32 scratch_bytes)
{
	u64 n = t23_scratch_plane_bytes(width, height);

	return n && n <= scratch_bytes;
}

/*
 * STREAMOFF parks the output on the scratch area when the output stays
 * enabled (keep_enabled 2) and frames keep coming (the input does not stop
 * with this STREAMOFF and runs, the output bit is in the channel mask).
 */
static inline int t23_scratch_park_wanted(int keep_enabled, int input_stops,
					  int input_running, int output_on)
{
	return keep_enabled >= 2 && !input_stops && input_running && output_on;
}

/* A completion address that is the scratch area (low 3 bits ignored). */
static inline int t23_scratch_is(u32 scratch_phys, u32 y_phys)
{
	return scratch_phys && (y_phys & ~7U) == (scratch_phys & ~7U);
}

#endif
