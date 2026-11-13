/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 LDC sub-device stream state (the "core ops" of the LDC sub-device):
 *
 *   t32_ldc_video_s_stream   ldc_video_s_stream 0xfcd0: enable: state 3 -> 4; disable: state 4 -> 3; returns 0
 *   t32_ldc_core_ops_init    ldc_core_ops_init 0xfd34: -22 (-EINVAL) for a NULL / error sub-device or private
 *                            pointer; state 1 is left alone; enable: state = 3; disable: a streaming (4) LDC is
 *                            first stopped through ldc_video_s_stream, then state 3 -> 2; returns 0
 *
 * The per-instance state words are at private + 288 + 4 * arg->idx (no range check in the vendor).  State
 * values as the vendor uses them: 1 (not initialised), 2 (initialised, idle), 3 (ready), 4 (streaming).
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15) by tests/t32_oracle (t32_ldcops_oracle).
 */
#ifndef TX_ISP_T32_LDCOPS_H
#define TX_ISP_T32_LDCOPS_H

#ifndef __KERNEL__
#include <stdint.h>
#ifndef T32_HOST_U32
#define T32_HOST_U32
typedef uint32_t u32;
#endif
#define T32_IS_ERR_OR_NULL(p)	(!(p) || (uintptr_t)(p) >= (uintptr_t)-4095)
#else
#include <linux/err.h>
#define T32_IS_ERR_OR_NULL(p)	IS_ERR_OR_NULL(p)
#endif

#define T32_LDC_ST_UNINIT	1
#define T32_LDC_ST_IDLE		2
#define T32_LDC_ST_READY	3
#define T32_LDC_ST_STREAMING	4
#define T32_LDC_EINVAL		((u32)-22)

struct t32_ldc_priv {
	unsigned char hdr[288];
	u32 state[2];		/* per instance, indexed by arg->idx */
};

struct t32_ldc_sd {
	unsigned char hdr[280];
	struct t32_ldc_priv *priv;	/* sub-device + 280 */
};

struct t32_ldc_stream_arg {
	u32 enable;
	u32 idx;
};

static inline u32 t32_ldc_video_s_stream(struct t32_ldc_sd *sd, const struct t32_ldc_stream_arg *arg)
{
	u32 *st = &sd->priv->state[arg->idx];

	if (arg->enable) {
		if (*st == T32_LDC_ST_READY)
			*st = T32_LDC_ST_STREAMING;
	} else if (*st == T32_LDC_ST_STREAMING) {
		*st = T32_LDC_ST_READY;
	}
	return 0;
}

static inline u32 t32_ldc_core_ops_init(struct t32_ldc_sd *sd, const struct t32_ldc_stream_arg *arg)
{
	struct t32_ldc_priv *priv;
	u32 *st;

	if (T32_IS_ERR_OR_NULL(sd))
		return T32_LDC_EINVAL;
	priv = sd->priv;
	if (T32_IS_ERR_OR_NULL(priv))
		return T32_LDC_EINVAL;
	st = &priv->state[arg->idx];
	if (*st == T32_LDC_ST_UNINIT)
		return 0;
	if (arg->enable) {
		*st = T32_LDC_ST_READY;
		return 0;
	}
	if (*st == T32_LDC_ST_STREAMING)
		t32_ldc_video_s_stream(sd, arg);
	if (*st == T32_LDC_ST_READY)
		*st = T32_LDC_ST_IDLE;
	return 0;
}

#endif
