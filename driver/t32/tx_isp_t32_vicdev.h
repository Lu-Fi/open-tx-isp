/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 VIC raw-capture device: readable C of the vendor's
 * tisp_vic_ctrl_release, tisp_vic_mmap and tisp_vic_ctrl_ioctl.
 *
 * The raw buffer state is t32_data_d941c (40 B, struct t32_vic_raw):
 * physical / virtual address, the capture parameters and the "busy" byte
 * (+30, t32_data_d943a).  release frees the raw buffer and clears busy;
 * mmap frees the previous buffer, sets the VIC parameters again
 * (lib_tisp_vic_set_par(mode, memMode, 3, 1, fmt, 0)), fetches the new
 * buffer (lib_tisp_vic_get_par), reports it and maps it (remap_pfn_range,
 * -EFAULT and an error print on failure).
 *
 * ioctl ('v', nr < 51): 0x20007600 computes the buffer size from the
 * sensor attributes (width x height x 2, x 2 for format 2), fetches the
 * buffer and copies the 40-byte state to user space; 0x20007601 copies a
 * state from user space and runs its command (+36: 0 / 1 none, 2 free the
 * buffer, 3 start the VIC DMA, other -1).  Other 'v' commands: -EINVAL
 * without a print.
 *
 * The OS / lib calls are hooks (struct t32_vicdev_ops).  Vendor behaviour
 * kept: a failed access check of 0x20007601 clears the kernel-side state;
 * GET dereferences the sensor attributes without a NULL check (a missing
 * sensor, or an error pointer, faults in the vendor as in ours).  Checked against
 * Matt Davis' recovered C (PR #15 @ 2e2e06bc) by tests/t32_oracle
 * (t32_vicdev_oracle).
 */
#ifndef TX_ISP_T32_VICDEV_H
#define TX_ISP_T32_VICDEV_H

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
#endif
#endif

#define T32_VICDEV_PAGE_SHIFT	12
#define T32_VICDEV_EFAULT	(-14)
#define T32_VICDEV_EINVAL	(-22)
#define T32_VICDEV_IOC_TYPE	'v'
#define T32_VICDEV_IOC_NRS	51
#define T32_VICDEV_GET		0x20007600u
#define T32_VICDEV_SET		0x20007601u
#define T32_VICDEV_RAW_SIZE	40
#define T32_VICDEV_FILE_PRIV	120	/* file -> device */
#define T32_VICDEV_DEV_SD	72	/* device -> subdevice (may be an error pointer) */
#define T32_VICDEV_SD_ATTR	280	/* subdevice -> sensor attributes */
#define T32_VICDEV_ATTR_W	288
#define T32_VICDEV_ATTR_H	292

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
	u32 (*printv)(u32 level, const char *fmt, u32 a, u32 b);	/* prints without "[%s, %d]" */
	int (*access_ok)(u32 uaddr, u32 n);
	u32 (*copy_to_user)(u32 uaddr, const void *src, u32 n);	/* bytes not copied */
	u32 (*copy_from_user)(void *dst, u32 uaddr, u32 n);	/* bytes not copied */
	void (*vic_dma_start)(void);				/* lib_tisp_vic_dma_start */
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
		o->printv(2, "Failed to remap PTE for shared memory\n", 0, 0);
		return T32_VICDEV_EFAULT;
	}
	return 0;
}

static inline u32 t32_vicdev_rd32(const void *base, u32 off)
{
	u32 v;

	memcpy(&v, (const u8 *)base + off, 4);
	return v;
}

/* tisp_vic_cmd_process (inlined in tisp_vic_ctrl_ioctl): run the command of a new state */
static inline int t32_vic_cmd_process(const struct t32_vicdev_ops *o, struct t32_vic_raw *raw)
{
	switch (raw->state) {
	case 0:
	case 1:
		break;
	case 2:
		o->raw_free(NULL, NULL);
		raw->busy = 0;
		break;
	case 3:
		o->printf(1, "[%s, %d]: vic dmaout paddr is 0x%08x \n", "tisp_vic_cmd_process", 1613,
			  raw->paddr, 0, 0);
		o->vic_dma_start();
		break;
	default:
		o->printf(1, "[%s, %d]: vic cmd error !!!\n", "tisp_vic_cmd_process", 1617, 0, 0, 0);
		return -1;
	}
	raw->state = 0;
	return 0;
}

/* tisp_vic_ctrl_ioctl */
static inline int t32_vic_ctrl_ioctl(const struct t32_vicdev_ops *o, struct t32_vic_raw *raw,
				     const void *file, u32 cmd, u32 arg)
{
	const void *attr = NULL;
	u32 sd = t32_vicdev_rd32((const void *)(uintptr_t)t32_vicdev_rd32(file, T32_VICDEV_FILE_PRIV),
				 T32_VICDEV_DEV_SD);

	if (sd && sd < (u32)-4095)
		attr = (const void *)(uintptr_t)t32_vicdev_rd32((const void *)(uintptr_t)sd, T32_VICDEV_SD_ATTR);
	if (((cmd >> 8) & 0xff) != T32_VICDEV_IOC_TYPE) {
		o->printv(1, "_IOC_TYPE error !!! cmd is %d\n", cmd, 0);
		return T32_VICDEV_EINVAL;
	}
	if ((cmd & 0xff) >= T32_VICDEV_IOC_NRS) {
		o->printv(1, "_IOC_NR error !!! cmd is %d\n", cmd, 0);
		return T32_VICDEV_EINVAL;
	}
	if (cmd == T32_VICDEV_GET) {
		raw->size = t32_vicdev_rd32(attr, T32_VICDEV_ATTR_W) * t32_vicdev_rd32(attr, T32_VICDEV_ATTR_H) * 2 *
			    (raw->fmt == 2 ? 2 : 1);
		o->vic_get_par(&raw->vaddr, raw);
		o->printf(1, "[%s, %d]: vic dmaout paddr is 0x%08x \n", "tisp_vic_ctrl_ioctl", 1652, raw->paddr, 0, 0);
		if (o->access_ok(arg, T32_VICDEV_RAW_SIZE) && !o->copy_to_user(arg, raw, T32_VICDEV_RAW_SIZE))
			return 0;
	} else if (cmd == T32_VICDEV_SET) {
		if (!o->access_ok(arg, T32_VICDEV_RAW_SIZE)) {
			memset(raw, 0, T32_VICDEV_RAW_SIZE);
		} else if (!o->copy_from_user(raw, arg, T32_VICDEV_RAW_SIZE)) {
			o->printv(1, "mem mode is %d, vic cmd is %d \n", raw->mem_mode, raw->state);
			return t32_vic_cmd_process(o, raw);
		}
	} else {
		return T32_VICDEV_EINVAL;
	}
	o->printv(2, "get ioctl par copy_to_user error!!!\n", 0, 0);
	return T32_VICDEV_EFAULT;
}

#endif
