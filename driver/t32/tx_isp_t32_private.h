/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32: the vendor's private_* kernel wrappers (tx-isp-t32.o .text 0x0..0xd00,
 * 0xcd70, 0xd014, 0x17750) as readable C.  The vendor exports them to the
 * sensor modules and calls them from its own code; each one is a thin shim
 * over one kernel call.  The OS calls are kept as the plain kernel API, so in
 * the kernel build they are the real functions and in the host oracles
 * (tests/t32_oracle t32_priv_oracle, t32_privx_oracle) they are recording
 * hooks of tests/t32_oracle/t32o_kern.h, which expands the kernel inlines the
 * way the vendor object shows them.
 *
 * Checked by tests/t32_oracle (t32_priv_oracle: against Matt Davis' recovered
 * wrappers; t32_privx_oracle: against his mechanical C and, where his C is
 * marked diff_mismatch and differs from the vendor object, against the
 * vendor instructions).  Vendor behaviour kept on purpose:
 *  - t32_private_vmalloc: below one page it is kmalloc(GFP_KERNEL), and
 *    t32_private_vfree picks vfree / kfree by the address (0x838, 0x864);
 *  - t32_private_copy_from_user / _to_user: a range that fails access_ok is
 *    copied with memcpy and reported as fully copied (0x8b8, 0x910), the
 *    vendor relies on it for kernel buffers;
 *  - t32_private_kthread_run: the name is handed over as the format (0xa30);
 *  - t32_private_seq_printf / t32_private_misc_deregister: the vendor returns
 *    whatever the void kernel call left in v0 (undefined); ours returns 0.
 *  - t32_private_spin_lock_init is empty on the vendor's UP kernel (0x518),
 *    here it is spin_lock_init(), which is the same there.
 */
#ifndef TX_ISP_T32_PRIVATE_H
#define TX_ISP_T32_PRIVATE_H

#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/miscdevice.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/dma-mapping.h>
#include <linux/time.h>
#include <linux/ktime.h>
#include <linux/hrtimer.h>
#include <linux/workqueue.h>
#include <linux/kmod.h>
#include <linux/capability.h>
int isp_printf(unsigned int level, const char *fmt, ...);	/* tx_isp_t32_funcs.c */
extern int jzgpio_set_func(int port, int func, unsigned long pins);
extern struct proc_dir_entry *jz_proc_mkdir(char *s);
extern unsigned long ispmem_base;
extern unsigned long ispmem_size;
#endif

/* ---- clocks (0x94..0x134, 0x248) ---- */
static inline struct clk *t32_private_clk_get(struct device *dev, const char *id)
{
	return clk_get(dev, id);
}

static inline struct clk *t32_private_devm_clk_get(struct device *dev, const char *id)
{
	return devm_clk_get(dev, id);
}

static inline int t32_private_clk_enable(struct clk *clk)
{
	return clk_enable(clk);
}

static inline void t32_private_clk_disable(struct clk *clk)
{
	clk_disable(clk);
}

static inline void t32_private_clk_disable_unprepare(struct clk *clk)
{
	clk_disable(clk);
	clk_unprepare(clk);
}

static inline unsigned long t32_private_clk_get_rate(struct clk *clk)
{
	return clk_get_rate(clk);
}

static inline void t32_private_clk_put(struct clk *clk)
{
	clk_put(clk);
}

static inline void t32_private_devm_clk_put(struct device *dev, struct clk *clk)
{
	devm_clk_put(dev, clk);
}

static inline int t32_private_clk_set_rate(struct clk *clk, unsigned long rate)
{
	return clk_set_rate(clk, rate);
}

/* clk_prepare, then clk_enable; an enable failure undoes the prepare */
static inline int t32_private_clk_prepare_enable(struct clk *clk)
{
	int ret = clk_prepare(clk);

	if (ret)
		return ret;
	ret = clk_enable(clk);
	if (ret)
		clk_unprepare(clk);
	return ret;
}

/* ---- i2c (0x0, 0x8, 0x144..0x16c, 0x550..0x590) ---- */
static inline void t32_private_i2c_set_clientdata(struct i2c_client *client, void *data)
{
	i2c_set_clientdata(client, data);
}

static inline void *t32_private_i2c_get_clientdata(const struct i2c_client *client)
{
	return i2c_get_clientdata(client);
}

static inline int t32_private_i2c_transfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	return i2c_transfer(adap, msgs, num);
}

static inline int t32_private_i2c_add_driver(struct i2c_driver *drv)
{
	return i2c_add_driver(drv);
}

static inline void t32_private_i2c_del_driver(struct i2c_driver *drv)
{
	i2c_del_driver(drv);
}

static inline struct i2c_adapter *t32_private_i2c_get_adapter(int nr)
{
	return i2c_get_adapter(nr);
}

static inline void t32_private_i2c_put_adapter(struct i2c_adapter *adap)
{
	i2c_put_adapter(adap);
}

static inline int t32_private_i2c_register_driver(struct module *owner, struct i2c_driver *drv)
{
	return i2c_register_driver(owner, drv);
}

static inline struct i2c_client *t32_private_i2c_new_device(struct i2c_adapter *adap,
							    const struct i2c_board_info *info)
{
	return i2c_new_device(adap, info);
}

static inline void t32_private_i2c_unregister_device(struct i2c_client *client)
{
	i2c_unregister_device(client);
}

/* ---- gpio (0x17c..0x1d0, 0x5a0, 0x5d0) ---- */
static inline int t32_private_gpio_request(unsigned int gpio, const char *label)
{
	return gpio_request(gpio, label);
}

static inline void t32_private_gpio_free(unsigned int gpio)
{
	gpio_free(gpio);
}

static inline int t32_private_gpio_direction_output(unsigned int gpio, int value)
{
	return gpio_direction_output(gpio, value);
}

static inline int t32_private_gpio_direction_input(unsigned int gpio)
{
	return gpio_direction_input(gpio);
}

static inline int t32_private_gpio_set_debounce(unsigned int gpio, unsigned int debounce)
{
	return gpio_set_debounce(gpio, debounce);
}

static inline int t32_private_jzgpio_set_func(int port, int func, unsigned long pins)
{
	return jzgpio_set_func(port, func, pins);
}

/* ---- delays, capability (0x1e0, 0x1f0, 0x238) ---- */
static inline void t32_private_msleep(unsigned int msecs)
{
	msleep(msecs);
}

/* busy wait in whole milliseconds: one __udelay(1000) per millisecond */
static inline void t32_private_mdelay(int msecs)
{
	while (msecs != 0) {
		__udelay(1000);
		msecs--;
	}
}

static inline bool t32_private_capable(int cap)
{
	return capable(cap);
}

/* ---- platform bus, device data (0x348..0x3bc) ---- */
static inline int t32_private_platform_driver_register(struct platform_driver *drv)
{
	return platform_driver_register(drv);
}

static inline void t32_private_platform_driver_unregister(struct platform_driver *drv)
{
	platform_driver_unregister(drv);
}

static inline void t32_private_platform_set_drvdata(struct platform_device *pdev, void *data)
{
	platform_set_drvdata(pdev, data);
}

static inline void *t32_private_platform_get_drvdata(const struct platform_device *pdev)
{
	return platform_get_drvdata(pdev);
}

static inline int t32_private_platform_device_register(struct platform_device *pdev)
{
	return platform_device_register(pdev);
}

static inline void t32_private_platform_device_unregister(struct platform_device *pdev)
{
	platform_device_unregister(pdev);
}

static inline struct resource *t32_private_platform_get_resource(struct platform_device *pdev,
								 unsigned int type, unsigned int num)
{
	return platform_get_resource(pdev, type, num);
}

static inline void t32_private_dev_set_drvdata(struct device *dev, void *data)
{
	dev_set_drvdata(dev, data);
}

static inline void *t32_private_dev_get_drvdata(const struct device *dev)
{
	return dev_get_drvdata(dev);
}

static inline int t32_private_platform_get_irq(struct platform_device *pdev, unsigned int num)
{
	return platform_get_irq(pdev, num);
}

/* ---- memory regions, mapping (0x3cc..0x430) ---- */
static inline struct resource *t32_private_request_mem_region(resource_size_t start, resource_size_t n,
							      const char *name)
{
	return request_mem_region(start, n, name);
}

static inline void t32_private_release_mem_region(resource_size_t start, resource_size_t n)
{
	release_mem_region(start, n);
}

static inline void __iomem *t32_private_ioremap(phys_addr_t offset, unsigned long size)
{
	return ioremap(offset, size);
}

static inline void t32_private_iounmap(const volatile void __iomem *addr)
{
	iounmap((volatile void __iomem *)addr);
}

/* ---- interrupts, locks (0x440..0x540) ---- */
static inline int t32_private_request_threaded_irq(unsigned int irq, irq_handler_t handler,
						   irq_handler_t thread_fn, unsigned long flags,
						   const char *name, void *dev)
{
	return request_threaded_irq(irq, handler, thread_fn, flags, name, dev);
}

/* request_irq: request_threaded_irq without a thread function (0x450) */
static inline int t32_private_request_irq(unsigned int irq, irq_handler_t handler, unsigned long flags,
					  const char *name, void *dev)
{
	return request_threaded_irq(irq, handler, NULL, flags, name, dev);
}

static inline void t32_private_enable_irq(unsigned int irq)
{
	enable_irq(irq);
}

static inline void t32_private_disable_irq(unsigned int irq)
{
	disable_irq(irq);
}

static inline void t32_private_free_irq(unsigned int irq, void *dev_id)
{
	free_irq(irq, dev_id);
}

static inline void t32_private_spin_unlock_irqrestore(spinlock_t *lock, unsigned long flags)
{
	spin_unlock_irqrestore(lock, flags);
}

static inline void t32_private_spin_lock_init(spinlock_t *lock)
{
	spin_lock_init(lock);
}

static inline void t32_private_mutex_lock(struct mutex *lock)
{
	mutex_lock(lock);
}

static inline void t32_private_mutex_unlock(struct mutex *lock)
{
	mutex_unlock(lock);
}

static inline void t32_private_raw_mutex_init(struct mutex *lock, const char *name, struct lock_class_key *key)
{
	__mutex_init(lock, name, key);
}

/* ---- clock source, modules (0x604..0x670) ---- */
static inline unsigned long long t32_private_sched_clock(void)
{
	return sched_clock();
}

static inline bool t32_private_try_module_get(struct module *module)
{
	return try_module_get(module);
}

static inline void t32_private_module_put(struct module *module)
{
	module_put(module);
}

/* ---- completions, wait queues (0x680..0x7ec) ---- */
static inline void t32_private_init_completion(struct completion *x)
{
	init_completion(x);
}

static inline void t32_private_complete(struct completion *x)
{
	complete(x);
}

static inline int t32_private_wait_for_completion_interruptible(struct completion *x)
{
	return wait_for_completion_interruptible(x);
}

static inline unsigned long t32_private_wait_for_completion_timeout(struct completion *x, unsigned long timeout)
{
	return wait_for_completion_timeout(x, timeout);
}

/* sleep until state(data) is true or a signal arrives (-ERESTARTSYS) */
static inline int t32_private_wait_event_interruptible(wait_queue_head_t *q, int (*state)(void *), void *data)
{
	return wait_event_interruptible(*q, state(data));
}

static inline void t32_private_wake_up_all(wait_queue_head_t *q)
{
	wake_up_all(q);
}

static inline void t32_private_wake_up(wait_queue_head_t *q)
{
	wake_up(q);
}

static inline void t32_private_init_waitqueue_head(wait_queue_head_t *q)
{
	init_waitqueue_head(q);
}

/* ---- misc device, proc (0x808..0x828) ---- */
static inline int t32_private_misc_register(struct miscdevice *mdev)
{
	return misc_register(mdev);
}

static inline int t32_private_misc_deregister(struct miscdevice *mdev)
{
	misc_deregister(mdev);
	return 0;
}

static inline struct proc_dir_entry *t32_private_proc_create_data(const char *name, umode_t mode,
								  struct proc_dir_entry *parent,
								  const struct file_operations *proc_fops, void *data)
{
	return proc_create_data(name, mode, parent, proc_fops, data);
}

/* ---- memory (0x838..0x8a8) ---- */
/* below one page the vendor takes kmalloc memory, from one page on vmalloc memory */
static inline void *t32_private_vmalloc(unsigned long size)
{
	if (size < PAGE_SIZE)
		return kmalloc(size, GFP_KERNEL);
	return vmalloc(size);
}

/* frees either kind of t32_private_vmalloc memory */
static inline void t32_private_vfree(void *addr)
{
	if (is_vmalloc_addr(addr))
		vfree(addr);
	else
		kfree(addr);
}

static inline void *t32_private_kmalloc(size_t size, gfp_t flags)
{
	return kmalloc(size, flags);
}

static inline void t32_private_kfree(void *p)
{
	kfree(p);
}

/* ---- user copies (0x8b8, 0x910): see the header comment ---- */
static inline long t32_private_copy_from_user(void *to, const void __user *from, long size)
{
	if (access_ok(VERIFY_READ, from, size))
		return __copy_from_user(to, from, size);
	memcpy(to, (const void __force *)from, size);
	return 0;
}

static inline long t32_private_copy_to_user(void __user *to, const void *from, long size)
{
	if (access_ok(VERIFY_WRITE, to, size))
		return __copy_to_user(to, from, size);
	memcpy((void __force *)to, from, size);
	return 0;
}

/* ---- files (0x968..0x9d0) ---- */
static inline struct file *t32_private_filp_open(const char *filename, int flags, umode_t mode)
{
	return filp_open(filename, flags, mode);
}

static inline int t32_private_filp_close(struct file *filp, fl_owner_t id)
{
	return filp_close(filp, id);
}

static inline ssize_t t32_private_vfs_read(struct file *file, char __user *buf, size_t count, loff_t *pos)
{
	return vfs_read(file, buf, count, pos);
}

static inline ssize_t t32_private_vfs_write(struct file *file, const char __user *buf, size_t count, loff_t *pos)
{
	return vfs_write(file, buf, count, pos);
}

static inline loff_t t32_private_vfs_llseek(struct file *file, loff_t offset, int whence)
{
	return vfs_llseek(file, offset, whence);
}

/* get_fs returns the address limit of the current thread, set_fs replaces it */
static inline mm_segment_t t32_private_get_fs(void)
{
	return get_fs();
}

static inline void t32_private_set_fs(mm_segment_t val)
{
	set_fs(val);
}

static inline void t32_private_dma_cache_sync(struct device *dev, void *vaddr, size_t size,
					      enum dma_data_direction direction)
{
	dma_cache_sync(dev, vaddr, size, direction);
}

/* ---- time, threads (0x9e0..0xa80) ---- */
static inline void t32_private_getrawmonotonic(struct timespec *ts)
{
	getrawmonotonic(ts);
}

static inline bool t32_private_kthread_should_stop(void)
{
	return kthread_should_stop();
}

/* create the thread (name used as the format) and start it unless creation failed */
static inline struct task_struct *t32_private_kthread_run(int (*threadfn)(void *data), void *data,
							  const char namefmt[])
{
	struct task_struct *k = kthread_create(threadfn, data, namefmt);

	if (!IS_ERR(k))
		wake_up_process(k);
	return k;
}

static inline int t32_private_kthread_stop(struct task_struct *k)
{
	return kthread_stop(k);
}

/* ---- seq / proc files (0xa90..0xb38, 0xbf0, 0xc14) ---- */
static inline ssize_t t32_private_seq_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
	return seq_read(file, buf, size, ppos);
}

static inline loff_t t32_private_seq_lseek(struct file *file, loff_t offset, int whence)
{
	return seq_lseek(file, offset, whence);
}

static inline int t32_private_single_release(struct inode *inode, struct file *file)
{
	return single_release(inode, file);
}

static inline int t32_private_single_open_size(struct file *file, int (*show)(struct seq_file *, void *),
					       void *data, size_t size)
{
	return single_open_size(file, show, data, size);
}

static inline struct proc_dir_entry *t32_private_jz_proc_mkdir(char *s)
{
	return jz_proc_mkdir(s);
}

static inline void t32_private_proc_remove(struct proc_dir_entry *de)
{
	proc_remove(de);
}

/* printf into the seq file through one "%pV" (vendor 0xaf0); varargs: not inline */
static __maybe_unused int t32_private_seq_printf(struct seq_file *m, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	seq_printf(m, "%pV", &vaf);
	va_end(args);
	return 0;
}

static inline unsigned long long t32_private_simple_strtoull(const char *cp, char **endp, unsigned int base)
{
	return simple_strtoull(cp, endp, base);
}

/* proc_create is proc_create_data without data (vendor 0xbf0) */
static inline struct proc_dir_entry *t32_private_proc_create(const char *name, umode_t mode,
							     struct proc_dir_entry *parent,
							     const struct file_operations *proc_fops)
{
	return proc_create_data(name, mode, parent, proc_fops, NULL);
}

static inline void t32_private_remove_proc_entry(const char *name, struct proc_dir_entry *parent)
{
	remove_proc_entry(name, parent);
}

/* ---- module requests (0x624) ---- */
/* the vendor always requests synchronously, whatever wait says */
static __maybe_unused int t32_private_request_module(bool wait, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;
	int ret;

	(void)wait;
	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	ret = __request_module(true, "%pV", &vaf);
	va_end(args);
	return ret;
}

/* ---- ISP reserved memory (0xb48) ---- */
static inline void t32_private_get_isp_priv_mem(unsigned int *base, unsigned int *size)
{
	*base = ispmem_base;
	*size = ispmem_size;
}

/* ---- timers, work, time of day (0xb64..0xbe0) ---- */
static inline ktime_t t32_private_ktime_set(const long secs, const unsigned long nsecs)
{
	return ktime_set(secs, nsecs);
}

static inline void t32_private_set_current_state(unsigned int state)
{
	set_current_state(state);
}

static inline int t32_private_schedule_hrtimeout(ktime_t *expires, const enum hrtimer_mode mode)
{
	return schedule_hrtimeout(expires, mode);
}

static inline bool t32_private_schedule_work(struct work_struct *work)
{
	return schedule_work(work);
}

static inline bool t32_private_flush_work(struct work_struct *work)
{
	return flush_work(work);
}

static inline void t32_private_do_gettimeofday(struct timeval *tv)
{
	do_gettimeofday(tv);
}

/* ---- weak vendor stubs (0xcd70, 0xd014) ---- */
static inline void *t32_private_get_init_net(void)
{
	return NULL;
}

static inline int t32_private_math_exp2(void)
{
	return 0;
}

/* ---- ISP block reset through the CPM (0x17750) ----
 * CPM SRBC (0x100000c4, KSEG1 0xb00000c4): the vendor raises bit 21 (stop
 * request), waits up to 500 rounds for bit 20 (stopped), then pulses bit 22
 * (soft reset) while dropping bit 21.  One round sleeps 2 ms, or busy waits
 * 1 ms when fast start (vendor zeratul_open) is on.  An argument other than 0
 * does nothing. */
#define T32_CPM_SRBC_KSEG1	0xb00000c4UL
#define T32_SRBC_ISP_STOP	(1u << 21)
#define T32_SRBC_ISP_STOPPED	(1u << 20)
#define T32_SRBC_ISP_RESET	(1u << 22)

static inline int t32_private_reset_tx_isp_module(int arg, const volatile unsigned int *fast_start)
{
	volatile unsigned int *srbc = (volatile unsigned int *)T32_CPM_SRBC_KSEG1;
	int tries;

	if (arg != 0)
		return 0;
	*srbc |= T32_SRBC_ISP_STOP;
	if (*fast_start)
		isp_printf(1, "optimize startup time\n");
	for (tries = 500; tries != 0; tries--) {
		if (*srbc & T32_SRBC_ISP_STOPPED) {
			*srbc = (*srbc & ~T32_SRBC_ISP_STOP) | T32_SRBC_ISP_RESET;
			*srbc &= ~T32_SRBC_ISP_RESET;
			return 0;
		}
		if (*fast_start)
			__udelay(1000);
		else
			t32_private_msleep(2);
	}
	return -1;
}

#endif
