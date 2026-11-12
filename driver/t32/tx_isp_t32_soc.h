/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Per-SoC constants of the T32-family driver.  driver/t32 builds the T32
 * (PRJ007) module; driver/t33 builds the same sources with TX_ISP_T33 for the
 * T33 (PRJ008), which shares the ioctl ABI, the register map and the
 * interrupt layout with the T32 (T33 plan, step 1).
 *
 * DEFAULT_BIN: size of the IMPISPBinInfo libimp passes, and the offsets of
 * its version[8] and soc[8] members.  The T33 2.0.2.1 header has no
 * paddr[6] in tisp_bin_attr_t (828 bytes, T32 852), so the struct is 24
 * bytes shorter: 856 / 839 / 847 against 880 / 863 / 871 on the T32.
 *
 * Clocks: working rates of the vendor glue (isp_clk, isp_clkl, isp_clkv);
 * the scaler and AXI rates and the parents (sclka, mpll) are the same on
 * both SoCs.  The T33 probe rates are taken equal to the working rates until
 * the T33 QEMU trace (step 2) shows the platform table.
 */
#ifndef TX_ISP_T32_SOC_H
#define TX_ISP_T32_SOC_H

#ifdef TX_ISP_T33
#define T32_SOC_NAME		"PRJ008"
#define T32_SOC_DRV		"tx-isp-t33"
#define T32_SOC_DESC		"Open Ingenic T33 ISP driver (T32 family, skeleton)"
#define T32_DEFAULT_BIN_SIZE	856
#define T32_DEFAULT_BIN_VER	839
#define T32_DEFAULT_BIN_SOC	847
#define T32_CLK_ISP_PROBE	340000000UL
#define T32_CLK_ISP		340000000UL
#define T32_CLK_LDC_PROBE	357000000UL
#define T32_CLK_LDC		357000000UL
#define T32_CLK_VPU_PROBE	340000000UL
#define T32_CLK_VPU		340000000UL
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
#endif

#endif /* TX_ISP_T32_SOC_H */
