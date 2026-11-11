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

/*
 * isp_vic_cmd_set (0x1c54): the VIC debug file.  The user text (up to 32 bytes into the static
 * buffer bss d9328, longer into a kmalloc'ed one) is one of
 *   "snapraw <ch> <n> <m>": snapshot n raw frames (1 when n < 2 or the text is 8 bytes) of VIC
 *     channel ch (< 5; 104 B channel blocks at +288 width / +292 height) into the reserved
 *     buffer (+872, synced for the CPU via mips_dma_map_ops +36 when present) with save mode
 *     m (>= 4 is 3; mode 3 saves with 0 and then 1) and the raw mode (data ced78);
 *     a width above 3840 or a non-zero word +872 (the synced buffer handle) only prints to the
 *     seq_file and gives 0, so the save itself only runs while +872 is 0;
 *   "mode <v>": sets the raw mode (data ced78 = v, also when v >= 3, which then gives -1);
 *   "help": prints the usage.
 * Result: the length, -1, -ENOMEM, -EFAULT, or for an invalid subdev the seq_file's word +12.
 *
 * Vendor behaviour kept: only the -EFAULT path frees a kmalloc'ed buffer (the others leak it);
 * the third snapraw number is parsed at end + 3 of the first one (end + 1 is the second); an
 * invalid mode is stored before it is rejected.
 */
#define T32_VIC_CH_SIZE		104
#define T32_VIC_CH_W		288
#define T32_VIC_CH_H		292
#define T32_VIC_RAWBUF		872
#define T32_DMA_SYNC_FOR_CPU	36

struct t32_viccmd_ops {
	void (*seq)(u32 seq, u32 msg);			/* 0 seq_printf, 1 / 2 private_seq_printf */
	void (*print)(u32 level, u32 msg, u32 a, u32 b, u32 c, u32 d);	/* isp_printf(level, msg ...) */
	u32 (*kmalloc)(u32 size, u32 gfp);
	void (*kfree)(u32 p);
	u32 (*copy_from_user)(u32 dst, u32 src, u32 n);
	u32 (*strtoull)(u32 s, u32 *end);		/* simple_strtoull(s, end, 0), low word */
	void (*save_raw)(u32 ch, u32 rawmode, u32 n, u32 mode);	/* lib_tisp_vic_save_raw */
	u32 (*dma_sync)(void);				/* mips_dma_map_ops->sync_single_for_cpu, 0 = none */
	void (*sync)(u32 fn, u32 handle, u32 size, u32 dir);
	u8 *small_buf;					/* bss d9328 */
	u32 *rawmode;					/* data ced78 */
};

static inline int t32_isp_vic_cmd_set(const struct t32_viccmd_ops *ops, u32 file, u32 ubuf, u32 len)
{
	u32 seq = t32_ic_r32(file, T32_FILE_PRIV), sd = t32_ic_r32(seq, T32_SEQ_PRIV), vic, buf;
	u32 ch, n, m, end, v, w, h, total, fn, i;
	int small = len < 33;

	vic = t32_pm_bad(sd) ? 0 : t32_ic_r32(sd, T32_SD_PRIV);
	if (t32_pm_bad(vic)) {
		ops->seq(seq, 0);
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
		if (!small)
			ops->kfree(buf);
		return T32_EFAULT;
	}
	if (!strncmp((const char *)T32_IC_P(buf), "snapraw", 7)) {
		end = 0;
		ch = ops->strtoull(buf + 8, &end);
		n = ops->strtoull(end + 1, NULL);
		m = ops->strtoull(end + 3, NULL);
		ops->print(1, 1, m, 0, 0, 0);
		if (n < 2 || len == 8)
			n = 1;
		if (ch >= 5) {
			ops->print(2, 2, 1484, ch, 0, 0);
			return -1;
		}
		v = vic + ch * T32_VIC_CH_SIZE;
		w = t32_ic_r32(v, T32_VIC_CH_W);
		h = t32_ic_r32(v, T32_VIC_CH_H);
		total = n * (w * h * 2);
		fn = ops->dma_sync();
		if (fn)
			ops->sync(fn, t32_ic_r32(vic, T32_VIC_RAWBUF), total, 2);
		ops->print(0, 3, ch, w, h, total);
		if (t32_ic_r32(v, T32_VIC_CH_W) >= 3841) {
			ops->seq(seq, 1);
			return 0;
		}
		if (t32_ic_r32(vic, T32_VIC_RAWBUF)) {
			ops->seq(seq, 2);
			return 0;
		}
		if (m >= 4)
			m = 3;
		if (m == 3) {
			ops->save_raw(ch & 0xff, *ops->rawmode & 0xff, n & 0xff, 0);
			m = 1;
		}
		ops->save_raw(ch & 0xff, *ops->rawmode & 0xff, n & 0xff, m);
		return (int)len;
	}
	if (!strncmp((const char *)T32_IC_P(buf), "mode", 4)) {
		end = 0;
		v = ops->strtoull(buf + 5, &end);
		*ops->rawmode = v;
		if (v >= 3) {
			ops->print(1, 4, 0, 0, 0, 0);
			return -1;
		}
		ops->print(0, 5, v, 0, 0, 0);
		return (int)len;
	}
	if (!strncmp((const char *)T32_IC_P(buf), "help", 4))
		for (i = 0; i < 13; i++)
			ops->print(1, 6 + i, 0, 0, 0, 0);
	return (int)len;
}

#endif /* TX_ISP_T32_ISPCMD_H */
