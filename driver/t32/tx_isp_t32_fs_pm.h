/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 frame-source (fs, the /dev/framechanN channels) module glue from the
 * vendor module (Matt Davis' recovered C, PR #15): fs_activate_module
 * (0x10590), fs_complete_module (0x13a60), fs_suspend_module (0x13abc),
 * fs_slake_module (0x1432c) and frame_channel_unlocked_ioctl_first (0x1443c).
 *
 * fs device (vendor offsets): channel array pointer +0x120, channel count
 * +0x124 (signed), module state +0x12c (1 slaked, 2 activated).  Channel
 * (816 bytes): misc device registered +0x28, vb2 queue +0x2c (its first word
 * is handed to the stream-off), queue memory +0x214, channel state +0x2e0
 * (1 slaked, 2 activated, 4 streaming).  The vb2 / misc calls and
 * frame_channel_unlocked_ioctl are hooks.
 *
 * Vendor quirks kept: activate stops at the first channel not in state 1
 * with -1 (the channels before it stay activated, the module state is not
 * changed); complete returns isp_printf's result on its error paths (tail
 * call) and 0 otherwise; suspend prints (level 0) and returns 0 for an
 * active module.
 *
 * Checked bit-exact against the vendor flow by tests/t32_oracle
 * (t32_fspm_oracle).
 */
#ifndef TX_ISP_T32_FS_PM_H
#define TX_ISP_T32_FS_PM_H

#include "tx_isp_t32_glue.h"

int isp_printf(unsigned int level, const char *fmt, ...);

#define T32_FS_CHANS		0x120U
#define T32_FS_NCHAN		0x124U
#define T32_FS_STATE		0x12cU
#define T32_FS_ST_SLAKED	1U
#define T32_FS_ST_ACTIVE	2U
#define T32_FCH_BYTES		816U
#define T32_FCH_MISC		0x28U
#define T32_FCH_VBQ		0x2cU
#define T32_FCH_QMEM		0x214U
#define T32_FCH_STATE		0x2e0U
#define T32_FCH_ST_STREAM	4U

struct t32_fs_os {
	void (*streamoff)(u8 *ch, u32 q0);		/* __frame_channel_vb2_streamoff */
	void (*queue_free)(u8 *q, u32 mem);		/* __vb2_queue_free */
	void (*misc_deregister)(u8 *ch);		/* private_misc_deregister */
	long (*unlocked_ioctl)(u32 file, u32 cmd, u32 arg);	/* frame_channel_unlocked_ioctl */
};

static inline int t32_fs_valid(const u8 *p)
{
	return p && (uintptr_t)p < (uintptr_t)(u32)-4095;
}

static inline u8 *t32_fs_chan(const u8 *fs, u32 i)
{
	return T32_PTR(t32_g_rd32(fs, T32_FS_CHANS)) + i * T32_FCH_BYTES;
}

static inline int t32_fs_activate_module(u8 *fs)
{
	int i;

	if (!t32_fs_valid(fs))
		return -22;
	if (t32_g_rd32(fs, T32_FS_STATE) != T32_FS_ST_SLAKED)
		return 0;
	for (i = 0; i < (int)t32_g_rd32(fs, T32_FS_NCHAN); i++) {
		u8 *ch = t32_fs_chan(fs, (u32)i);

		if (t32_g_rd32(ch, T32_FCH_STATE) != T32_FS_ST_SLAKED) {
			isp_printf(2, "framesource: channel %d state != %d\n", i, 2);
			return -1;
		}
		t32_g_wr32(ch, T32_FCH_STATE, T32_FS_ST_ACTIVE);
	}
	t32_g_wr32(fs, T32_FS_STATE, T32_FS_ST_ACTIVE);
	return 0;
}

static inline int t32_fs_complete_module(u8 *fs)
{
	if (!t32_fs_valid(fs))
		return isp_printf(2, "%s[%d]: invalid fs\n", "fs_complete_module", 1948);
	if ((int)t32_g_rd32(fs, T32_FS_STATE) < 2)
		return isp_printf(2, "%s[%d]: fs not active\n", "fs_complete_module", 1953);
	return 0;
}

static inline int t32_fs_suspend_module(u8 *fs)
{
	if (!t32_fs_valid(fs)) {
		isp_printf(2, "%s[%d]: invalid fs\n", "fs_suspend_module", 1859);
		return -22;
	}
	if ((int)t32_g_rd32(fs, T32_FS_STATE) >= 2)
		isp_printf(0, "%s[%d]: suspend\n", "fs_suspend_module", 1867);
	return 0;
}

static inline int t32_fs_slake_module(const struct t32_fs_os *os, u8 *fs)
{
	int i;

	if (!t32_fs_valid(fs))
		return -22;
	if (t32_g_rd32(fs, T32_FS_STATE) == T32_FS_ST_SLAKED)
		return 0;
	for (i = 0; i < (int)t32_g_rd32(fs, T32_FS_NCHAN); i++) {
		u8 *ch = t32_fs_chan(fs, (u32)i);

		if (t32_g_rd32(ch, T32_FCH_STATE) == T32_FCH_ST_STREAM) {
			os->streamoff(ch, t32_g_rd32(ch, T32_FCH_VBQ));
			os->queue_free(ch + T32_FCH_VBQ, t32_g_rd32(ch, T32_FCH_QMEM));
		}
		if (t32_g_rd32(ch, T32_FCH_MISC)) {
			os->misc_deregister(ch);
			t32_g_wr32(ch, T32_FCH_MISC, 0);
		}
		t32_g_wr32(ch, T32_FCH_STATE, T32_FS_ST_SLAKED);
	}
	t32_g_wr32(fs, T32_FS_STATE, T32_FS_ST_SLAKED);
	return 0;
}

static inline long t32_frame_channel_unlocked_ioctl_first(const struct t32_fs_os *os, u32 file, u32 cmd, u32 arg)
{
	return os->unlocked_ioctl(file, cmd, arg);
}

#endif
