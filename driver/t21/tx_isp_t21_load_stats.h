/* SPDX-License-Identifier: GPL-2.0 */
#ifndef TX_ISP_T21_LOAD_STATS_H
#define TX_ISP_T21_LOAD_STATS_H

/*
 * T21 layout of the shared load statistics (TX_ISP_LOAD_STATS=y, see
 * ../include/tx_isp/tx_isp_load_stats.h). Parameters are t21_*.
 *
 * t21_isr_bits[0..31]: ISP core status word 0x84.
 * t21_work_*[0..31]: isp_fw_process event callbacks by event id. T21 has
 * no per-frame work items besides the event thread.
 */
#define TX_LS_PREFIX		t21_
#define TX_LS_ISR_WORDS		1
#define TX_LS_WORKS		32

#include "../include/tx_isp/tx_isp_load_stats.h"

#endif
