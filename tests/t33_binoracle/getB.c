/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "getB": our LSC info allocation and AE sensor
 * trigger (driver/t32/tx_isp_t33_getB.h) with the vendor calling convention,
 * run against the vendor tx-isp-t33.ko instructions (spec: getB.conf).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_getB.h"

/* module statics the vendor code reads/writes (GLOBAL lines) */
u32 *lsc_para[4];
u8 ae_init_flag[4];
u32 *ae_cache[4];

/* kernel import: neither side runs it, the call is recorded */
extern void *__kmalloc(u32 size, u32 flags);

void bo_tisp_lsc_malloc_info(u32 ch) { t33_lsc_malloc_info(ch); }
void bo_tisp_normal_ae_par_sensor_trig(u32 ch) { t33_normal_ae_par_sensor_trig(ch); }
