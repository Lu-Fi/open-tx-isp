/* SPDX-License-Identifier: GPL-2.0 */
#ifndef TX_ISP_T23_AF_H
#define TX_ISP_T23_AF_H

/*
 * T23 auto-focus statistics chain, stock tx-isp-t23.ko (md5 8237acb1...,
 * libt23-firmware-1.3.0): tiziano_af_params_refresh, tiziano_af_set_hardware_param,
 * af_interrupt_static -> tisp_af_get_statistics + Tiziano_af_fpga,
 * tisp_af_set_attr(_refresh), tisp_af_get_attr/_metric/_zone,
 * tisp_s_af_weight.  The data handling is the one of the T31 chain
 * (driver/t31/tx_isp_t31_af.h, the same stock code): verified identical for
 * T23 by running both in the MIPS emulator (see tests/tx_isp_t23_af_test.c
 * and docs of the commit), only the places differ, which are fixed here.
 */
#include "../t31/tx_isp_t31_af.h"

/* the AF block of an IQ bank, from the stock tiziano_af_params_refresh */
#define T23_AF_ACTIVE_OFFSET 0x27c74U     /* in tparams (active bank) */
#define T23_AF_ACTIVE_WEIGHT_OFFSET 0x27ed4U
#define T23_AF_ACTIVE_BANK 0x13100U       /* start of the active bank */
/* the same block in the day / night bank (tisp_s_af_weight: +0x14dd4) */
#define T23_AF_BANK_OFFSET (T23_AF_ACTIVE_OFFSET - T23_AF_ACTIVE_BANK)
#define T23_AF_BANK_WEIGHT_OFFSET (T23_AF_ACTIVE_WEIGHT_OFFSET - T23_AF_ACTIVE_BANK)

/* core interrupt status bit: stock system_irq_func_set(0, 31, af_interrupt_static) */
#define T23_AF_IRQ_BIT 31U
/* statistics ring: 4 pages of 4 KiB (0xb8a8..0xb8b4), bank = register 0xb8bc */
#define T23_AF_STAT_BANK_REG 0xb8bcU
#define T23_AF_STAT_PAGES 4U
#define T23_AF_STAT_PAGE_BYTES 0x1000U

/* the parameter block has the same layout as on T31 (608 + 900 bytes) */
#define T23_AF_PARAM_BYTES (sizeof(struct t31_af_params))

/* a bank value the 4-page ring can hold (stock does not check) */
static inline int t23_af_bank_ok(uint32_t bank)
{
	return bank < T23_AF_STAT_PAGES;
}

#endif
