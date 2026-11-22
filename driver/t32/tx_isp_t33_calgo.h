/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 custom AE / AWB library commands and block sizes (vendor libimp 2.0.2.1
 * structures, openimp #805; tx_isp_t32_calgo.c).
 */
#ifndef TX_ISP_T33_CALGO_H
#define TX_ISP_T33_CALGO_H

#define T33_IOC_AE_ALGO_GET	0xc004541bU
#define T33_IOC_AE_ALGO_SET	0xc004541cU
#define T33_IOC_AE_ALGO_OPEN	0xc004541dU
#define T33_IOC_AE_ALGO_CLOSE	0xc004541eU
#define T33_IOC_AWB_ALGO_GET	0xc0045423U
#define T33_IOC_AWB_ALGO_SET	0xc0045424U
#define T33_IOC_AWB_ALGO_OPEN	0xc0045425U
#define T33_IOC_AWB_ALGO_CLOSE	0xc0045426U
#define T33_CALGO_AE_INFO	1996U	/* IMPISPAeInfo (T33 2.0.2.1) */
#define T33_CALGO_AE_INIT	128U	/* IMPISPAeInitAttr */
#define T33_CALGO_AE_ATTR	56U	/* IMPISPAeAttr */
#define T33_CALGO_AWB_INFO	12316U	/* IMPISPAwbInfo */
#define T33_CALGO_AWB_INIT	20U	/* IMPISPAwbInitAttr */
#define T33_CALGO_AWB_ATTR	16U	/* IMPISPAwbAttr */

#endif
