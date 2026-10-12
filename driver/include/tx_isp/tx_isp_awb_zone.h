/* SPDX-License-Identifier: MIT */
/*
 * Tiziano ISP (T23, T31) per-zone AWB statistics for the 1.1.x
 * IMP_ISP_Tuning_GetAwbZone(IMPISPAWBZone *) ABI:
 *
 *	typedef struct {
 *		unsigned char zone_r[225];  15x15 zone means, R channel
 *		unsigned char zone_g[225];  G
 *		unsigned char zone_b[225];  B
 *	} IMPISPAWBZone;                675 bytes
 *
 * The AWB statistics DMA bank holds, per zone, the channel sums (R 21
 * bits, G 21 bits, B 21 bits, IR 21 bits) and the number of pixels counted
 * (13 bits).  The stock Tiziano_awb_fpga (tx-isp-t31.ko 1.1.6, HLIL
 * 0x18218) fills the zone block as
 *
 *	zone_x[i] = pix[i] ? (unsigned char)(sum_x[i] / pix[i]) : 0
 *
 * i.e. the mean is truncated to a byte, not rounded, not saturated; the
 * stock tisp_g_wb_zone then copies the 675 bytes to the caller.  The sums
 * are of 8-bit samples, so a mean beyond 255 does not occur in practice
 * (0x1fffff / 0x1fff = 256 only with a saturated zone of one pixel).
 *
 * The open drivers add a freshness contract on top of the stock copy: the
 * block is handed out only when it was computed from a statistics frame
 * of the current stream less than TX_ISP_AWB_ZONE_MAX_AGE ago, else the
 * ioctl fails with -EAGAIN and libimp returns -1 to the caller.  A
 * consumer therefore never sees zero-filled or stale zone data as a
 * success.
 */
#ifndef TX_ISP_AWB_ZONE_H
#define TX_ISP_AWB_ZONE_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/jiffies.h>
#else
#include <stdint.h>
#endif

#define TX_ISP_AWB_ZONE_GRID	15U
#define TX_ISP_AWB_ZONES	(TX_ISP_AWB_ZONE_GRID * TX_ISP_AWB_ZONE_GRID)
#define TX_ISP_AWB_ZONE_BYTES	(3U * TX_ISP_AWB_ZONES)	/* 675 */

/* oldest statistics frame GetAwbZone still reports as current */
#define TX_ISP_AWB_ZONE_MAX_AGE_MS	1000U

/* one zone mean the stock way: truncated byte of sum / pixels, 0 if empty */
static inline uint8_t tx_isp_awb_zone_mean(uint32_t sum, uint32_t pixels)
{
	return pixels ? (uint8_t)(sum / pixels) : (uint8_t)0;
}

/*
 * The whole IMPISPAWBZone block from the four 225-word statistics arrays:
 * r at out[0..224], g at out[225..449], b at out[450..674].
 */
static inline void tx_isp_awb_zone_pack(const uint32_t *r, const uint32_t *g,
					const uint32_t *b, const uint32_t *pix,
					uint8_t *out)
{
	unsigned int i;

	for (i = 0; i < TX_ISP_AWB_ZONES; i++) {
		out[i] = tx_isp_awb_zone_mean(r[i], pix[i]);
		out[TX_ISP_AWB_ZONES + i] = tx_isp_awb_zone_mean(g[i], pix[i]);
		out[2U * TX_ISP_AWB_ZONES + i] = tx_isp_awb_zone_mean(b[i], pix[i]);
	}
}

/*
 * Freshness: @stamp is the jiffies value taken when the block was computed
 * (0 = never since the stream started).  Returns 1 when the block may be
 * handed out, 0 when it is missing or older than max_age_ms.
 */
#ifdef __KERNEL__
static inline int tx_isp_awb_zone_fresh(unsigned long stamp, unsigned long now,
					unsigned int max_age_ms)
{
	if (!stamp)
		return 0;
	return time_before_eq(now, stamp + msecs_to_jiffies(max_age_ms)) ? 1 : 0;
}
#else
/* host test build: tick = 1 ms */
static inline int tx_isp_awb_zone_fresh(unsigned long stamp, unsigned long now,
					unsigned int max_age_ms)
{
	if (!stamp)
		return 0;
	return (long)(now - stamp) <= (long)max_age_ms ? 1 : 0;
}
#endif

#endif /* TX_ISP_AWB_ZONE_H */
