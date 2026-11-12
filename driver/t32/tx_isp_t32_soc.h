/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Per-SoC constants of the T32-family driver.  driver/t32 builds the T32
 * (PRJ007) module; driver/t33 builds the same sources with TX_ISP_T33 for the
 * T33 (PRJ008), which shares the ioctl ABI, the register map and the
 * interrupt layout with the T32 (T33 plan, step 1).
 *
 * DEFAULT_BIN: size of the IMPISPBinInfo libimp passes, and the offsets of
 * its version[8] and soc[8] members.  The T33 2.0.2.1 header suggests a
 * 856-byte struct (no paddr[6] in tisp_bin_attr_t), but the camera libimp
 * 2.0.2.1 compares version / soc at 863 / 871 of an 880-byte buffer and the
 * vendor T33 module writes "1.00" / "PRJ008" there (QEMU trace, T33 plan
 * step 2): the T33 uses the T32 layout.
 *
 * Clocks: rates the vendor module asks for (isp_clk, isp_clkl, isp_clkv
 * defaults).  On the T33 the probe and working rates are the same; the
 * vendor module without parameters ends at ISP 325 / LDC 325 / VPU 316.7 /
 * ISPA 475 MHz in QEMU (mpll 1300, sclka 950).  The T33 clock names and
 * parents are in tx_isp_t32_core.c (t32_clks).
 */
#ifndef TX_ISP_T32_SOC_H
#define TX_ISP_T32_SOC_H

#ifdef TX_ISP_T33
#define T32_SOC_NAME		"PRJ008"
#define T32_SOC_DRV		"tx-isp-t33"
#define T32_SOC_DESC		"Open Ingenic T33 ISP driver (T32 family, skeleton)"
#define T32_DEFAULT_BIN_SIZE	880
#define T32_DEFAULT_BIN_VER	863
#define T32_DEFAULT_BIN_SOC	871
#define T32_CLK_ISP_PROBE	340000000UL
#define T32_CLK_ISP		340000000UL
#define T32_CLK_LDC_PROBE	357000000UL
#define T32_CLK_LDC		357000000UL
#define T32_CLK_VPU_PROBE	340000000UL
#define T32_CLK_VPU		340000000UL
/*
 * T33 interrupts (QEMU trace of the vendor module): nothing is written to
 * the ISP before its clock is on (no masking at probe), the list-done word
 * 0x300c0 is enabled and cleared like 0x300d0, and stream-off masks the
 * VIC interrupts again.
 */
#define T32_SOC_LIST_IRQ	1
#else
#define T32_SOC_NAME		"PRJ007"
#define T32_SOC_DRV		"tx-isp-t32"
#define T32_SOC_DESC		"Open Ingenic T32 ISP driver (S1 skeleton)"
#define T32_DEFAULT_BIN_SIZE	880
#define T32_DEFAULT_BIN_VER	863
#define T32_DEFAULT_BIN_SOC	871
#define T32_CLK_ISP_PROBE	200000000UL
#define T32_CLK_ISP		219000000UL
#define T32_CLK_LDC_PROBE	588000000UL
#define T32_CLK_LDC		588000000UL
#define T32_CLK_VPU_PROBE	500000000UL
#define T32_CLK_VPU		500000000UL
#define T32_SOC_LIST_IRQ	0
#endif

#endif /* TX_ISP_T32_SOC_H */
