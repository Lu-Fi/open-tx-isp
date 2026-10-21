/* SPDX-License-Identifier: GPL-2.0 */
#ifndef TX_ISP_T23_LOAD_STATS_H
#define TX_ISP_T23_LOAD_STATS_H

/*
 * T23 layout of the shared load statistics (TX_ISP_LOAD_STATS=y, see
 * ../../include/tx_isp/tx_isp_load_stats.h). Parameters are t23_*.
 *
 * t23_isr_bits[0..31]: ISP core status word 0xb4.
 * Counted in isp_irq_handle_body() (core branch), the handler actually
 * registered; the lifted ispcore_interrupt_service_routine is not used.
 * t23_work_*[]: 0 frame-sync work, 1 AE work (lifted OEM AE), 2 AE work
 * (HLIL), 3 AWB work, 4 ADR work, 5 defog work.
 * The active ISR does not queue fs_work (only the unused lifted ISR did),
 * so index 0 stays 0 on the current driver.
 */
#define TX_LS_PREFIX		t23_
#define TX_LS_ISR_WORDS		1
#define TX_LS_WORKS		6

#define T23_LS_WORK_FS		0
#define T23_LS_WORK_AE_LIFT	1
#define T23_LS_WORK_AE_HLIL	2
#define T23_LS_WORK_AWB		3
#define T23_LS_WORK_ADR		4
#define T23_LS_WORK_DEFOG	5

#include "../../include/tx_isp/tx_isp_load_stats.h"

#endif
