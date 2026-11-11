/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 tuning character device glue: readable C of the vendor's
 * tisp_code_create_tuning_node, tisp_code_destroy_tuning_node,
 * tisp_code_tuning_open, tisp_code_tuning_release, tisp_code_tuning_mmap and
 * tisp_code_tuning_poll_init.
 *
 * The node is "tisp" (one minor); a non-zero module parameter
 * (t32_data_d9008) fixes the major to 253, else it is allocated.  open
 * allocates the 20508-byte tuning message buffer, release frees it, mmap
 * maps the physical memory of the selected record of t32_data_da850
 * (336 B each, +300 kseg0 address), poll_init sets up the "&dumpQueue" wait
 * queue.  All OS calls are hooks (struct t32_ctune_ops); the OS objects
 * (cdev, file operations, class, wait queue, lock key) are passed through.
 * Checked against Matt Davis' recovered C (PR #15 @ 2e2e06bc) by
 * tests/t32_oracle (t32_ctune_oracle).
 *
 * Vendor behaviour kept: no error checks in create (register / alloc /
 * cdev_add / class / device results are ignored); release does not clear
 * the buffer pointer.  Vendor UB refused by ours: open runs memset on a
 * failed (NULL) allocation; ours skips the memset (the oracle does not run
 * the vendor on that case).
 */
#ifndef TX_ISP_T32_CTUNE_H
#define TX_ISP_T32_CTUNE_H

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

#define T32_CTUNE_NAME		"tisp"
#define T32_CTUNE_FIXED_MAJOR	253
#define T32_CTUNE_MINORBITS	20
#define T32_CTUNE_BUF_SIZE	20508
#define T32_CTUNE_GFP		0x24000c0u
#define T32_CTUNE_MEM_STRIDE	336
#define T32_CTUNE_MEM_ADDR	300
#define T32_CTUNE_KSEG0		0x80000000u
#define T32_CTUNE_PAGE_SHIFT	12
#define T32_CTUNE_EFAULT	(-14)

struct t32_ctune_vma {		/* the vm_area_struct words used */
	u32 vm_start;		/* +0 */
	u32 vm_end;		/* +4 */
	u32 rsvd[7];
	u32 vm_page_prot;	/* +36 */
};

struct t32_ctune_ops {
	u32 (*register_chrdev_region)(u32 devt, u32 count, const char *name);
	u32 (*alloc_chrdev_region)(u32 *devt, u32 first, u32 count, const char *name);
	void (*cdev_init)(void *cdev, void *fops);
	u32 (*cdev_add)(void *cdev, u32 devt, u32 count);
	void *(*class_create)(u32 owner, const char *name, void *key);
	void *(*device_create)(void *cls, void *parent, u32 devt, void *drvdata, const char *fmt);
	void (*cdev_del)(void *cdev);
	void (*device_destroy)(void *cls, u32 devt);
	void (*class_destroy)(void *cls);
	u32 (*unregister_chrdev_region)(u32 devt, u32 count);
	void *(*kmalloc)(u32 size, u32 flags);
	void (*kfree)(void *p);
	u32 (*printf)(u32 level, const char *fmt, const char *s, u32 d);
	u32 (*remap_pfn_range)(struct t32_ctune_vma *vma, u32 addr, u32 pfn, u32 size, u32 prot);
	void (*init_waitqueue_head)(void *wq, const char *name, void *key);
};

struct t32_ctune {
	const struct t32_ctune_ops *ops;
	u32 major_param;	/* t32_data_d9008 */
	u32 major;		/* t32_data_dadec */
	void *cls;		/* t32_data_dadac */
	void *cdev;		/* t32_data_dadb0 */
	u32 *fops;		/* t32_data_d087c: word 0 = owner */
	u32 owner;		/* t32_data_ce22c (THIS_MODULE) */
	void *lock_key;		/* t32_small_bss */
	void *buf;		/* t32_data_dada8 */
	u8 mem_sel;		/* t32_data_dada4 */
	const u8 *mem;		/* t32_data_da850 */
	u8 poll_ready;		/* t32_data_dad90 */
	void *dump_wq;		/* t32_data_dad94 */
};

/* tisp_code_create_tuning_node */
static inline u32 t32_code_create_tuning_node(struct t32_ctune *t)
{
	const struct t32_ctune_ops *o = t->ops;
	u32 devt;

	if (t->major_param)
		t->major = T32_CTUNE_FIXED_MAJOR;
	if (t->major) {
		devt = t->major << T32_CTUNE_MINORBITS;
		o->register_chrdev_region(devt, 1, T32_CTUNE_NAME);
	} else {
		o->alloc_chrdev_region(&devt, 0, 1, T32_CTUNE_NAME);
		t->major = devt >> T32_CTUNE_MINORBITS;
	}
	t->fops[0] = t->owner;
	o->cdev_init(t->cdev, t->fops);
	o->cdev_add(t->cdev, devt, 1);
	t->cls = o->class_create(t->owner, T32_CTUNE_NAME, t->lock_key);
	o->device_create(t->cls, NULL, t->major << T32_CTUNE_MINORBITS, NULL, T32_CTUNE_NAME);
	return 0;
}

/* tisp_code_destroy_tuning_node */
static inline u32 t32_code_destroy_tuning_node(struct t32_ctune *t)
{
	const struct t32_ctune_ops *o = t->ops;
	u32 devt = t->major << T32_CTUNE_MINORBITS;

	if (t->cls) {
		o->cdev_del(t->cdev);
		o->device_destroy(t->cls, t->major << T32_CTUNE_MINORBITS);
		o->class_destroy(t->cls);
		t->cls = NULL;
	}
	return o->unregister_chrdev_region(devt, 1);
}

/* tisp_code_tuning_open */
static inline u32 t32_code_tuning_open(struct t32_ctune *t)
{
	t->buf = t->ops->kmalloc(T32_CTUNE_BUF_SIZE, T32_CTUNE_GFP);
	if (t->buf)
		memset(t->buf, 0, T32_CTUNE_BUF_SIZE);
	return 0;
}

/* tisp_code_tuning_release */
static inline u32 t32_code_tuning_release(struct t32_ctune *t)
{
	t->ops->printf(0, "func is %s, lined is %d\n", "tisp_code_tuning_release", 544);
	t->ops->kfree(t->buf);
	return 0;
}

/* tisp_code_tuning_mmap: map the selected shared memory record */
static inline int t32_code_tuning_mmap(struct t32_ctune *t, struct t32_ctune_vma *vma)
{
	u32 addr;

	memcpy(&addr, t->mem + T32_CTUNE_MEM_STRIDE * t->mem_sel + T32_CTUNE_MEM_ADDR, 4);
	if (t->ops->remap_pfn_range(vma, vma->vm_start, (addr + T32_CTUNE_KSEG0) >> T32_CTUNE_PAGE_SHIFT,
				    vma->vm_end - vma->vm_start, vma->vm_page_prot)) {
		t->ops->printf(2, "Failed to remap PTE for shared memory\n", NULL, 0);
		return T32_CTUNE_EFAULT;
	}
	return 0;
}

/* tisp_code_tuning_poll_init */
static inline void t32_code_tuning_poll_init(struct t32_ctune *t)
{
	t->poll_ready = 0;
	t->ops->init_waitqueue_head(t->dump_wq, "&dumpQueue", t->lock_key);
}

#endif
