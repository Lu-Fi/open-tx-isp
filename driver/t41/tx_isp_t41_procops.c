// SPDX-License-Identifier: GPL-2.0
/*
 * Non-OEM: /proc entries on kernel 5.6 and later.
 *
 * The recovered driver creates its /proc nodes with struct file_operations
 * (the vendor 4.4 ABI). Since 5.6, proc_create_data() takes a struct
 * proc_ops, whose members lie elsewhere. The old structure was read as a
 * proc_ops (the build runs with -w -fpermissive, so nothing warned), and
 * reading a node such as /proc/jz/isp/isp-m0 called seq_read() without
 * the single_open() that sets file->private_data: an oops in mutex_lock
 * (NULL + 0x28). Translate each file_operations once into a proc_ops.
 * On older kernels the file_operations go through unchanged.
 */
#include <linux/module.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0)
#define T41_PROC_OPS_MAX 64

static struct {
	const struct file_operations *fops;
	struct proc_ops ops;
} t41_proc_ops_tab[T41_PROC_OPS_MAX];
static DEFINE_MUTEX(t41_proc_ops_lock);

const void *t41_proc_ops(const struct file_operations *f)
{
	const void *ret = NULL;
	int i;

	if (!f)
		return NULL;
	mutex_lock(&t41_proc_ops_lock);
	for (i = 0; i < T41_PROC_OPS_MAX; i++) {
		struct proc_ops *p = &t41_proc_ops_tab[i].ops;

		if (t41_proc_ops_tab[i].fops == f) {
			ret = p;
			break;
		}
		if (t41_proc_ops_tab[i].fops)
			continue;
		t41_proc_ops_tab[i].fops = f;
		p->proc_open = f->open;
		p->proc_read = f->read;
		p->proc_read_iter = f->read_iter;
		p->proc_write = f->write;
		p->proc_lseek = f->llseek;
		p->proc_release = f->release;
		p->proc_poll = f->poll;
		p->proc_ioctl = f->unlocked_ioctl;
#ifdef CONFIG_COMPAT
		p->proc_compat_ioctl = f->compat_ioctl;
#endif
		p->proc_mmap = f->mmap;
		ret = p;
		break;
	}
	mutex_unlock(&t41_proc_ops_lock);
	if (!ret)
		pr_err("t41: proc_ops table full, /proc node not created\n");
	return ret;
}
#else
const void *t41_proc_ops(const struct file_operations *f)
{
	return f;
}
#endif
