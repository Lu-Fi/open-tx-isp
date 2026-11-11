/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP library: stream on.
 *
 *   t32_lib_stream_on   lib_tisp_stream_on 0x25aa0: memory not configured (dd674 != 1): trace, return its result.
 *                       Otherwise: two sensors (dc284 word 900 != 1): tisp_ldc_reg_reflash(0) and
 *                       tisp_rcfg_cfg_trig(0); one sensor: tisp_single_lsc_rcfg_cfg_trig(0).  Poll register
 *                       0x22014 for 0 (20000 reads; timeout: info log line 585), trace; two sensors: register
 *                       0x10a0 = 0; returns system_reg_write(0x1040, 1) (stream start).
 *
 * The printk traces (source lines 570 / 587) are a trace hook.  OS / block calls are hooks.  Checked bit-exact
 * against the vendor flow (Matt Davis' recovered C, PR #15) by tests/t32_oracle (t32_libstream_oracle).
 */
#ifndef TX_ISP_T32_LIBSTREAM_H
#define TX_ISP_T32_LIBSTREAM_H

#ifndef __KERNEL__
#include <stdint.h>
#ifndef T32_HOST_U32
#define T32_HOST_U32
typedef uint32_t u32;
#endif
#ifndef T32_HOST_U8
#define T32_HOST_U8
typedef uint8_t u8;
typedef uint16_t u16;
#endif
#endif

#define T32_STRM_RCFG_BUSY	0x22014U
#define T32_STRM_POLLS		20000U
#define T32_STRM_DUAL_CTRL	0x10a0U
#define T32_STRM_START		0x1040U
#define T32_STRM_SENSOR_NUM	900U	/* u32 in channel 0's state (t32_data_dc284) */

struct t32_stream_hooks {
	void *ctx;
	u32 (*sys_read)(void *ctx, u32 reg);			/* system_reg_read */
	u32 (*sys_write)(void *ctx, u32 reg, u32 val);		/* system_reg_write */
	u32 (*trace)(void *ctx, u32 line);			/* printk */
	void (*log)(void *ctx, u32 level, u32 line);		/* isp_printf */
	u32 (*ldc_reg_reflash)(void *ctx, u32 a0);		/* tisp_ldc_reg_reflash */
	u32 (*rcfg_cfg_trig)(void *ctx, u32 a0);		/* tisp_rcfg_cfg_trig */
	u32 (*single_lsc_rcfg_trig)(void *ctx, u32 a0);		/* tisp_single_lsc_rcfg_cfg_trig */
};

/* configured = t32_data_dd674, state = t32_data_dc284 */
static inline u32 t32_lib_stream_on(const struct t32_stream_hooks *h, const u8 *configured, const u8 *state)
{
	unsigned int n;
	int dual;

	if (*configured != 1)
		return h->trace(h->ctx, 570);
	dual = *(const u32 *)(state + T32_STRM_SENSOR_NUM) != 1;
	if (dual) {
		h->ldc_reg_reflash(h->ctx, 0);
		h->rcfg_cfg_trig(h->ctx, 0);
	} else {
		h->single_lsc_rcfg_trig(h->ctx, 0);
	}
	for (n = T32_STRM_POLLS; h->sys_read(h->ctx, T32_STRM_RCFG_BUSY);)
		if (!--n) {
			h->log(h->ctx, 1, 585);
			break;
		}
	h->trace(h->ctx, 587);
	if (*(const u32 *)(state + T32_STRM_SENSOR_NUM) != 1)
		h->sys_write(h->ctx, T32_STRM_DUAL_CTRL, 0);
	return h->sys_write(h->ctx, T32_STRM_START, 1);
}

#endif
