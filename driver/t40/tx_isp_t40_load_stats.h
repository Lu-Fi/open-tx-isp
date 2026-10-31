/* SPDX-License-Identifier: GPL-2.0 */
#ifndef TX_ISP_T40_LOAD_STATS_H
#define TX_ISP_T40_LOAD_STATS_H

/*
 * T40 layout of the shared load statistics (TX_ISP_LOAD_STATS=y, see
 * ../include/tx_isp/tx_isp_load_stats.h). Parameters are t40_*.
 *
 * t40_isr_bits[]: [0..31] status 0x40028, [32..63] status 0x40038,
 * [64..95] error 0x40068 (main ISR path; the enable_oem_isr_lit path is
 * not counted).
 * t40_work_*[]: [0..31] tisp event callbacks of channel 0 by event id,
 * [32..63] channel 1, [64] main fs work, [65] sec fs work, [66] main fd
 * work, [67] sec fd work, [68] frame-done work (all channels), [69] ADR
 * process work, [70] translated vendor AWB process (inside event 14), [71] its EV
 * follow-up, [72] its CT follow-up (CCM/LSC/BCSH), [73] frame-3A sampler work
 * (kworker: Y/UV plane sampling through an uncached window + AE; not part of any tisp event thread).
 */
#define TX_LS_PREFIX		t40_
#define TX_LS_ISR_WORDS		3
#define TX_LS_WORKS		74

#define T40_LS_WORK_EVENT(ch, ev)	((ch) * 32 + (ev))
#define T40_LS_WORK_MAIN_FS	64
#define T40_LS_WORK_SEC_FS	65
#define T40_LS_WORK_MAIN_FD	66
#define T40_LS_WORK_SEC_FD	67
#define T40_LS_WORK_FRAME_DONE	68
#define T40_LS_WORK_ADR		69
#define T40_LS_WORK_VAWB_PROC	70
#define T40_LS_WORK_VAWB_EV	71
#define T40_LS_WORK_VAWB_CT	72
#define T40_LS_WORK_FRAME3A	73

#include "../include/tx_isp/tx_isp_load_stats.h"

#endif
