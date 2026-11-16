/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "r12B": tuning glue (tx_isp_t33_r12B.h) with the vendor calling convention.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_r12B.h"

extern void *private_proc_create(const char *name, u32 mode, void *parent, const void *fops);
extern void private_remove_proc_entry(const char *name, void *parent);
extern int printk(const char *fmt, ...);
extern int isp_printf(int level, const char *fmt, ...);
extern void *private_vmalloc(u32 size);
extern void private_vfree(void *p);
extern int register_chrdev_region(u32 devt, u32 n, const char *name);
extern int alloc_chrdev_region(u32 *devt, u32 first, u32 n, const char *name);
extern void cdev_init(void *cdev, void *fops);
extern int cdev_add(void *cdev, u32 devt, u32 n);
extern void *__class_create(void *owner, const char *name, void *key);
extern void *device_create(void *cls, void *parent, u32 devt, void *drvdata, const char *fmt);
extern void cdev_del(void *cdev);
extern void device_destroy(void *cls, u32 devt);
extern void class_destroy(void *cls);
extern int unregister_chrdev_region(u32 devt, u32 n);
extern void __init_waitqueue_head(void *wq, const char *name, void *key);

void *bo_procEntryDebug;
void *bo_tisp_tattr;
u32 bo_major;
void *bo_cls;
u32 bo_tisp_cdev[1];
void *bo_isp_module;
void *bo_tisp_fops[27];
u8 bo_tispPollValue[4] __attribute__((aligned(4)));
u32 bo_dumpQueue[2];
static u8 bo_show_fops[8], bo_key[8];

static const struct t33_tun_ops bo_ops = {
	.k_proc_create = private_proc_create,
	.k_remove_proc_entry = private_remove_proc_entry,
	.k_printk = printk,
	.k_isp_printf = isp_printf,
	.k_vmalloc = private_vmalloc,
	.k_vfree = private_vfree,
	.k_memset = (void *(*)(void *, int, u32))memset,
	.k_register_chrdev_region = register_chrdev_region,
	.k_alloc_chrdev_region = alloc_chrdev_region,
	.k_cdev_init = cdev_init,
	.k_cdev_add = cdev_add,
	.k_class_create = __class_create,
	.k_device_create = device_create,
	.k_cdev_del = cdev_del,
	.k_device_destroy = device_destroy,
	.k_class_destroy = class_destroy,
	.k_unregister_chrdev_region = unregister_chrdev_region,
	.k_init_waitqueue_head = __init_waitqueue_head,
};

static void bo_state(struct t33_tun_state *st)
{
	st->proc = &bo_procEntryDebug;
	st->tattr = &bo_tisp_tattr;
	st->major = &bo_major;
	st->cls = &bo_cls;
	st->cdev = bo_tisp_cdev;
	st->fops_owner = &bo_tisp_fops[0];
	st->fops = bo_tisp_fops;
	st->show_fops = bo_show_fops;
	st->module = &bo_isp_module;
	st->poll_value = bo_tispPollValue;
	st->dump_queue = bo_dumpQueue;
	st->key = bo_key;
}

int bo_tisp_core_tuning_init(void)
{
	struct t33_tun_state st;

	bo_state(&st);
	return t33_core_tuning_init(&bo_ops, &st);
}

int bo_tisp_core_tuning_deinit(void)
{
	struct t33_tun_state st;

	bo_state(&st);
	return t33_core_tuning_deinit(&bo_ops, &st);
}

int bo_tisp_enable_tuning(void)
{
	struct t33_tun_state st;

	bo_state(&st);
	return t33_enable_tuning(&bo_ops, &st);
}

void bo_tisp_disable_tuning(void)
{
	struct t33_tun_state st;

	bo_state(&st);
	t33_disable_tuning(&bo_ops, &st);
}

int bo_tisp_code_create_tuning_node(void)
{
	struct t33_tun_state st;

	bo_state(&st);
	return t33_code_create_tuning_node(&bo_ops, &st);
}

int bo_tisp_code_destroy_tuning_node(void)
{
	struct t33_tun_state st;

	bo_state(&st);
	return t33_code_destroy_tuning_node(&bo_ops, &st);
}
