/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 VIC raw-capture device: readable C of the vendor's
 * tisp_vic_ctrl_release and tisp_vic_mmap.
 *
 * The raw buffer state is t32_data_d941c (40 B, struct t32_vic_raw):
 * physical / virtual address, the capture parameters and the "busy" byte
 * (+30, t32_data_d943a).  release frees the raw buffer and clears busy;
 * mmap frees the previous buffer, sets the VIC parameters again
 * (lib_tisp_vic_set_par(mode, memMode, 3, 1, fmt, 0)), fetches the new
 * buffer (lib_tisp_vic_get_par), reports it and maps it (remap_pfn_range,
 * -EFAULT and an error print on failure).
 *
 * The OS / lib calls are hooks (struct t32_vicdev_ops).  Checked against
 * Matt Davis' recovered C (PR #15 @ 2e2e06bc) by tests/t32_oracle
 * (t32_vicdev_oracle).
 */
#ifndef TX_ISP_T32_VICDEV_H
#define TX_ISP_T32_VICDEV_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#ifndef T32_HOST_U32
#define T32_HOST_U32
typedef uint32_t u32;
#endif
#ifndef T32_HOST_U8
#define T32_HOST_U8
typedef uint8_t u8;
#endif
#endif

#define T32_VICDEV_PAGE_SHIFT	12
#define T32_VICDEV_EFAULT	(-14)

struct t32_vic_raw {		/* t32_data_d941c, 40 B */
	u32 paddr;		/* +0 */
	u32 vaddr;		/* +4, t32_data_d9420 */
	u32 size;		/* +8 */
	u8 rsvd0[12];
	u8 mode;		/* +24 */
	u8 rsvd1;
	u8 mem_mode;		/* +26 */
	u8 mem_mode_used;	/* +27 */
	u8 rsvd2[2];
	u8 busy;		/* +30, t32_data_d943a */
	u8 fmt;			/* +31 */
	u32 rsvd3;
	u32 state;		/* +36 */
};

struct t32_vicdev_vma {		/* the vm_area_struct words used */
	u32 vm_start;		/* +0 */
	u32 vm_end;		/* +4 */
	u32 rsvd[7];
	u32 vm_page_prot;	/* +36 */
};

struct t32_vicdev_ops {
	void (*raw_free)(void *inode, void *file);		/* lib_tisp_raw_free */
	u32 (*single_release)(void *inode, void *file);		/* private_single_release */
	void (*vic_set_par)(u32 mode, u32 mem_mode, u32 a2, u32 a3, u32 fmt, u32 a5);
	void (*vic_get_par)(u32 *vaddr, struct t32_vic_raw *raw);
	u32 (*printf)(u32 level, const char *fmt, const char *fn, u32 line, u32 paddr, u32 vaddr, u32 mem_mode);
	u32 (*remap_pfn_range)(struct t32_vicdev_vma *vma, u32 addr, u32 pfn, u32 size, u32 prot);
};

/* tisp_vic_ctrl_release */
static inline u32 t32_vic_ctrl_release(const struct t32_vicdev_ops *o, struct t32_vic_raw *raw,
				       void *inode, void *file)
{
	o->raw_free(inode, file);
	raw->busy = 0;
	return o->single_release(inode, file);
}

/* tisp_vic_mmap */
static inline int t32_vic_mmap(const struct t32_vicdev_ops *o, struct t32_vic_raw *raw,
			       void *file, struct t32_vicdev_vma *vma)
{
	u32 size = vma->vm_end - vma->vm_start;

	o->raw_free(file, vma);
	raw->busy = 0;
	o->vic_set_par(raw->mode, raw->mem_mode, 3, 1, raw->fmt, 0);
	raw->mem_mode_used = raw->mem_mode;
	o->vic_get_par(&raw->vaddr, raw);
	o->printf(1, "[%s, %d] vic paddr 0x%x, vaddr 0x%x, memMode is %d\n", "tisp_vic_mmap", 1696,
		  raw->paddr, raw->vaddr, raw->mem_mode);
	if (o->remap_pfn_range(vma, vma->vm_start, raw->paddr >> T32_VICDEV_PAGE_SHIFT, size, vma->vm_page_prot)) {
		o->printf(2, "Failed to remap PTE for shared memory\n", NULL, 0, 0, 0, 0);
		return T32_VICDEV_EFAULT;
	}
	return 0;
}

#endif
