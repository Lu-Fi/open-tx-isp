/* SPDX-License-Identifier: GPL-2.0 */
#ifndef TX_ISP_T31_LOAD_STATS_H
#define TX_ISP_T31_LOAD_STATS_H

/*
 * T31 layout of the shared load statistics (TX_ISP_LOAD_STATS=y, see
 * ../../include/tx_isp/tx_isp_load_stats.h). Parameters are t31_*.
 *
 * t31_isr_bits[0..31]: ISP core status word 0xb4.
 * t31_work_*[0..31]: isp_fw_process event callbacks by event id
 * (tisp_event_push id); [32] frame-sync work, [33] sensor exposure work,
 * [34] WDR work, [35] day/night work.
 */
#define TX_LS_PREFIX		t31_
#define TX_LS_ISR_WORDS		1
#define TX_LS_WORKS		36

#define T31_LS_WORK_EVENT(id)	(id)
#define T31_LS_WORK_FS		32
#define T31_LS_WORK_EXPO	33
#define T31_LS_WORK_WDR		34
#define T31_LS_WORK_DAYNIGHT	35

#include "../../include/tx_isp/tx_isp_load_stats.h"

#endif
