/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP library: write a buffer to a file.
 *
 *   t32_lib_save_file   lib_tisp_save_file 0x1d0b8 (buf, len, path): filp_open(path, O_WRONLY | O_CREAT |
 *                       O_TRUNC (0x301), 0766), switch to the kernel address limit (get_fs / set_fs(0)),
 *                       vfs_write(file, buf, len, &file->f_pos (file + 48)), restore the old limit; returns
 *                       filp_close(file, NULL).  The vendor does not check the filp_open result (an error
 *                       pointer is passed on to vfs_write; vendor behaviour, kept).
 *
 * OS calls are hooks.  Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_libfile_oracle).
 */
#ifndef TX_ISP_T32_LIBFILE_H
#define TX_ISP_T32_LIBFILE_H

#ifndef __KERNEL__
#include <stdint.h>
#ifndef T32_HOST_U32
#define T32_HOST_U32
typedef uint32_t u32;
#endif
#endif

#define T32_SAVE_FLAGS		0x301U	/* O_WRONLY | O_CREAT | O_TRUNC (MIPS) */
#define T32_SAVE_MODE		0766U
#define T32_FILE_POS		48U	/* struct file: f_pos */
#define T32_KERNEL_DS		0U

struct t32_file_hooks {
	void *ctx;
	void *(*filp_open)(void *ctx, const char *path, u32 flags, u32 mode);	/* private_filp_open */
	void (*get_fs)(void *ctx, u32 *old);					/* private_get_fs */
	void (*set_fs)(void *ctx, u32 fs);					/* private_set_fs */
	u32 (*vfs_write)(void *ctx, void *file, const void *buf, u32 len, void *pos);	/* private_vfs_write */
	u32 (*filp_close)(void *ctx, void *file, void *id);			/* private_filp_close */
};

static inline u32 t32_lib_save_file(const struct t32_file_hooks *h, const void *buf, u32 len, const char *path)
{
	void *file = h->filp_open(h->ctx, path, T32_SAVE_FLAGS, T32_SAVE_MODE);
	u32 old;

	h->get_fs(h->ctx, &old);
	h->set_fs(h->ctx, T32_KERNEL_DS);
	h->vfs_write(h->ctx, file, buf, len, (unsigned char *)file + T32_FILE_POS);
	h->set_fs(h->ctx, old);
	return h->filp_close(h->ctx, file, NULL);
}

#endif
