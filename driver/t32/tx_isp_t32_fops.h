/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 small proc / misc-device file handlers of the vendor module: the
 * camera-mode, vpuBs, ivdc and isp buffer read handlers, the camera-mode
 * proc show, and the open handlers that bind a show function with
 * single_open / private_single_open_size.
 *
 * Flow of the vendor functions camera_mode_fops_read, vpuBs_fops_read,
 * ivdcbuf_fops_read, ispbuf_fops_read, proc_read, camera_mode_fops_open,
 * tisp_show_debug_info_open, video_input_cmd_open, proc_ivdc_open.
 * Kernel calls go through struct t32_fops_ops (the kernel passes the real
 * copy_to_user / seq_printf / single_open / private_single_open_size /
 * PDE_DATA), so that the host oracle can record them.
 *
 * Vendor behaviour kept (not fixed, a change needs the user's OK):
 *  - the read handlers return the number of bytes copy_to_user did NOT copy
 *    (0 on success) instead of the byte count / -EFAULT; the 4-byte reads
 *    ignore count and *ppos;
 *  - the ivdc / isp buffer reads size the copy by strlen(buf + *ppos) but
 *    always copy from the start of the buffer.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15)
 * by tests/t32_oracle (t32_fops_oracle).
 */
#ifndef TX_ISP_T32_FOPS_H
#define TX_ISP_T32_FOPS_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/string.h>
#else
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#ifndef T32_HOST_U32
#define T32_HOST_U32
typedef uint32_t u32;
#endif
#ifndef T32_HOST_U64
#define T32_HOST_U64
typedef uint64_t u64;
#endif
#endif

/* proc show of the camera mode (vendor text) */
#define T32_FOPS_CAMERA_MODE_FMT	"Number of sensors:%d\n"
/* seq buffer sizes of the two single_open_size users */
#define T32_VIC_CMD_SEQ_SIZE		512
#define T32_IVDC_SEQ_SIZE		2048

typedef int (*t32_show_fn)(void *seq, void *v);

struct t32_fops_ops {
	/* returns the number of bytes not copied (copy_to_user incl. access_ok) */
	unsigned long (*copy_to_user)(void *to, const void *from, unsigned long n);
	int (*seq_printf)(void *seq, const char *fmt, ...);
	int (*single_open)(void *file, t32_show_fn show, void *data);
	int (*single_open_size)(void *file, t32_show_fn show, void *data, unsigned long size);
	void *(*pde_data)(void *inode);
};

/* the vendor's .bss objects behind the handlers */
struct t32_fops_state {
	u32 camera_mode;	/* camera_mode, read by camera_mode_fops_read */
	u32 vpu_bs_size;	/* read by vpuBs_fops_read */
	char *ivdc_buf;		/* ivdc_buf_len text buffer */
	char *isp_buf;		/* isp_buf_len text buffer */
	u32 sensor_number;	/* 2 / 3 / 4 sensors -> camera mode 1 / 2 / 3 */
};

/* camera_mode_fops_read: refresh the mode from the sensor count, copy 4 bytes */
static inline unsigned long t32_camera_mode_read(const struct t32_fops_ops *k, struct t32_fops_state *st,
						 void *ubuf)
{
	switch (st->sensor_number) {
	case 2: st->camera_mode = 1; break;
	case 3: st->camera_mode = 2; break;
	case 4: st->camera_mode = 3; break;
	default: break;
	}
	return k->copy_to_user(ubuf, &st->camera_mode, 4);
}

/* vpuBs_fops_read */
static inline unsigned long t32_vpubs_read(const struct t32_fops_ops *k, struct t32_fops_state *st, void *ubuf)
{
	return k->copy_to_user(ubuf, &st->vpu_bs_size, 4);
}

/* ivdcbuf_fops_read / ispbuf_fops_read: buf is the text buffer, ppos a 64-bit offset */
static inline unsigned long t32_textbuf_read(const struct t32_fops_ops *k, const char *buf, void *ubuf,
					     unsigned long count, u64 *ppos)
{
	unsigned long left;

	if (strlen(buf + (u32)*ppos) < count)
		count = strlen(buf + (u32)*ppos);
	left = k->copy_to_user(ubuf, buf, count);
	if (left)
		return left;
	*ppos += count;
	return count;
}

/* proc_read: the camera-mode proc show */
static inline int t32_proc_read(const struct t32_fops_ops *k, const struct t32_fops_state *st, void *seq)
{
	k->seq_printf(seq, T32_FOPS_CAMERA_MODE_FMT, st->camera_mode);
	return 0;
}

/* camera_mode_fops_open / tisp_show_debug_info_open: single_open(file, show, NULL) */
static inline int t32_single_show_open(const struct t32_fops_ops *k, void *file, t32_show_fn show)
{
	return k->single_open(file, show, NULL);
}

/* video_input_cmd_open (512) / proc_ivdc_open (2048): the PDE data as the show argument */
static inline int t32_pde_show_open(const struct t32_fops_ops *k, void *inode, void *file, t32_show_fn show,
				    unsigned long size)
{
	return k->single_open_size(file, show, k->pde_data(inode), size);
}

#endif /* TX_ISP_T32_FOPS_H */
