/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) D-PHY settle time (openimp #99), binary oracle block "mipi"
 * (tests/t33_binoracle/mipi.conf): get_settle_time, the vendor's table from
 * the sensor's MIPI clock (Mbps, sensor attribute +0x1c) that
 * mipi_phy_stream_on writes to the D-PHY lane words 0x160 + n * 0x80.  The
 * T32 module computes 115 * clk / 4000 - 3 instead (tx_isp_t32_vic.h).
 */
#ifndef TX_ISP_T33_MIPI_H
#define TX_ISP_T33_MIPI_H

/* { clock band end (Mbps, exclusive), settle in the band }; 2400 and above: 0x46 */
static const short t33_settle_tab[][2] = {
	{ 110, 2 }, { 150, 3 }, { 300, 6 }, { 400, 8 }, { 500, 0xb }, { 600, 0xe }, { 700, 0x10 },
	{ 800, 0x12 }, { 1000, 0x16 }, { 1200, 0x1e }, { 1400, 0x23 }, { 1600, 0x2d }, { 1800, 0x32 },
	{ 2000, 0x37 }, { 2200, 0x3c }, { 2400, 0x41 },
};

/* get_settle_time (signed clock: negatives give 2) */
static inline unsigned int t33_settle_time(int clk)
{
	unsigned int i;

	for (i = 0; i < sizeof(t33_settle_tab) / sizeof(t33_settle_tab[0]); i++)
		if (clk < t33_settle_tab[i][0])
			return t33_settle_tab[i][1];
	return 0x46;
}

#endif /* TX_ISP_T33_MIPI_H */
