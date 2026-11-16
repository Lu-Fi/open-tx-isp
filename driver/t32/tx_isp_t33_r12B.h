/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 tuning glue of the vendor tisp core: the tuning attribute block
 * (tisp_enable_tuning / tisp_disable_tuning), the "tisp" proc entry
 * (tisp_core_tuning_init / _deinit) and the tuning character device node
 * (tisp_code_create_tuning_node / tisp_code_destroy_tuning_node).
 *
 * Kernel calls go through struct t33_tun_ops, the module objects through
 * struct t33_tun_state (pointers to the vendor's statics).
 *
 * Vendor behaviour kept: deinit does not clear the proc entry pointer, the
 * node create ignores every failure except alloc_chrdev_region, destroy does
 * not clear the class pointer.
 */
#ifndef TX_ISP_T33_R12B_H
#define TX_ISP_T33_R12B_H

#include "tx_isp_t33_blk.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

#define T33_TUN_PROC_NAME "tisp"
#define T33_TUN_PROC_ERR "[ISP][%s, %d]: proc create error !!! \n"
#define T33_TUN_NODE_ERR "[ISP][%s, %d]: alloc chrdev error !!! \n"
#define T33_TUN_MAJOR 253
#define T33_TUN_MINORBITS 20
#define T33_TATTR_SIZE 4532
#define T33_TATTR_ERR "[ %s:%d ] Failed to allocate TISP TUNING DATA!!!\n"

struct t33_tun_ops {
	void *(*k_proc_create)(const char *name, u32 mode, void *parent, const void *fops);
	void (*k_remove_proc_entry)(const char *name, void *parent);
	int (*k_printk)(const char *fmt, ...);
	int (*k_isp_printf)(int level, const char *fmt, ...);
	void *(*k_vmalloc)(u32 size);
	void (*k_vfree)(void *p);
	void *(*k_memset)(void *p, int c, u32 n);
	int (*k_register_chrdev_region)(u32 devt, u32 n, const char *name);
	int (*k_alloc_chrdev_region)(u32 *devt, u32 first, u32 n, const char *name);
	void (*k_cdev_init)(void *cdev, void *fops);
	int (*k_cdev_add)(void *cdev, u32 devt, u32 n);
	void *(*k_class_create)(void *owner, const char *name, void *key);
	void *(*k_device_create)(void *cls, void *parent, u32 devt, void *drvdata, const char *fmt);
	void (*k_cdev_del)(void *cdev);
	void (*k_device_destroy)(void *cls, u32 devt);
	void (*k_class_destroy)(void *cls);
	int (*k_unregister_chrdev_region)(u32 devt, u32 n);
	void (*k_init_waitqueue_head)(void *wq, const char *name, void *key);
};

struct t33_tun_state {
	void **proc;		/* procEntryDebug */
	void **tattr;		/* tisp_tattr */
	u32 *major;		/* major */
	void **cls;		/* cls */
	void *cdev;		/* tisp_cdev */
	void **fops_owner;	/* tisp_fops, word 0 */
	void *fops;		/* tisp_fops */
	void *show_fops;	/* tisp_show_fops */
	void **module;		/* isp_module */
	u8 *poll_value;		/* tispPollValue */
	void *dump_queue;	/* dumpQueue */
	void *key;		/* lock class key */
};

static inline void t33_tt_put32(u8 *p, u32 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
	p[2] = (u8)(v >> 16);
	p[3] = (u8)(v >> 24);
}

static inline void t33_tt_put16(u8 *p, u32 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
}

/* tisp_code_create_tuning_node */
static inline int t33_code_create_tuning_node(const struct t33_tun_ops *ops, const struct t33_tun_state *st)
{
	u32 devt = (u32)T33_TUN_MAJOR << T33_TUN_MINORBITS;
	int ret;

	*st->major = T33_TUN_MAJOR;
	if (ops->k_register_chrdev_region(devt, 1, T33_TUN_PROC_NAME)) {
		ret = ops->k_alloc_chrdev_region(&devt, 0, 1, T33_TUN_PROC_NAME);
		if (ret < 0) {
			ops->k_printk(T33_TUN_NODE_ERR, "tisp_code_create_tuning_node", 890);
			return ret;
		}
		*st->major = devt >> T33_TUN_MINORBITS;
	}
	*st->fops_owner = *st->module;
	ops->k_cdev_init(st->cdev, st->fops);
	ops->k_cdev_add(st->cdev, devt, 1);
	*st->cls = ops->k_class_create(*st->module, T33_TUN_PROC_NAME, st->key);
	ops->k_device_create(*st->cls, NULL, *st->major << T33_TUN_MINORBITS, NULL, T33_TUN_PROC_NAME);
	*st->poll_value = 0;
	ops->k_init_waitqueue_head(st->dump_queue, "&dumpQueue", st->key);
	return 0;
}

/* tisp_code_destroy_tuning_node */
static inline int t33_code_destroy_tuning_node(const struct t33_tun_ops *ops, const struct t33_tun_state *st)
{
	u32 devt = *st->major << T33_TUN_MINORBITS;

	ops->k_cdev_del(st->cdev);
	if (*st->cls) {
		ops->k_device_destroy(*st->cls, *st->major << T33_TUN_MINORBITS);
		ops->k_class_destroy(*st->cls);
	}
	return ops->k_unregister_chrdev_region(devt, 1);
}

/* tisp_core_tuning_init */
static inline int t33_core_tuning_init(const struct t33_tun_ops *ops, const struct t33_tun_state *st)
{
	*st->proc = ops->k_proc_create(T33_TUN_PROC_NAME, 0, NULL, st->show_fops);
	if (!*st->proc)
		return ops->k_printk(T33_TUN_PROC_ERR, "tisp_core_tuning_init", 1025);
	return t33_code_create_tuning_node(ops, st);
}

/* tisp_core_tuning_deinit */
static inline int t33_core_tuning_deinit(const struct t33_tun_ops *ops, const struct t33_tun_state *st)
{
	if (*st->proc)
		ops->k_remove_proc_entry(T33_TUN_PROC_NAME, NULL);
	return t33_code_destroy_tuning_node(ops, st);
}

/* tisp_enable_tuning: tisp_tattr with its defaults */
static inline int t33_enable_tuning(const struct t33_tun_ops *ops, const struct t33_tun_state *st)
{
	u8 *t;
	u32 i, k;

	t = ops->k_vmalloc(T33_TATTR_SIZE);
	*st->tattr = t;
	if (!t) {
		ops->k_isp_printf(2, T33_TATTR_ERR, "tisp_enable_tuning", 73);
		return -1;
	}
	ops->k_memset(t, 0, T33_TATTR_SIZE);
	for (i = 0; i < 20; i++)
		t[i] = 0x80;
	for (i = 0; i < 4; i++) {
		for (k = 0; k < 16; k++) {
			t33_tt_put32(t + 1060 + 128 * i + 8 * k, 0);
			t[1064 + 128 * i + 8 * k] = 0x80;
		}
	}
	for (i = 0; i < 4; i++) {
		u8 *c = t + 1572 + 72 * i;

		t33_tt_put32(c + 0, 0);
		t33_tt_put32(c + 4, 1);
		t33_tt_put32(c + 8, 1);
		c[12] = 15;
		c[13] = 15;
		t33_tt_put32(c + 16, 0);
		c[20] = 13;
		c[21] = 64;
		c[22] = 0x90;
		c[23] = 0xc0;
		t33_tt_put32(c + 24, 0);
		t33_tt_put32(c + 28, 1);
		t33_tt_put32(c + 32, 1);
		c[36] = 15;
		c[37] = 15;
		t33_tt_put32(c + 40, 0);
		t33_tt_put32(c + 44, 0);
		t33_tt_put32(c + 48, 1);
		t33_tt_put32(c + 52, 3);
		c[56] = 15;
		c[57] = 15;
		c[60] = 0;
		t33_tt_put16(c + 62, 42);
		t33_tt_put16(c + 64, 22);
		t33_tt_put16(c + 66, 200);
		t33_tt_put16(c + 68, 19);
		t33_tt_put16(c + 70, 58);
	}
	for (k = 0; k < 4; k++) {
		t33_tt_put32(t + 2164 + 460 * k, 0);
		t33_tt_put32(t + 2168 + 460 * k, 0);
		t33_tt_put32(t + 1872 + 76 * k, 0);
		t33_tt_put32(t + 1924 + 76 * k, 0);
	}
	for (i = 0; i < 4; i++) {
		u8 *b = t + 44 * i;

		for (k = 0; k < 7; k++)
			t33_tt_put32(b + 4004 + 4 * k, 0);
		ops->k_memset(b + 4032, 0, 16);
	}
	return 0;
}

/* tisp_disable_tuning */
static inline void t33_disable_tuning(const struct t33_tun_ops *ops, const struct t33_tun_state *st)
{
	if (*st->tattr) {
		ops->k_vfree(*st->tattr);
		*st->tattr = NULL;
	}
}

#endif /* TX_ISP_T33_R12B_H */
