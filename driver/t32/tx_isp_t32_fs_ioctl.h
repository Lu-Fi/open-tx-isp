/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 frame-source core ioctl from the vendor module (Matt Davis' recovered
 * C, PR #15): fs_core_ops_ioctl (0x12ce8).
 *
 * fs device layout: tx_isp_t32_fs_pm.h; +0x128 is the IRQ-off flag and its
 * spin lock (the same word: the lock is empty on the uniprocessor T32).
 * 0x1000002 registers the misc devices of channel group *arg (1: channels
 * 0..2, 2: 3..5, 3: 6..8, 4: 9..11, others: nothing); 0x100000e / 0x100000f
 * set the flag to 0 / 1 under the lock.  Every other command returns 0.
 * tx_isp_frame_chan_misc_register / private_misc_deregister and the lock are
 * hooks of struct t32_fs_ioctl_os.
 *
 * Vendor quirks kept: the channel array is read before the fs pointer is
 * used, through NULL for a NULL / ERR_PTR fs (an oops; never exercised by the
 * oracle); when a registration fails, the channels below it are deregistered
 * down to channel 1, never channel 0, and across the group start (a failure
 * at channel 3 deregisters 2 and 1), and its error is returned; a failure
 * at channel 0 counts down from -1 and runs below the channel array (never
 * exercised by the oracle).
 *
 * Checked bit-exact against the vendor flow by tests/t32_oracle
 * (t32_fsioc_oracle).
 */
#ifndef TX_ISP_T32_FS_IOCTL_H
#define TX_ISP_T32_FS_IOCTL_H

#include "tx_isp_t32_fs_pm.h"

#define T32_FS_IRQ_OFF		0x128U
#define T32_FS_IOC_MISC_REG	0x1000002U
#define T32_FS_IOC_IRQ_ON	0x100000eU	/* flag = 0 */
#define T32_FS_IOC_IRQ_OFF	0x100000fU	/* flag = 1 */
#define T32_FS_GROUP_CHANS	3U

struct t32_fs_ioctl_os {
	int (*misc_register)(u8 *ch);			/* tx_isp_frame_chan_misc_register */
	void (*misc_deregister)(u8 *ch);		/* private_misc_deregister */
	void (*lock)(u8 *lock, u32 *flags);		/* __private_spin_lock_irqsave */
	void (*unlock)(u8 *lock, u32 flags);		/* private_spin_unlock_irqrestore */
};

static inline int t32_fs_core_ops_ioctl(const struct t32_fs_ioctl_os *os, u8 *fs, u32 cmd, const u8 *arg)
{
	u8 *f = t32_fs_valid(fs) ? fs : 0;
	u8 *chans = T32_PTR(t32_g_rd32(f, T32_FS_CHANS));
	u32 flags = 0, grp, j;
	int r;

	switch (cmd) {
	case T32_FS_IOC_MISC_REG:
		grp = t32_g_rd32(arg, 0);
		if (grp < 1 || grp > 4)
			return 0;
		for (j = (grp - 1) * T32_FS_GROUP_CHANS; j < grp * T32_FS_GROUP_CHANS; j++) {
			r = os->misc_register(chans + j * T32_FCH_BYTES);
			if (!r)
				continue;
			while (--j) {
				os->misc_deregister(chans + j * T32_FCH_BYTES);
				t32_g_wr32(chans + j * T32_FCH_BYTES, T32_FCH_MISC, 0);
			}
			return r;
		}
		return 0;
	case T32_FS_IOC_IRQ_ON:
	case T32_FS_IOC_IRQ_OFF:
		os->lock(f + T32_FS_IRQ_OFF, &flags);
		t32_g_wr32(f, T32_FS_IRQ_OFF, cmd == T32_FS_IOC_IRQ_OFF);
		os->unlock(f + T32_FS_IRQ_OFF, flags);
		return 0;
	default:
		return 0;
	}
}

#endif
