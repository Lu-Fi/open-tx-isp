/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP core glue, part 19: the proc write handlers of the core / CSI debug files.
 * The seq_file of the proc file is file->private_data (+120), its private word (+72) is the
 * subdev and the subdev's private block is at +280.
 *
 * isp_csi_cmd_set (0x84fc): copies the user text (up to 32 bytes into the static buffer bss
 *   d9660, longer into a kmalloc'ed one) and on "reset" sets every lane state (+704, count
 *   +860) to 3 under the CSI lock (+732), clears +856 and restarts every PHY lane
 *   (csi_phy_start(csi, lane)).  Result: the length, -ENOMEM, -EFAULT, or for an invalid
 *   subdev the seq_file's word +12 after printing the error to it.
 * isp_save_cmd_set (0xb6204): copies the user text to the core's command buffer (+784, 64 B,
 *   cleared afterwards) and runs "get_vb" (flags +944 / +945 = 1, msleep(200)),
 *   "isp_reg_reset" (lib_tisp_func_stop_reset) or "w <key> <value> ..." with the keys
 *   dbgmode / items / interval / vinum (words +744.. +756, "0x" / "0X" values hexadecimal,
 *   others decimal).  Result: the length.
 *
 * Vendor behaviour kept: isp_save_cmd_set logs an invalid subdev and then works on a NULL
 * private block (writes to address 784: a crash, not exercised by the oracle); it does not
 * bound the copy to the 64 B buffer, ignores copy_from_user's result and reads the value after
 * a key without checking that there is one (NULL dereference when the key is the last word);
 * one word is compared against all four keys.  isp_csi_cmd_set compares only the first five
 * characters and does not NUL-terminate the copy.
 *
 * Checked against the vendor functions (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_ispcmd_oracle).
 */
#ifndef TX_ISP_T32_ISPCMD_H
#define TX_ISP_T32_ISPCMD_H

#include "tx_isp_t32_isppm.h"

#define T32_FILE_PRIV		120
#define T32_SEQ_PRIV		72
#define T32_SEQ_COUNT		12
#define T32_EFAULT		(-14)
#define T32_ENOMEM		(-12)
#define T32_CMD_GFP		0x024000c0U

#define T32_CSI_LANE_STATE	704
#define T32_CSI_LOCK		732
#define T32_CSI_RESTART		856
#define T32_CSI_NLANES		860

#define T32_SAVE_VALUES		744	/* 4 words */
#define T32_SAVE_BUF		784	/* 64 B */
#define T32_SAVE_VB		944	/* 2 flag bytes */

struct t32_cmd_ops {
	void (*seq_err)(u32 seq);				/* seq_printf(seq, "...") */
	void (*log)(u32 msg);					/* isp_printf(1, msg 0 / 1 / 2 ...) */
	u32 (*kmalloc)(u32 size, u32 gfp);
	void (*kfree)(u32 p);
	u32 (*copy_from_user)(u32 dst, u32 src, u32 n);	/* bytes not copied */
	void (*lock)(u32 lock, u32 *flags);			/* __private_spin_lock_irqsave */
	void (*unlock)(u32 lock, u32 flags);
	void (*phy_start)(u32 csi, u32 lane);			/* csi_phy_start */
	void (*msleep)(u32 ms);
	void (*stop_reset)(void);				/* lib_tisp_func_stop_reset */
	u32 (*strsep)(u32 *cursor, const char *delim);
	void (*kstrtouint)(u32 s, u32 base, u32 res);
	u8 *small_buf;						/* bss d9660, 33 B */
};

static inline u32 t32_cmd_sd(u32 file)
{
	return t32_ic_r32(t32_ic_r32(file, T32_FILE_PRIV), T32_SEQ_PRIV);
}

static inline int t32_isp_csi_cmd_set(const struct t32_cmd_ops *ops, u32 file, u32 ubuf, u32 len)
{
	u32 seq = t32_ic_r32(file, T32_FILE_PRIV), sd = t32_ic_r32(seq, T32_SEQ_PRIV), csi, buf, i, flags;
	int small = len < 33, r;

	csi = t32_pm_bad(sd) ? 0 : t32_ic_r32(sd, T32_SD_PRIV);
	if (t32_pm_bad(csi)) {
		ops->seq_err(seq);
		return (int)t32_ic_r32(seq, T32_SEQ_COUNT);
	}
	if (small) {
		buf = T32_IC_A(ops->small_buf);
	} else {
		buf = ops->kmalloc(len + 1, T32_CMD_GFP);
		if (!buf)
			return T32_ENOMEM;
	}
	if (ops->copy_from_user(buf, ubuf, len)) {
		r = T32_EFAULT;
	} else {
		r = (int)len;
		if (!strncmp((const char *)T32_IC_P(buf), "reset", 5)) {
			ops->log(0);
			for (i = 0; i < t32_ic_r32(csi, T32_CSI_NLANES); i++) {
				flags = 0;
				ops->lock(csi + T32_CSI_LOCK, &flags);
				t32_ic_w32(csi, T32_CSI_LANE_STATE + 4 * i, 3);
				ops->unlock(csi + T32_CSI_LOCK, flags);
			}
			if (t32_ic_r32(csi, T32_CSI_RESTART))
				t32_ic_w32(csi, T32_CSI_RESTART, 0);
			for (i = 0; i < t32_ic_r32(csi, T32_CSI_NLANES); i++)
				ops->phy_start(csi, i);
			ops->log(1);
		}
	}
	if (!small)
		ops->kfree(buf);
	return r;
}

static const char *const t32_save_keys[4] = { "dbgmode", "items", "interval", "vinum" };	/* c311c */

static inline int t32_isp_save_cmd_set(const struct t32_cmd_ops *ops, u32 file, u32 ubuf, u32 len)
{
	u32 sd = t32_cmd_sd(file), core = 0, buf, cur, tok, val, j;

	if (t32_pm_bad(sd) || !(core = t32_ic_r32(sd, T32_SD_PRIV))) {
		ops->log(2);
		core = 0;
	}
	buf = core + T32_SAVE_BUF;
	ops->copy_from_user(buf, ubuf, len);
	if (!strncmp((const char *)T32_IC_P(buf), "get_vb", 6)) {
		*T32_IC_P(core + T32_SAVE_VB) = 1;
		*T32_IC_P(core + T32_SAVE_VB + 1) = 1;
		ops->msleep(200);
	} else if (!strncmp((const char *)T32_IC_P(buf), "isp_reg_reset", 13)) {
		ops->stop_reset();
	} else if (!strncmp((const char *)T32_IC_P(buf), "w", 1)) {
		cur = buf + 2;
		while ((tok = ops->strsep(&cur, " "))) {
			for (j = 0; j < 4; j++) {
				if (strcmp((const char *)T32_IC_P(tok), t32_save_keys[j]))
					continue;
				val = ops->strsep(&cur, " ");
				ops->kstrtouint(val, !strncmp((const char *)T32_IC_P(val), "0x", 2) ||
						     !strncmp((const char *)T32_IC_P(val), "0X", 2) ? 16 : 10,
						core + T32_SAVE_VALUES + 4 * j);
			}
		}
	}
	memset(T32_IC_P(buf), 0, 64);
	return (int)len;
}

#endif /* TX_ISP_T32_ISPCMD_H */
