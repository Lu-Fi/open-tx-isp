// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our private_* kernel wrappers (driver/t32/tx_isp_t32_private.h)
 * against Matt Davis' recovered normalized wrappers (PR #15 @ 2e2e06bc, ref/,
 * built through fix_priv_kern.sh against the host kernel model t32o_kern.h).
 *
 * The kernel is a set of recording hooks (t32o_kern_rec.h): every call with
 * its arguments (strings by content, callee-local objects by presence) and a
 * seeded result.  Per case both sides start from the same random pool of
 * objects, thread_info and hook results; compared are the hook trace (names,
 * arguments, order), the return value and every byte of the pool and
 * thread_info.  4000 random cases per wrapper, arguments mixed from pool
 * pointers, 0 / 1 / -1 / error codes and random 32 / 64 bit values.
 *
 * Not here: the wrappers whose recovered C differs from the vendor object
 * (vmalloc, vfree, copy_from/to_user, kthread_run, seq_printf) and the
 * mechanical ones are in t32_privx_oracle.  Exit 0 = all equal, 1 = mismatch.
 */
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-prototypes"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
#pragma GCC diagnostic ignored "-Wpointer-to-int-cast"
#pragma GCC diagnostic ignored "-Wcast-function-type"
#include <stdio.h>
#include <stdlib.h>
#include "t32o_kern.h"
#include "t32o_kern_rec.h"
#include "tx_isp_t32_private.h"

/* ---- reference prototypes (ref/, renamed by fix_priv_kern.sh) ---- */
void ref_private_i2c_set_clientdata(struct i2c_client *dev, void *data);
void *ref_private_i2c_get_clientdata(const struct i2c_client *dev);
struct clk *ref_private_clk_get(struct device *dev, const char *id);
struct clk *ref_private_devm_clk_get(struct device *dev, const char *id);
int ref_private_clk_enable(struct clk *clk);
void ref_private_clk_disable(struct clk *clk);
void ref_private_clk_disable_unprepare(struct clk *clk);
unsigned long ref_private_clk_get_rate(struct clk *clk);
void ref_private_clk_put(struct clk *clk);
void ref_private_devm_clk_put(struct device *dev, struct clk *clk);
int ref_private_clk_set_rate(struct clk *clk, unsigned long rate);
int ref_private_i2c_transfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num);
void ref_private_i2c_del_driver(struct i2c_driver *driver);
int ref_private_gpio_request(unsigned int gpio, const char *label);
void ref_private_gpio_free(unsigned int gpio);
int ref_private_gpio_direction_output(unsigned int gpio, int value);
int ref_private_jzgpio_set_func(int port, int func, unsigned long pins);
void ref_private_msleep(unsigned int msecs);
void ref_private_mdelay(int32_t arg1);
bool ref_private_capable(int cap);
int ref_private_clk_prepare_enable(struct clk *clk);
void ref_private_platform_driver_unregister(struct platform_driver *drv);
void ref_private_platform_set_drvdata(struct platform_device *pdev, void *data);
void *ref_private_platform_get_drvdata(const struct platform_device *pdev);
int ref_private_platform_device_register(struct platform_device *pdev);
void ref_private_platform_device_unregister(struct platform_device *pdev);
struct resource *ref_private_platform_get_resource(struct platform_device *pdev, unsigned int type, unsigned int num);
void ref_private_dev_set_drvdata(struct device *dev, void *data);
void *ref_private_dev_get_drvdata(const struct device *dev);
int ref_private_platform_get_irq(struct platform_device *pdev, unsigned int num);
struct resource *ref_private_request_mem_region(resource_size_t start, resource_size_t n, const char *name);
void ref_private_release_mem_region(resource_size_t start, resource_size_t n);
void  *ref_private_ioremap(phys_addr_t offset, unsigned long size);
void ref_private_iounmap(const volatile void  *addr);
int ref_private_request_threaded_irq(unsigned int irq, irq_handler_t handler, irq_handler_t thread_fn, unsigned long flags, const char *name, void *dev);
int ref_private_request_irq(unsigned int irq, irq_handler_t handler, unsigned long flags, const char *name, void *dev);
void ref_private_enable_irq(unsigned int irq);
void ref_private_disable_irq(unsigned int irq);
void ref_private_free_irq(unsigned int irq, void *dev_id);
void ref_private_spin_unlock_irqrestore(spinlock_t *lock, unsigned long flags);
void ref_private_spin_lock_init(spinlock_t *lock);
void ref_private_mutex_lock(struct mutex *lock);
void ref_private_mutex_unlock(struct mutex *lock);
void ref_private_raw_mutex_init(struct mutex *lock, const char *name, struct lock_class_key *key);
struct i2c_adapter *ref_private_i2c_get_adapter(int nr);
void ref_private_i2c_put_adapter(struct i2c_adapter *adap);
int ref_private_i2c_register_driver(struct module *owner, struct i2c_driver *driver);
struct i2c_client *ref_private_i2c_new_device(struct i2c_adapter *adap, const struct i2c_board_info *info);
void ref_private_i2c_unregister_device(struct i2c_client *client);
int ref_private_gpio_direction_input(unsigned int gpio);
int ref_private_gpio_set_debounce(unsigned int gpio, unsigned int debounce);
unsigned long long ref_private_sched_clock(void);
bool ref_private_try_module_get(struct module *module);
int ref_private_request_module(bool wait, const char *fmt, ...);
void ref_private_module_put(struct module *module);
void ref_private_init_completion(struct completion *x);
void ref_private_complete(struct completion *x);
int ref_private_wait_for_completion_interruptible(struct completion *x);
unsigned long ref_private_wait_for_completion_timeout(struct completion *x, unsigned long timeout);
void ref_private_wake_up_all(wait_queue_head_t *q);
void ref_private_wake_up(wait_queue_head_t *q);
void ref_private_init_waitqueue_head(wait_queue_head_t *q);
int ref_private_misc_register(struct miscdevice *mdev);
int ref_private_misc_deregister(struct miscdevice *mdev);
struct proc_dir_entry *ref_private_proc_create_data(const char *name, umode_t mode, struct proc_dir_entry *parent, const struct file_operations *proc_fops, void *data);
void *ref_private_kmalloc(size_t size, gfp_t flags);
void ref_private_kfree(void *p);
struct file *ref_private_filp_open(const char *filename, int flags, umode_t mode);
int ref_private_filp_close(struct file *filp, fl_owner_t id);
ssize_t ref_private_vfs_read(struct file *file, char  *buf, size_t count, loff_t *pos);
ssize_t ref_private_vfs_write(struct file *file, const char  *buf, size_t count, loff_t *pos);
loff_t ref_private_vfs_llseek(struct file *file, loff_t offset, int whence);
mm_segment_t ref_private_get_fs(void);
void ref_private_set_fs(mm_segment_t val);
void ref_private_dma_cache_sync(struct device *dev, void *t32_data_e3f30, size_t size, enum dma_data_direction direction);
void ref_private_getrawmonotonic(struct timespec *ts);
bool ref_private_kthread_should_stop(void);
int ref_private_kthread_stop(struct task_struct *k);
ssize_t ref_private_seq_read(struct file *file, char  *buf, size_t size, loff_t *ppos);
loff_t ref_private_seq_lseek(struct file *file, loff_t offset, int whence);
int ref_private_single_release(struct inode *inode, struct file *file);
struct proc_dir_entry *ref_private_jz_proc_mkdir(char *s);
void ref_private_proc_remove(struct proc_dir_entry *de);
unsigned long long ref_private_simple_strtoull(const char *cp, char **endp, unsigned int base);
ktime_t ref_private_ktime_set(const long secs, const unsigned long nsecs);
void ref_private_set_current_state(unsigned int state);
int ref_private_schedule_hrtimeout(ktime_t *expires, const enum hrtimer_mode mode);
bool ref_private_schedule_work(struct work_struct *work);
int ref_private_flush_work(struct work_struct *work);
void ref_private_do_gettimeofday(struct timeval *tv);
void ref_private_remove_proc_entry(const char *name, struct proc_dir_entry *parent);
int32_t ref_private_math_exp2(void);
int ref_private_wait_event_interruptible(wait_queue_head_t *q, int (*state)(void *), void *data);
int ref_private_single_open_size(struct file *file, int (*show)(struct seq_file *, void *), void *data, size_t size);

#define NCASE	4000
#define P(i)	((void *)(uintptr_t)r[i])
#define Q(i)	((void *)(t32k_pool[r[i] & 3] + ((r[i] >> 8) & 15) * 8))
typedef u64 (*t32p_call)(u64 *r);
#define VCASE(fn, args) \
	static u64 o_##fn(u64 *r) { t32_private_##fn args; return 0; } \
	static u64 f_##fn(u64 *r) { ref_private_##fn args; return 0; }
#define PCASE(fn, args) \
	static u64 o_##fn(u64 *r) { return (u64)(uintptr_t)t32_private_##fn args; } \
	static u64 f_##fn(u64 *r) { return (u64)(uintptr_t)ref_private_##fn args; }
#define SCASE(fn, args) \
	static u64 o_##fn(u64 *r) { return (u64)(s64)t32_private_##fn args; } \
	static u64 f_##fn(u64 *r) { return (u64)(s64)ref_private_##fn args; }
#define UCASE(fn, args) \
	static u64 o_##fn(u64 *r) { return (u64)t32_private_##fn args; } \
	static u64 f_##fn(u64 *r) { return (u64)ref_private_##fn args; }
VCASE(i2c_set_clientdata, ((struct i2c_client *)Q(0), (void *)P(1)))
PCASE(i2c_get_clientdata, ((const struct i2c_client *)Q(0)))
PCASE(clk_get, ((struct device *)P(0), (const char *)P(1)))
PCASE(devm_clk_get, ((struct device *)P(0), (const char *)P(1)))
SCASE(clk_enable, ((struct clk *)P(0)))
VCASE(clk_disable, ((struct clk *)P(0)))
VCASE(clk_disable_unprepare, ((struct clk *)P(0)))
UCASE(clk_get_rate, ((struct clk *)P(0)))
VCASE(clk_put, ((struct clk *)P(0)))
VCASE(devm_clk_put, ((struct device *)P(0), (struct clk *)P(1)))
SCASE(clk_set_rate, ((struct clk *)P(0), (unsigned long)r[1]))
SCASE(i2c_transfer, ((struct i2c_adapter *)P(0), (struct i2c_msg *)P(1), (int)r[2]))
VCASE(i2c_del_driver, ((struct i2c_driver *)P(0)))
SCASE(gpio_request, ((unsigned int)r[0], (const char *)P(1)))
VCASE(gpio_free, ((unsigned int)r[0]))
SCASE(gpio_direction_output, ((unsigned int)r[0], (int)r[1]))
SCASE(jzgpio_set_func, ((int)r[0], (int)r[1], (unsigned long)r[2]))
VCASE(msleep, ((unsigned int)r[0]))
UCASE(capable, ((int)r[0]))
SCASE(clk_prepare_enable, ((struct clk *)P(0)))
VCASE(platform_driver_unregister, ((struct platform_driver *)P(0)))
VCASE(platform_set_drvdata, ((struct platform_device *)Q(0), (void *)P(1)))
PCASE(platform_get_drvdata, ((const struct platform_device *)Q(0)))
SCASE(platform_device_register, ((struct platform_device *)P(0)))
VCASE(platform_device_unregister, ((struct platform_device *)P(0)))
PCASE(platform_get_resource, ((struct platform_device *)P(0), (unsigned int)r[1], (unsigned int)r[2]))
VCASE(dev_set_drvdata, ((struct device *)Q(0), (void *)P(1)))
PCASE(dev_get_drvdata, ((const struct device *)Q(0)))
SCASE(platform_get_irq, ((struct platform_device *)P(0), (unsigned int)r[1]))
PCASE(request_mem_region, ((resource_size_t)r[0], (resource_size_t)r[1], (const char *)P(2)))
VCASE(release_mem_region, ((resource_size_t)r[0], (resource_size_t)r[1]))
PCASE(ioremap, ((phys_addr_t)r[0], (unsigned long)r[1]))
VCASE(iounmap, ((const volatile void  *)P(0)))
VCASE(enable_irq, ((unsigned int)r[0]))
VCASE(disable_irq, ((unsigned int)r[0]))
VCASE(free_irq, ((unsigned int)r[0], (void *)P(1)))
VCASE(spin_unlock_irqrestore, ((spinlock_t *)P(0), (unsigned long)r[1]))
VCASE(spin_lock_init, ((spinlock_t *)P(0)))
VCASE(mutex_lock, ((struct mutex *)P(0)))
VCASE(mutex_unlock, ((struct mutex *)P(0)))
VCASE(raw_mutex_init, ((struct mutex *)P(0), (const char *)P(1), (struct lock_class_key *)P(2)))
PCASE(i2c_get_adapter, ((int)r[0]))
VCASE(i2c_put_adapter, ((struct i2c_adapter *)P(0)))
SCASE(i2c_register_driver, ((struct module *)P(0), (struct i2c_driver *)P(1)))
PCASE(i2c_new_device, ((struct i2c_adapter *)P(0), (const struct i2c_board_info *)P(1)))
VCASE(i2c_unregister_device, ((struct i2c_client *)P(0)))
SCASE(gpio_direction_input, ((unsigned int)r[0]))
SCASE(gpio_set_debounce, ((unsigned int)r[0], (unsigned int)r[1]))
UCASE(sched_clock, ())
UCASE(try_module_get, ((struct module *)P(0)))
VCASE(module_put, ((struct module *)P(0)))
VCASE(complete, ((struct completion *)P(0)))
SCASE(wait_for_completion_interruptible, ((struct completion *)P(0)))
UCASE(wait_for_completion_timeout, ((struct completion *)P(0), (unsigned long)r[1]))
VCASE(wake_up_all, ((wait_queue_head_t *)P(0)))
VCASE(wake_up, ((wait_queue_head_t *)P(0)))
VCASE(init_waitqueue_head, ((wait_queue_head_t *)P(0)))
SCASE(misc_register, ((struct miscdevice *)P(0)))
SCASE(misc_deregister, ((struct miscdevice *)P(0)))
PCASE(proc_create_data, ((const char *)P(0), (umode_t)r[1], (struct proc_dir_entry *)P(2), (const struct file_operations *)P(3), (void *)P(4)))
PCASE(kmalloc, ((size_t)r[0], (gfp_t)r[1]))
VCASE(kfree, ((void *)P(0)))
PCASE(filp_open, ((const char *)P(0), (int)r[1], (umode_t)r[2]))
SCASE(filp_close, ((struct file *)P(0), (fl_owner_t)r[1]))
SCASE(vfs_read, ((struct file *)P(0), (char  *)P(1), (size_t)r[2], (loff_t *)P(3)))
SCASE(vfs_write, ((struct file *)P(0), (const char  *)P(1), (size_t)r[2], (loff_t *)P(3)))
VCASE(dma_cache_sync, ((struct device *)P(0), (void *)P(1), (size_t)r[2], (enum dma_data_direction)r[3]))
UCASE(kthread_should_stop, ())
SCASE(kthread_stop, ((struct task_struct *)P(0)))
SCASE(seq_read, ((struct file *)P(0), (char  *)P(1), (size_t)r[2], (loff_t *)P(3)))
SCASE(single_release, ((struct inode *)P(0), (struct file *)P(1)))
PCASE(jz_proc_mkdir, ((char *)P(0)))
VCASE(proc_remove, ((struct proc_dir_entry *)P(0)))
UCASE(simple_strtoull, ((const char *)P(0), (char **)P(1), (unsigned int)r[2]))
SCASE(schedule_hrtimeout, ((ktime_t *)P(0), (enum hrtimer_mode)r[1]))
UCASE(schedule_work, ((struct work_struct *)P(0)))
SCASE(flush_work, ((struct work_struct *)P(0)))
VCASE(do_gettimeofday, ((struct timeval *)P(0)))
VCASE(remove_proc_entry, ((const char *)P(0), (struct proc_dir_entry *)P(1)))
SCASE(math_exp2, ())

/* ---- wrappers with struct values, callbacks, varargs or bounded loops ---- */
static u64 o_get_fs(u64 *r) { return t32_private_get_fs().seg; }
static u64 f_get_fs(u64 *r) { return ref_private_get_fs().seg; }
static u64 o_set_fs(u64 *r) { mm_segment_t s = { (u32)r[0] }; t32_private_set_fs(s); return 0; }
static u64 f_set_fs(u64 *r) { mm_segment_t s = { (u32)r[0] }; ref_private_set_fs(s); return 0; }
static u64 o_ktime_set(u64 *r) { return (u64)t32_private_ktime_set((long)(s32)r[0], (unsigned long)(u32)r[1]).tv64; }
static u64 f_ktime_set(u64 *r) { return (u64)ref_private_ktime_set((long)(s32)r[0], (unsigned long)(u32)r[1]).tv64; }
static u64 o_request_module(u64 *r)
{
	return (u64)(s64)t32_private_request_module((bool)(r[0] & 1), (const char *)P(1), (int)r[2], (int)r[3],
						    (int)r[4], (int)r[5]);
}
static u64 f_request_module(u64 *r)
{
	return (u64)(s64)ref_private_request_module((bool)(r[0] & 1), (const char *)P(1), (int)r[2], (int)r[3],
						    (int)r[4], (int)r[5]);
}
/* the wait condition: recorded, true with probability 1/3 */
static int st_fn(void *data)
{
	static const int k_[1] = { T32K_V };
	u64 v[1] = { T32K_U(data) };

	return t32k_rec("state", 1, k_, v) % 3 == 0;
}
static u64 o_wait_event_interruptible(u64 *r) { return (u64)(s64)t32_private_wait_event_interruptible((wait_queue_head_t *)P(0), st_fn, P(2)); }
static u64 f_wait_event_interruptible(u64 *r) { return (u64)(s64)ref_private_wait_event_interruptible((wait_queue_head_t *)P(0), st_fn, P(2)); }
static u64 o_getrawmonotonic(u64 *r) { t32_private_getrawmonotonic((struct timespec *)Q(0)); return 0; }
static u64 f_getrawmonotonic(u64 *r) { ref_private_getrawmonotonic((struct timespec *)Q(0)); return 0; }
static u64 o_init_completion(u64 *r) { t32_private_init_completion((struct completion *)Q(0)); return 0; }
static u64 f_init_completion(u64 *r) { ref_private_init_completion((struct completion *)Q(0)); return 0; }
static u64 o_set_current_state(u64 *r) { t32_private_set_current_state((unsigned int)r[0]); return 0; }
static u64 f_set_current_state(u64 *r) { ref_private_set_current_state((unsigned int)r[0]); return 0; }
static int show_fn(struct seq_file *m, void *v) { return 0; }
static u64 o_single_open_size(u64 *r) { return (u64)(s64)t32_private_single_open_size((struct file *)P(0), show_fn, P(2), (size_t)r[3]); }
static u64 f_single_open_size(u64 *r) { return (u64)(s64)ref_private_single_open_size((struct file *)P(0), show_fn, P(2), (size_t)r[3]); }
static u64 o_vfs_llseek(u64 *r) { return (u64)t32_private_vfs_llseek((struct file *)P(0), (loff_t)r[1], (int)r[2]); }
static u64 f_vfs_llseek(u64 *r) { return (u64)ref_private_vfs_llseek((struct file *)P(0), (loff_t)r[1], (int)r[2]); }
static u64 o_seq_lseek(u64 *r) { return (u64)t32_private_seq_lseek((struct file *)P(0), (loff_t)r[1], (int)r[2]); }
static u64 f_seq_lseek(u64 *r) { return (u64)ref_private_seq_lseek((struct file *)P(0), (loff_t)r[1], (int)r[2]); }
static int irq_fn(int irq, void *dev) { return 0; }
static int thr_fn(int irq, void *dev) { return 1; }
static u64 o_request_threaded_irq(u64 *r)
{
	return (u64)(s64)t32_private_request_threaded_irq((unsigned int)r[0], irq_fn, (r[2] & 1) ? thr_fn : NULL,
							  (unsigned long)r[3], (const char *)P(4), P(5));
}
static u64 f_request_threaded_irq(u64 *r)
{
	return (u64)(s64)ref_private_request_threaded_irq((unsigned int)r[0], irq_fn, (r[2] & 1) ? thr_fn : NULL,
							  (unsigned long)r[3], (const char *)P(4), P(5));
}
static u64 o_request_irq(u64 *r) { return (u64)(s64)t32_private_request_irq((unsigned int)r[0], irq_fn, (unsigned long)r[2], (const char *)P(3), P(4)); }
static u64 f_request_irq(u64 *r) { return (u64)(s64)ref_private_request_irq((unsigned int)r[0], irq_fn, (unsigned long)r[2], (const char *)P(3), P(4)); }
/* mdelay: one __udelay per millisecond, kept below the log size */
static u64 o_mdelay(u64 *r) { t32_private_mdelay((int)(r[0] % 50)); return 0; }
static u64 f_mdelay(u64 *r) { ref_private_mdelay((int32_t)(r[0] % 50)); return 0; }

#define E(fn)	{ #fn, o_##fn, f_##fn }
static const struct { const char *name; t32p_call o, f; } tab[] = {
	E(i2c_set_clientdata),
	E(i2c_get_clientdata),
	E(clk_get),
	E(devm_clk_get),
	E(clk_enable),
	E(clk_disable),
	E(clk_disable_unprepare),
	E(clk_get_rate),
	E(clk_put),
	E(devm_clk_put),
	E(clk_set_rate),
	E(i2c_transfer),
	E(i2c_del_driver),
	E(gpio_request),
	E(gpio_free),
	E(gpio_direction_output),
	E(jzgpio_set_func),
	E(msleep),
	E(capable),
	E(clk_prepare_enable),
	E(platform_driver_unregister),
	E(platform_set_drvdata),
	E(platform_get_drvdata),
	E(platform_device_register),
	E(platform_device_unregister),
	E(platform_get_resource),
	E(dev_set_drvdata),
	E(dev_get_drvdata),
	E(platform_get_irq),
	E(request_mem_region),
	E(release_mem_region),
	E(ioremap),
	E(iounmap),
	E(enable_irq),
	E(disable_irq),
	E(free_irq),
	E(spin_unlock_irqrestore),
	E(spin_lock_init),
	E(mutex_lock),
	E(mutex_unlock),
	E(raw_mutex_init),
	E(i2c_get_adapter),
	E(i2c_put_adapter),
	E(i2c_register_driver),
	E(i2c_new_device),
	E(i2c_unregister_device),
	E(gpio_direction_input),
	E(gpio_set_debounce),
	E(sched_clock),
	E(try_module_get),
	E(module_put),
	E(complete),
	E(wait_for_completion_interruptible),
	E(wait_for_completion_timeout),
	E(wake_up_all),
	E(wake_up),
	E(init_waitqueue_head),
	E(misc_register),
	E(misc_deregister),
	E(proc_create_data),
	E(kmalloc),
	E(kfree),
	E(filp_open),
	E(filp_close),
	E(vfs_read),
	E(vfs_write),
	E(dma_cache_sync),
	E(kthread_should_stop),
	E(kthread_stop),
	E(seq_read),
	E(single_release),
	E(jz_proc_mkdir),
	E(proc_remove),
	E(simple_strtoull),
	E(schedule_hrtimeout),
	E(schedule_work),
	E(flush_work),
	E(do_gettimeofday),
	E(remove_proc_entry),
	E(math_exp2),
	E(get_fs), E(set_fs), E(ktime_set), E(request_module), E(wait_event_interruptible), E(getrawmonotonic),
	E(init_completion), E(set_current_state), E(single_open_size), E(vfs_llseek), E(seq_lseek),
	E(request_threaded_irq), E(request_irq), E(mdelay),
};

static u8 pool0[sizeof(t32k_pool)], ti0[sizeof(t32k_ti)];
static u8 pool_o[sizeof(t32k_pool)], ti_o[sizeof(t32k_ti)];
static struct t32k_log log_o;

/* one argument: a pool pointer, a boundary or a random value */
static u64 arg(u64 *s)
{
	u64 x = t32k_next(s);

	switch ((x >> 60) & 7) {
	case 0: return (u64)(uintptr_t)(t32k_pool[x & 3] + ((x >> 8) & 15) * 8);
	case 1: return 0;
	case 2: return (u64)-1;
	case 3: return x & 1;
	case 4: return (u32)x;
	case 5: return (u64)(s64)(s32)x;
	case 6: return x & 0xfff;
	default: return x;
	}
}

static void setup(u64 seed)
{
	u64 s = seed ^ 0x9e3779b97f4a7c15ull;
	size_t i;

	for (i = 0; i < sizeof(pool0); i++)
		pool0[i] = (u8)t32k_next(&s);
	for (i = 0; i < sizeof(ti0); i++)
		ti0[i] = (u8)t32k_next(&s);
	/* thread_info->task points at a pool object (set_current_state writes its state) */
	*(u32 *)ti0 = (u32)(uintptr_t)t32k_pool[3];
}

static void arm(u64 seed)
{
	memmove(t32k_pool, pool0, sizeof(pool0));
	memmove(t32k_ti, ti0, sizeof(ti0));
	memset(&t32k_cur, 0, sizeof(t32k_cur));
	t32k_rs = seed | 1;
}

int main(void)
{
	u64 s = 0x7432a11ce5eedull, r[8], ro, rf;
	unsigned int t, c, bad = 0, n = 0;
	int i;

	for (t = 0; t < sizeof(tab) / sizeof(tab[0]); t++) {
		for (c = 0; c < NCASE; c++) {
			u64 seed = t32k_next(&s);

			for (i = 0; i < 8; i++)
				r[i] = arg(&s);
			setup(seed);
			arm(seed);
			ro = tab[t].o(r);
			log_o = t32k_cur;
			memmove(pool_o, t32k_pool, sizeof(pool_o));
			memmove(ti_o, t32k_ti, sizeof(ti_o));
			arm(seed);
			rf = tab[t].f(r);
			n++;
			if (t32k_logcmp(tab[t].name, c, &log_o, &t32k_cur)) {
				bad++;
			} else if (ro != rf) {
				printf("%s case %u: return 0x%llx vs 0x%llx\n", tab[t].name, c,
				       (unsigned long long)ro, (unsigned long long)rf);
				bad++;
			} else if (memcmp(pool_o, t32k_pool, sizeof(pool_o)) || memcmp(ti_o, t32k_ti, sizeof(ti_o))) {
				printf("%s case %u: memory differs\n", tab[t].name, c);
				bad++;
			}
			if (bad > 10)
				goto out;
		}
	}
out:
	printf("t32_priv_oracle: %u wrappers, %u cases, %u mismatches\n", (unsigned int)(sizeof(tab) / sizeof(tab[0])), n, bad);
	return bad ? 1 : 0;
}
