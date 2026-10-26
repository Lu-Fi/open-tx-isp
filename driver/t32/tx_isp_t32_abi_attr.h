/* SPDX-License-Identifier: GPL-2.0 */
/* T32 sensor attribute offsets (kernel and host tests). */
#ifndef TX_ISP_T32_ABI_ATTR_H
#define TX_ISP_T32_ABI_ATTR_H

/*
 * struct tx_isp_sensor_attribute (0x190 bytes; the sensor's own copy, which
 * video.attr points to).  Offsets from the T32 SDK header layout; the MIPI
 * bus struct (tx_isp_mipi_bus) starts at 0x18 and is the T41 one.
 */
#define T32_ATTR_DBUS_TYPE		0x014	/* 1 MIPI, 2 DVP */
#define T32_ATTR_MIPI_MODE		0x018	/* 0 other, 1 Sony */
#define T32_ATTR_MIPI_CLK		0x01c	/* Mbps per lane */
#define T32_ATTR_MIPI_INDEX		0x028	/* u8: CSI host */
#define T32_ATTR_MIPI_LANES		0x029	/* u8 */
#define T32_ATTR_MIPI_TWIDTH		0x030	/* image_twidth */
#define T32_ATTR_MIPI_THEIGHT		0x034
#define T32_ATTR_MIPI_SC		0x044	/* struct vic_mipi_sensor_ctl */
#define T32_ATTR_SC_HCROP_DIFF_EN	0x044
#define T32_ATTR_SC_VCOMP_EN		0x048
#define T32_ATTR_SC_HCOMP_EN		0x04c
#define T32_ATTR_SC_CROP0X		0x050	/* u16 x 8: start0x, 0y, 1x, 1y .. 3y */
#define T32_ATTR_SC_LINE_SYNC		0x060
#define T32_ATTR_SC_WORK_START		0x064
#define T32_ATTR_SC_DT_EN		0x068
#define T32_ATTR_SC_DT_VALUE		0x06c	/* MIPI data type, 0x2b RAW10 */
#define T32_ATTR_SC_DEL_START		0x070
#define T32_ATTR_SC_FID_MODE		0x074
#define T32_ATTR_SC_FRAME_MODE		0x078	/* 0 linear, 1/2 WDR 2/3 frames */
#define T32_ATTR_SC_SENSOR_MODE		0x07c
#define T32_ATTR_SC_CSI_FMT		0x080	/* 0 RAW8, 1 RAW10, 2 RAW12, 7 YUV422 */
#define T32_ATTR_DATA_TYPE		0x094	/* linear / WDR */
#define T32_ATTR_TOTAL_WIDTH		0x114	/* u16 HTS */
#define T32_ATTR_TOTAL_HEIGHT		0x116	/* u16 VTS */
#define T32_ATTR_SIZE			0x190

#define T32_DBUS_MIPI			1
#define T32_DBUS_DVP			2

#endif /* TX_ISP_T32_ABI_ATTR_H */
