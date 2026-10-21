/* SPDX-License-Identifier: GPL-2.0 */
#ifndef TX_ISP_T30_LOAD_STATS_H
#define TX_ISP_T30_LOAD_STATS_H

/*
 * T30 layout of the shared load statistics (TX_ISP_LOAD_STATS=y, see
 * ../include/tx_isp/tx_isp_load_stats.h). Parameters are t30_*.
 *
 * t30_isr_bits[0..15]: APICAL interrupt status 0xa0 (bits 16..31 unused).
 * t30_work_*[0..15]: firmware interrupt callbacks by source (run in the
 * hard IRQ), [16] ISP IRQ thread handler. The APICAL worker
 * (apical_process) sleeps inside, so it is not timed.
 */
#define TX_LS_PREFIX		t30_
#define TX_LS_ISR_WORDS		1
#define TX_LS_WORKS		17

#define T30_LS_WORK_IRQ_THREAD	16

#include "../include/tx_isp/tx_isp_load_stats.h"

#endif
