/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 tuning character device glue: readable C of the vendor's
 * tisp_code_create_tuning_node, tisp_code_destroy_tuning_node,
 * tisp_code_tuning_open, tisp_code_tuning_release, tisp_code_tuning_mmap and
 * tisp_code_tuning_poll_init and tisp_code_tuning_ioctl.
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

/* ---- tisp_code_tuning_ioctl (vendor 0x1f10c), oracle t32_ctio_oracle ---- */

#define T32_CTIO_MAGIC		't'
#define T32_CTIO_NR_LIMIT	51		/* _IOC_NR checked against this first */
#define T32_CTIO_CMD0		0x20007400u	/* _IO('t', 0): the nine commands below */
#define T32_CTIO_EINVAL		(-22)
/* the 20508 B exchange buffer (t32_data_dada8) */
#define T32_CTIO_ID		0		/* u32 block id */
#define T32_CTIO_LEN		4		/* u32 length (out) */
#define T32_CTIO_SUB		8		/* u32 / u16 sub id */
#define T32_CTIO_CH		26		/* u8 channel */
#define T32_CTIO_DATA		27		/* parameter bytes */
#define T32_CTIO_GET_IDS	45
#define T32_CTIO_SET_IDS	44
#define T32_CTIO_AE_INFO_LEN	164
#define T32_CTIO_VER		"V-20241122"		/* rodata 0xc7ca8 */
#define T32_CTIO_ISP_VER	"V-ISP-20241122"	/* rodata 0xc7cb4 */

enum t32_ctio_cmd {
	T32_CTIO_GET, T32_CTIO_SET, T32_CTIO_GET_VER, T32_CTIO_GET_ISP_VER, T32_CTIO_GET_AE_INFO,
	T32_CTIO_SET_AE_INFO, T32_CTIO_SET_AE_MINMAX, T32_CTIO_GET_AWB_INFO, T32_CTIO_SET_AWB_INFO,
	T32_CTIO_CMDS,
};

/* block ids with their own argument forms (the others: (ch, data, len) / (ch, data)) */
enum t32_ctio_id {
	T32_CTIO_LSC = 3, T32_CTIO_MDNS = 14, T32_CTIO_TSTP = 25, T32_CTIO_REGMAP = 28,
	T32_CTIO_FIXED4 = 36,	/* get: length 4 only; set: debug print */
	T32_CTIO_COMN = 37,	/* set: nothing */
	T32_CTIO_NONE = 38,	/* get / set: nothing */
	T32_CTIO_API = 42,	/* u16 sub id */
};

enum t32_ctio_info { T32_CTIO_I_SET_AE, T32_CTIO_I_SET_AE_MINMAX, T32_CTIO_I_GET_AWB, T32_CTIO_I_SET_AWB };

struct t32_ctio_ops {
	u32 (*addr_limit)(void);				/* current_thread_info()->addr_limit */
	u32 (*copy_from_user)(void *dst, u32 usr, u32 n);	/* __copy_user: bytes not copied */
	u32 (*copy_to_user)(u32 usr, const void *src, u32 n);
	/* tisp_<block>_param_array_get(ch[, sub], data, len); tstp without ch (0 here),
	 * reg_map with the buffer start as data */
	void (*get)(u32 id, u32 ch, u32 sub, u8 *data, u8 *len);
	/* tisp_<block>_param_array_set(ch[, sub], data); tstp without ch, reg_map with the buffer start */
	void (*set)(u32 id, u32 ch, u32 sub, u8 *data);
	void (*info)(enum t32_ctio_info what, u8 *buf);	/* tisp_set_ae_info / _set_ae_minmax / _get_awb_info / _set_awb_info */
};

/* the ids without a block (debug print on get / set) */
static inline int t32_ctio_unknown(u32 id)
{
	return id == 2 || id == 8 || id == 27 || (id >= 29 && id <= 35);
}

static inline void t32_ctio_put32(u8 *p, u32 v) { memcpy(p, &v, 4); }
static inline u32 t32_ctio_get32(const u8 *p) { u32 v; memcpy(&v, p, 4); return v; }
static inline u32 t32_ctio_get16(const u8 *p) { return p[0] | p[1] << 8; }	/* little endian */

static inline int t32_ctio_bad(const struct t32_ctio_ops *o, u32 arg)
{
	return ((arg + T32_CTUNE_BUF_SIZE) | arg) & o->addr_limit();
}

/* copy the user buffer in; a refused address clears the kernel buffer */
static inline int t32_ctio_in(struct t32_ctune *t, const struct t32_ctio_ops *o, u32 arg)
{
	if (t32_ctio_bad(o, arg)) {
		memset(t->buf, 0, T32_CTUNE_BUF_SIZE);
		return T32_CTUNE_EFAULT;
	}
	if (o->copy_from_user(t->buf, arg, T32_CTUNE_BUF_SIZE))
		return T32_CTUNE_EFAULT;
	return 0;
}

static inline int t32_ctio_out(struct t32_ctune *t, const struct t32_ctio_ops *o, u32 arg)
{
	if (t32_ctio_bad(o, arg) || o->copy_to_user(arg, t->buf, T32_CTUNE_BUF_SIZE))
		return T32_CTUNE_EFAULT;
	return 0;
}

static inline void t32_ctio_get_par(const struct t32_ctio_ops *o, u8 *b)
{
	u32 id = t32_ctio_get32(b + T32_CTIO_ID), ch = b[T32_CTIO_CH];
	u8 *d = b + T32_CTIO_DATA, *len = b + T32_CTIO_LEN;

	if (id - 1 >= T32_CTIO_GET_IDS || t32_ctio_unknown(id))
		return;					/* debug print */
	switch (id) {
	case T32_CTIO_MDNS:
		o->get(id, ch, t32_ctio_get32(b + T32_CTIO_SUB), d, len);
		break;
	case T32_CTIO_TSTP:
		o->get(id, 0, 0, d, len);
		break;
	case T32_CTIO_REGMAP:
		o->get(id, ch, t32_ctio_get32(b + T32_CTIO_SUB), b, len);
		break;
	case T32_CTIO_API:
		o->get(id, ch, t32_ctio_get16(b + T32_CTIO_SUB), d, len);
		break;
	case T32_CTIO_FIXED4:
		t32_ctio_put32(len, 4);
		break;
	case T32_CTIO_NONE:
		break;
	default:
		o->get(id, ch, 0, d, len);
		break;
	}
}

static inline void t32_ctio_set_par(const struct t32_ctio_ops *o, u8 *b)
{
	u32 id = t32_ctio_get32(b + T32_CTIO_ID), ch = b[T32_CTIO_CH];
	u8 *d = b + T32_CTIO_DATA;

	if (id - 1 >= T32_CTIO_SET_IDS || t32_ctio_unknown(id))
		return;					/* debug print */
	switch (id) {
	case T32_CTIO_LSC: case T32_CTIO_MDNS:
		o->set(id, ch, t32_ctio_get32(b + T32_CTIO_SUB), d);
		break;
	case T32_CTIO_TSTP:
		o->set(id, 0, 0, d);
		break;
	case T32_CTIO_REGMAP:
		o->set(id, ch, 0, b);
		break;
	case T32_CTIO_API:
		o->set(id, ch, t32_ctio_get16(b + T32_CTIO_SUB), d);
		break;
	case T32_CTIO_FIXED4: case T32_CTIO_COMN: case T32_CTIO_NONE:
		break;					/* 36: debug print */
	default:
		o->set(id, ch, 0, d);
		break;
	}
}

/*
 * tisp_code_tuning_ioctl: the tuning device's commands _IO('t', 0 .. 8) on the
 * 20508 B buffer at arg (get / set a block's parameters, version strings, AE / AWB
 * info).  -22 for a foreign command, -14 for a refused or short user copy, else 0;
 * the block functions' results are ignored.  The vendor's debug prints are left out.
 */
static inline int t32_code_tuning_ioctl(struct t32_ctune *t, const struct t32_ctio_ops *o, u32 cmd, u32 arg)
{
	static const char ver[] = T32_CTIO_VER, isp_ver[] = T32_CTIO_ISP_VER;
	u8 *b = t->buf;
	u32 nr = cmd - T32_CTIO_CMD0;
	int r;

	if (((cmd >> 8) & 0xff) != T32_CTIO_MAGIC || (cmd & 0xff) >= T32_CTIO_NR_LIMIT)
		return T32_CTIO_EINVAL;			/* debug print */
	if (nr >= T32_CTIO_CMDS)
		return T32_CTIO_EINVAL;
	switch (nr) {
	case T32_CTIO_GET:
		r = t32_ctio_in(t, o, arg);
		if (r)
			return r;
		t->mem_sel = b[T32_CTIO_CH];
		t32_ctio_get_par(o, b);
		break;
	case T32_CTIO_SET:
		r = t32_ctio_in(t, o, arg);
		if (r)
			return r;
		t->mem_sel = b[T32_CTIO_CH];
		t32_ctio_set_par(o, b);
		return 0;
	case T32_CTIO_GET_VER: case T32_CTIO_GET_ISP_VER:
		r = t32_ctio_in(t, o, arg);
		if (r)
			return r;
		if (nr == T32_CTIO_GET_VER) {
			t32_ctio_put32(b + T32_CTIO_LEN, sizeof(ver));
			memcpy(b + T32_CTIO_DATA, ver, sizeof(ver));
		} else {
			t32_ctio_put32(b + T32_CTIO_LEN, sizeof(isp_ver));
			memcpy(b + T32_CTIO_DATA, isp_ver, sizeof(isp_ver));
		}
		break;
	case T32_CTIO_GET_AE_INFO:
		t32_ctio_put32(b + T32_CTIO_LEN, T32_CTIO_AE_INFO_LEN);	/* nothing filled in */
		break;
	case T32_CTIO_GET_AWB_INFO:
		o->info(T32_CTIO_I_GET_AWB, b);
		break;
	default:					/* the three info setters */
		r = t32_ctio_in(t, o, arg);
		if (r)
			return r;
		o->info(nr == T32_CTIO_SET_AE_INFO ? T32_CTIO_I_SET_AE :
			nr == T32_CTIO_SET_AE_MINMAX ? T32_CTIO_I_SET_AE_MINMAX : T32_CTIO_I_SET_AWB, b);
		return 0;
	}
	return t32_ctio_out(t, o, arg);
}

#endif
