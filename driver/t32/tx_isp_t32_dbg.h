/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32: the vendor's debug-print helpers of tx-isp-t32.o (Matt Davis' recovered
 * C, PR #15) as readable C: the register dumps dump_vic_reg
 * (0x2eac) / dump_csi_reg (0x872c) and the two error watchers check_vic_error
 * (0x2f2c) / check_csi_error (0x88bc), the array printers ISPWARNING (0x1fea8)
 * and print_array_awb_8/16/32 (0x3a620 / 0x3a5a0 / 0x3a520), the five
 * /proc open wrappers (dump_isp_vic_frd_open 0x17f4, dump_isp_csi_open 0x74f8,
 * dump_ldc_frd_open 0xfe94, dump_isp_framesource_open 0x125e0,
 * dump_isp_info_open 0xb5ec4) and the debug stubs that the vendor build leaves
 * empty (tisp_*_print_para, tisp_clm_print_struct, tmo_params_print,
 * tisp_ldc_debug, dump_msca_regs: they do nothing and return 0).
 *
 * Checked by tests/t32_oracle (t32_dbg_oracle): the sequence of isp_printf /
 * printk calls (level, format text, the arguments the format uses) against
 * the recovered C, on random register images.  The format texts are the
 * vendor's .rodata.str (tests/t32_oracle/data_str.py).
 *
 * Vendor behaviour kept on purpose, named here and not fixed:
 *  - t32_dump_vic_reg hands the register OFFSET to the "%s" of its format
 *    ("[ %s:%d ] register is 0x%x, value is 0x%x"): the vendor passes
 *    (1, fmt, offset, value) only, so at level >= print_level the "%s" would
 *    dereference the offset;
 *  - t32_check_vic_error / t32_check_csi_error never return (vendor busy loop
 *    that dumps the registers over and over); one pass of the loop body is
 *    t32_check_*_error_pass, which is what the oracle compares;
 *  - the first line of dump_csi_reg passes its caller's a2 / a3 through to
 *    isp_printf (the format uses none of them), ours passes 0.
 */
#ifndef TX_ISP_T32_DBG_H
#define TX_ISP_T32_DBG_H

#include "tx_isp_t32_glue.h"
#include <stdarg.h>
#include <stddef.h>

#ifdef __KERNEL__
#include <linux/kernel.h>
#define T32_DBG_PRINTK		printk
#endif
/* host oracle: defines T32_DBG_PRINTK and isp_printf before the include */

#ifndef __maybe_unused
#define __maybe_unused __attribute__((unused))
#endif

int isp_printf(unsigned int level, const char *fmt, ...);

#define T32_DBG_REGS		244U	/* vic / csi device: register base pointer */
#define T32_VIC_DUMP_BYTES	436U
#define T32_CSI_ERR1		32U
#define T32_CSI_ERR2		36U

/* ---- register dumps and error watchers ---- */
static inline void t32_dump_vic_reg(const u8 *vic)
{
	u32 off;

	for (off = 0; off != T32_VIC_DUMP_BYTES; off += 4)
		isp_printf(1, "[ %s:%d ] register is 0x%x, value is 0x%x\n", off,
			   t32_g_rd32(T32_PTR(t32_g_rd32(vic, T32_DBG_REGS)), off));
}

/* one pass of check_vic_error: the vendor repeats it forever; vic_global = the word at t32_data_d9348 */
static inline void t32_check_vic_error_pass(const u32 *vic_global)
{
	t32_dump_vic_reg(T32_PTR(*vic_global));
}

static inline void t32_dump_csi_reg(const u8 *csi)
{
	static const char *const fmt[14] = {
		"**********VERSION =%08x\n",
		"**********N_LANES =%08x\n",
		"**********PHY_SHUTDOWNZ = %08x\n",
		"**********DPHY_RSTZ = %08x\n",
		"**********CSI2_RESETN =%08x\n",
		"**********PHY_STATE = %08x\n",
		"**********DATA_IDS_1 = %08x\n",
		"**********DATA_IDS_2 = %08x\n",
		"**********ERR1 = %08x\n",
		"**********ERR2 = %08x\n",
		"**********MASK1 =%08x\n",
		"**********MASK2 =%08x\n",
		"**********PHY_TST_CTRL0 = %08x\n",
		"**********PHY_TST_CTRL1 = %08x\n",
	};
	u32 i;

	isp_printf(0, "****>>>>> dump csi reg <<<<<******\n", 0, 0);
	for (i = 0; i < 14; i++)
		isp_printf(0, fmt[i], t32_g_rd32(T32_PTR(t32_g_rd32(csi, T32_DBG_REGS)), 4 * i));
}

static inline void t32_check_csi_error_pass(const u32 *csi_global)
{
	const u8 *csi = T32_PTR(*csi_global);
	const u8 *regs;
	u32 err1, err2;

	t32_dump_csi_reg(csi);
	regs = T32_PTR(t32_g_rd32(T32_PTR(*csi_global), T32_DBG_REGS));
	err1 = t32_g_rd32(regs, T32_CSI_ERR1);
	err2 = t32_g_rd32(regs, T32_CSI_ERR2);
	if (err1)
		isp_printf(0, "error-------- 1:0x%08x\n", err1);
	if (err2)
		isp_printf(0, "error-------- 2:0x%08x\n", err2);
}

/* ---- array printers (printk) ---- */
/* ISPWARNING: rows x cols u32 words, each >> shift, "%d," per word and a newline per row, one more newline at the end */
static inline void t32_array_print(const u32 *a, int rows, u32 cols, u32 shift)
{
	int r;
	u32 c;

	for (r = 0; r != rows; r++, a += cols) {
		for (c = 0; c != cols; c++)
			T32_DBG_PRINTK("%d,", a[c] >> (shift & 31));
		T32_DBG_PRINTK("\n");
	}
	T32_DBG_PRINTK("\n");
}

#define T32_DBG_AWB_ARRAY(name, type)						\
static inline void name(const type *a, int n)					\
{										\
	int i;									\
										\
	for (i = 0; i < n; i++)							\
		T32_DBG_PRINTK(" %d,", a[i]);					\
	T32_DBG_PRINTK("]\n");							\
}
T32_DBG_AWB_ARRAY(t32_print_array_awb_32, u32)
T32_DBG_AWB_ARRAY(t32_print_array_awb_16, u16)
T32_DBG_AWB_ARRAY(t32_print_array_awb_8, u8)

/* ---- /proc open wrappers: single_open_size(file, show, PDE_DATA(inode), size) ---- */
struct t32_dbg_os {
	void *(*pde_data)(void *inode);
	int (*single_open_size)(void *file, void *show, void *data, size_t size);
};

static inline int t32_dump_open(const struct t32_dbg_os *os, void *inode, void *file, void *show, size_t size)
{
	void *data = os->pde_data(inode);

	return os->single_open_size(file, show, data, size);
}
#define T32_DUMP_VIC_FRD_SIZE		1024U	/* dump_isp_vic_frd_open -> isp_vic_frd_show */
#define T32_DUMP_CSI_SIZE		1024U	/* dump_isp_csi_open -> isp_csi_show */
#define T32_DUMP_LDC_SIZE		1024U	/* dump_ldc_frd_open -> ldc_cmd_show */
#define T32_DUMP_FRAMESOURCE_SIZE	2048U	/* dump_isp_framesource_open -> isp_framesource_show */
#define T32_DUMP_INFO_SIZE		8192U	/* dump_isp_info_open -> isp_info_show */

/* ---- debug stubs the vendor build leaves empty ---- */
static inline int t32_tisp_print_para_stub(void)	/* tisp_{gib,lsc,gamma,ccm,bcsh,csc,csccr}_print_para, tmo_params_print, tisp_ldc_debug, dump_msca_regs */
{
	return 0;
}
static inline void t32_tisp_clm_print_struct(void) { }

#endif
