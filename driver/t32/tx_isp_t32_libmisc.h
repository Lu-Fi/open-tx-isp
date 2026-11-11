/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP library glue: ISP core interrupt masks and the bin information / path getters.
 *
 *   t32_lib_intp_enable    lib_tisp_intp_enable 0xac148: the six ISP core interrupt enable words = 0xffffffff
 *   t32_lib_intp_disable   lib_tisp_intp_disable 0xac1c0: the same six words = 0
 *   t32_lib_intp_clear     lib_tisp_intp_clear 0xac238: the seven interrupt status (clear) words = 0xffffffff,
 *                          in the vendor order (0x30064 last)
 *     The return value is the one of the last system_reg_write (tail call in the vendor).
 *   t32_lib_get_bin_info   lib_tisp_get_bin_info 0x27f1c: version tag "1.00" (8 bytes, t32_data_c8064) to
 *                          info + 863, then snprintf(info + 871, 8, "PRJ007") (t32_data_c8070, the project
 *                          name; the vendor's 4th argument is not used by the format); returns snprintf's result
 *   t32_lib_get_bin_path   lib_tisp_get_bin_path 0x28748: for channel (ch & 0xff): word 48 of the channel state,
 *                          the 128-byte bin file name (t32_data_da850 + 336 * ch), 768 bytes of channel state
 *                          from 132 and 6 bytes from 126 into the path block; returns memcpy's result (its
 *                          destination).  No channel range check in the vendor: a channel >= 2 reads past the
 *                          two state / name blocks (vendor behaviour, kept).
 *
 * Hooks: struct t32_lib_hooks (tx_isp_t32_libwrap.h).  Checked bit-exact against the vendor flow (Matt Davis'
 * recovered C, PR #15) by tests/t32_oracle (t32_libmisc_oracle).
 */
#ifndef TX_ISP_T32_LIBMISC_H
#define TX_ISP_T32_LIBMISC_H

#include "tx_isp_t32_libwrap.h"
#ifndef __KERNEL__
#include <stdio.h>
#include <string.h>
#endif

#define T32_INTP_ALL		0xffffffffU
#define T32_INTP_NONE		0U

/* ISP core interrupt enable words (0x30000 + n) */
static const u32 t32_intp_en_regs[6] = { 0x30020, 0x30030, 0x30060, 0x300a0, 0x300b0, 0x300d0 };
/* interrupt status words, written 1 to clear */
static const u32 t32_intp_clr_regs[7] = { 0x30024, 0x30034, 0x300a4, 0x300b4, 0x300d4, 0x2202c, 0x30064 };

static inline u32 t32_lib_intp_write(const struct t32_lib_hooks *h, const u32 *regs, unsigned int n, u32 val)
{
	unsigned int i;
	u32 r = 0;

	for (i = 0; i < n; i++)
		r = h->sys_write(h->ctx, regs[i], val);
	return r;
}

static inline u32 t32_lib_intp_enable(const struct t32_lib_hooks *h)
{
	return t32_lib_intp_write(h, t32_intp_en_regs, 6, T32_INTP_ALL);
}

static inline u32 t32_lib_intp_disable(const struct t32_lib_hooks *h)
{
	return t32_lib_intp_write(h, t32_intp_en_regs, 6, T32_INTP_NONE);
}

static inline u32 t32_lib_intp_clear(const struct t32_lib_hooks *h)
{
	return t32_lib_intp_write(h, t32_intp_clr_regs, 7, T32_INTP_ALL);
}

#define T32_BIN_INFO_VERSION	863U	/* 8 bytes */
#define T32_BIN_INFO_PROJECT	871U	/* 8 bytes */

/* version = t32_data_c8064 ("1.00", 8 bytes), project = t32_data_c8070 ("PRJ007", used as the format) */
static inline u32 t32_lib_get_bin_info(unsigned char *info, const char *version, const char *project)
{
	memcpy(info + T32_BIN_INFO_VERSION, version, 8);
	return (u32)snprintf((char *)info + T32_BIN_INFO_PROJECT, 8, project);
}

#define T32_CH_STATE_BIN_W48	48U	/* u32 copied to path + 0 */
#define T32_CH_STATE_BIN_A	132U	/* 768 bytes -> path + 132 */
#define T32_CH_STATE_BIN_B	126U	/* 6 bytes -> path + 900 */
#define T32_BIN_NAME_SIZE	336U	/* t32_data_da850 block per channel */

/* state = t32_data_dc284, names = t32_data_da850 */
static inline u32 t32_lib_get_bin_path(const unsigned char *state, const unsigned char *names, u32 ch,
				       unsigned char *path)
{
	const unsigned char *st;

	ch &= 0xff;
	st = state + T32_CH_STATE_SIZE_LW * ch;
	memcpy(path, st + T32_CH_STATE_BIN_W48, 4);
	memcpy(path + 132, st + T32_CH_STATE_BIN_A, 768);
	memcpy(path + 4, names + T32_BIN_NAME_SIZE * ch, 128);
	return (u32)(uintptr_t)memcpy(path + 900, st + T32_CH_STATE_BIN_B, 6);
}

#endif
