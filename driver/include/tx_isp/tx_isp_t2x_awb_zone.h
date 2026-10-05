/* SPDX-License-Identifier: MIT */
/*
 * T10/T20 (Apical ISP) per-zone AWB statistics for the T20 3.12.0
 * IMP_ISP_Tuning_GetAwbZone(IMPISPAWBZone *) ABI: 15x15 zones of
 * {u16 red_green, u16 blue_green, u32 sum} (struct isp_core_wb_zone_info).
 *
 * The hardware keeps them in the metering memory (0x8000 + 4 * word) from
 * word ISP_METERING_OFFSET_AWB (464) on, two words per zone: the R/G ratio
 * in bits 0..11 and the B/G ratio in bits 16..27 of the first word (the
 * ratio pair the AWB statistics mode selects, Q8), the population (pixels
 * counted) in the second.  This is the bank the T20 firmware
 * awb_read_statistics() and the compact AWB read.
 */
#ifndef TX_ISP_T2X_AWB_ZONE_H
#define TX_ISP_T2X_AWB_ZONE_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
typedef uint16_t u16;
typedef uint32_t u32;
#endif

#define TX_ISP_T2X_AWB_ZONE_GRID	15U
#define TX_ISP_T2X_AWB_ZONES		(TX_ISP_T2X_AWB_ZONE_GRID * \
					 TX_ISP_T2X_AWB_ZONE_GRID)
#define TX_ISP_T2X_METERING_MEM		0x8000U
#define TX_ISP_T2X_METERING_AWB_WORD	464U

struct tx_isp_t2x_awb_zone {
	u16 red_green;
	u16 blue_green;
	u32 sum;
};

/* register offset (from the ISP base) of zone @zone's word @word (0, 1) */
static inline u32 tx_isp_t2x_awb_zone_reg(u32 zone, u32 word)
{
	return TX_ISP_T2X_METERING_MEM +
	       ((TX_ISP_T2X_METERING_AWB_WORD + zone * 2U + word) << 2);
}

static inline void tx_isp_t2x_awb_zone_unpack(u32 ratios, u32 population,
					      struct tx_isp_t2x_awb_zone *z)
{
	z->red_green = (u16)(ratios & 0xfffU);
	z->blue_green = (u16)((ratios >> 16) & 0xfffU);
	z->sum = population;
}

#endif
