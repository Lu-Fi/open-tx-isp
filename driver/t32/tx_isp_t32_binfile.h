/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 IQ bin switch of the vendor tisp core: tisp_read_bin_file and
 * tisp_core_switch_bin (the ioctl path that changes a channel's bin).
 *
 * tisp_read_bin_file opens the file, reads it completely into a vmalloc
 * buffer under KERNEL_DS and frees the buffer again: it only proves that the
 * file is there and readable (the bin itself is loaded later).
 * tisp_core_switch_bin (attr: +0 mode word, +4 bin index, +5 128-byte path):
 * with mode 0 the path is also stored in the slot after the first loaded one
 * (channel state +4 + 128 * (i + 1), i = the first index with flag +126 + i
 * = 1); after a good read the path goes to the channel's common block and
 * the index's loaded flag is set.
 * File / fs calls go through struct t32_binfile_ops.
 *
 * Vendor behaviour kept (not fixed, a change needs the user's OK): the read
 * result is not checked, set_fs(old) runs twice on success, and the size is
 * the low word of i_size (files of 4 GiB and more wrap).
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15)
 * by tests/t32_oracle (t32_binfile_oracle).
 */
#ifndef TX_ISP_T32_BINFILE_H
#define TX_ISP_T32_BINFILE_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/string.h>
#else
#include <stdint.h>
#include <string.h>
#ifndef T32_HOST_U32
#define T32_HOST_U32
typedef uint32_t u32;
#endif
#ifndef T32_HOST_U8
#define T32_HOST_U8
typedef uint8_t u8;
typedef uint16_t u16;
#endif
#ifndef T32_HOST_U64
#define T32_HOST_U64
typedef uint64_t u64;
#endif
#ifndef T32_HOST_S64
#define T32_HOST_S64
typedef int64_t s64;
typedef int32_t s32;
#endif
#endif

struct t32_binfile_ops {
	void *(*filp_open)(const char *path, int flags, int mode);	/* private_filp_open */
	void (*filp_close)(void *f, void *id);
	u64 (*file_size)(void *f);		/* i_size of the file's inode */
	void *(*file_pos)(void *f);		/* &f->f_pos */
	u32 (*get_fs)(void);
	void (*set_fs)(u32 seg);
	void *(*vmalloc)(u32 size);
	void (*vfree)(void *p);
	int (*vfs_read)(void *f, void *buf, u32 n, void *pos);
	int (*isp_printf)(int level, const char *fmt, ...);
};

/* tisp_read_bin_file: 0 = the file could be opened and a buffer of its size allocated */
static inline int t32_read_bin_file(const struct t32_binfile_ops *k, const char *path)
{
	void *f = k->filp_open(path, 0, 0), *buf;
	u64 size;
	u32 old;

	if ((u32)(uintptr_t)f >= (u32)-4095) {
		k->isp_printf(2, "[ISP][%s, %d]: ISP: open %s file for isp calibrate read failed \n",
			      "tisp_read_bin_file", 1229, path);
		return -1;
	}
	size = k->file_size(f);
	old = k->get_fs();
	k->set_fs(0);
	buf = k->vmalloc((u32)size);
	if (!buf) {
		k->isp_printf(2, "[[ %s:%d ]]: Failed to alloc %lld KB buffer!\n", "tisp_read_bin_file", 1245,
			      (long long)((s64)size >> 10));
		k->filp_close(f, NULL);
		k->set_fs(old);
		return -1;
	}
	k->vfs_read(f, buf, (u32)size, k->file_pos(f));
	k->filp_close(f, NULL);
	k->set_fs(old);
	k->vfree(buf);
	k->set_fs(old);
	return 0;
}

/* tisp_core_switch_bin (chn: channel state dc284[ch], 904 B; blk: common block da850[ch], 336 B) */
static inline int t32_core_switch_bin(const struct t32_binfile_ops *k, u8 *chn, u8 *blk, const u8 *attr)
{
	const char *path = (const char *)attr + 5;
	int i, ret;

	if (*(const u32 *)attr == 0)
		for (i = 0; i < 6; i++)
			if (chn[126 + i] == 1) {
				memcpy(chn + 4 + 128 * (i + 1), path, 128);
				break;
			}
	ret = t32_read_bin_file(k, path);
	if (ret) {
		k->isp_printf(2, "[[ %s:%d ]] no bin file on the system!!!\n", "tisp_core_switch_bin", 1290);
		return ret;
	}
	memcpy(blk, path, 128);
	chn[126 + attr[4]] = 1;
	return 0;
}

#endif /* TX_ISP_T32_BINFILE_H */
