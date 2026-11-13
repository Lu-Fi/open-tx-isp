/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 AE sensor block: the 124 B block the vendor ispcore_sync_sensor_attr
 * (0xb6ac4) builds from the sensor's struct tx_isp_video_in (104 B, vin)
 * and its attribute block (si = vin word +52) and hands to
 * lib_tisp_sync_sensor_attr, which copies its run-time fields into the
 * channel's dd2d4 entry (t32_lib_sync_sensor_attr, tx_isp_t32_libwrap.h).
 * The same mapping as t32_ispcore_sync_sensor_attr (tx_isp_t32_ispsync.h),
 * on plain pointers for the driver's AE context (tx_isp_t32_tisp.c).
 * The bytes the vendor does not set are uninitialised stack there, 0 here.
 *
 * Checked against Matt Davis' recovered C (PR #15 @ 2e2e06bc) by
 * tests/t32_oracle (t32_aesns_oracle: the block and the dd2d4 entry after
 * the vendor ispcore_sync_sensor_attr -> lib_tisp_sync_sensor_attr).
 */
#ifndef TX_ISP_T32_AESNS_H
#define TX_ISP_T32_AESNS_H

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

#define T32_AESNS_BLK		124U

static inline void t32_ae_sensor_blk(u8 *blk, const u8 *vin, const u8 *si)
{
	memset(blk, 0, T32_AESNS_BLK);
	memcpy(blk + 0, si + 276, 2);	/* total width */
	memcpy(blk + 2, si + 278, 2);	/* total height */
	memcpy(blk + 36, si + 160, 4);	/* max again (log2) */
	memcpy(blk + 40, si + 192, 4);	/* max dgain (log2) */
	memcpy(blk + 56, vin + 68, 12);	/* video_in words +68 .. +76 (fps / ratio words) */
	memcpy(blk + 68, si + 220, 4);	/* integration time min */
	memcpy(blk + 72, si + 224, 4);	/* integration time max */
	memcpy(blk + 80, si + 224, 4);
	memcpy(blk + 84, si + 280, 4);
	memcpy(blk + 88, si + 236, 4);
	memcpy(blk + 92, si + 240, 4);
	memcpy(blk + 96, si + 176, 4);
	memcpy(blk + 108, si + 152, 4);
	memcpy(blk + 112, si + 216, 4);
	blk[116] = si[228];		/* low byte of the u16 */
	blk[117] = si[164];
	blk[123] = si[196] ? si[196] : 1;
}

#endif /* TX_ISP_T32_AESNS_H */
