/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Host model of the T32 vendor kernel (Linux 4.4.94, MIPS32 UP, no lock
 * debugging) for the private_* wrapper oracles (t32_priv_oracle,
 * t32_privx_oracle).  Both sides compile against it: the reference copies of
 * Matt Davis' normalized wrappers (ref/fn_*_private_*.c, include line pointed
 * here by fix_priv_kern.sh) and our port driver/t32/tx_isp_t32_private.h.
 *
 * Every out-of-line kernel function the vendor module calls is a recording
 * hook (t32k_rec, defined by the oracle): name, arguments, and a seeded
 * return value.  The kernel inlines / macros are expanded the way the vendor
 * object tx-isp-t32.o shows them (addresses of private_* in .text), so the
 * hook trace is the vendor's relocation trace:
 *   i2c clientdata +108, platform drvdata +92, dev drvdata +76 (0x0..0x3b8),
 *   gpio_* -> gpio_to_desc + gpiod_* (0x19c, 0x5a0, 0x5d0),
 *   request/release_mem_region -> __request/__release_region(&iomem_resource) (0x3cc, 0x404),
 *   ioremap -> __ioremap(.., 1024 = _CACHE_UNCACHED) (0x420), iounmap -> __iounmap,
 *   spin_lock_init -> nothing (0x518), wake_up(_all) -> __wake_up(q, 3, 1|0, NULL) (0x7bc, 0x7d4),
 *   init_completion / init_waitqueue_head -> __init_waitqueue_head(q, "&x->wait" / "q", key) (0x680, 0x7ec),
 *   wait_event_interruptible -> prepare_to_wait_event / schedule / finish_wait loop (0x6d4),
 *   get_fs / set_fs -> thread_info->addr_limit (+24) (0x9b8, 0x9c8),
 *   set_current_state -> thread_info->task->state (0xb94), schedule_work -> queue_work_on(1, system_wq, w) (0xbb4),
 *   getrawmonotonic -> getrawmonotonic64 + narrowing (0x9e0), ktime_set -> secs * 1e9 + nsecs (0xb64),
 *   kmalloc -> __kmalloc, kthread_create -> kthread_create_on_node(.., -1, ..) (0xa30),
 *   copy_*_user -> access_ok mask test on addr_limit + __copy_user (0x8b8, 0x910).
 * Target word size is 32 bit: unsigned long arithmetic of the inlines is done in u32.
 */
#ifndef T32O_KERN_H
#define T32O_KERN_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t s32;
typedef int64_t s64;
typedef u32 resource_size_t;
typedef u32 phys_addr_t;
typedef u16 umode_t;
typedef u32 gfp_t;
typedef s64 loff_t;
typedef long t32k_ssize_t;
#define ssize_t t32k_ssize_t
typedef void *fl_owner_t;
typedef s64 time64_t;
#define __iomem
#define __user
#define __force
#define __maybe_unused	__attribute__((unused))

struct device;
struct clk;
struct i2c_client;
struct i2c_adapter;
struct i2c_msg;
struct i2c_driver;
struct i2c_board_info;
struct platform_driver;
struct platform_device;
struct resource;
struct module;
struct mutex;
struct lock_class_key { int dummy; };
struct miscdevice;
struct proc_dir_entry;
struct file_operations;
struct file;
struct inode;
struct seq_file;
struct task_struct;
struct work_struct;
struct workqueue_struct;
struct timeval;
typedef struct { u32 lock; } spinlock_t;
typedef struct { u32 lock; u32 next, prev; } wait_queue_head_t;
typedef struct { u32 flags; u32 priv; u32 func; u32 next, prev; } wait_queue_t;
struct completion { u32 done; wait_queue_head_t wait; };
typedef struct { u32 seg; } mm_segment_t;
typedef union { s64 tv64; } ktime_t;
#define timespec t32k_timespec	/* the 32 bit kernel layout, not the host one */
struct timespec { s32 tv_sec; s32 tv_nsec; };
struct timespec64 { time64_t tv_sec; s32 tv_nsec; };
struct va_format { const char *fmt; va_list *va; };
typedef int (*irq_handler_t)(int, void *);
enum dma_data_direction { DMA_BIDIRECTIONAL, DMA_TO_DEVICE, DMA_FROM_DEVICE, DMA_NONE };
enum hrtimer_mode { HRTIMER_MODE_ABS, HRTIMER_MODE_REL };

/* ---- the recorder (defined by the oracle) ---- */
#define T32K_V	0	/* compared by value */
#define T32K_S	1	/* string: compared by content */
#define T32K_O	2	/* local object of the callee (stack / static key): only "non-NULL" is compared */
u64 t32k_rec(const char *name, int n, const int *kind, const u64 *a);
/* fmt + first four int varargs of a va_format */
u64 t32k_rec_vaf(const char *name, u64 a0, const char *fmt, const struct va_format *vaf);

#define T32K_U(x)	((u64)(uintptr_t)(x))
#define T32K_K0		static const int k_[1] = { 0 }
#define T32K_DEF0(ret, nm) \
	static inline ret t32k_##nm(void) { T32K_K0; return (ret)(uintptr_t)t32k_rec(#nm, 0, k_, 0); }
#define T32K_DEF1(ret, nm, t1) \
	static inline ret t32k_##nm(t1 a) { T32K_K0; u64 v[1] = { T32K_U(a) }; return (ret)(uintptr_t)t32k_rec(#nm, 1, k_, v); }
#define T32K_DEF2(ret, nm, t1, t2) \
	static inline ret t32k_##nm(t1 a, t2 b) { static const int k_[2] = { 0, 0 }; u64 v[2] = { T32K_U(a), T32K_U(b) }; \
		return (ret)(uintptr_t)t32k_rec(#nm, 2, k_, v); }
#define T32K_DEF3(ret, nm, t1, t2, t3) \
	static inline ret t32k_##nm(t1 a, t2 b, t3 c) { static const int k_[3] = { 0 }; u64 v[3] = { T32K_U(a), T32K_U(b), T32K_U(c) }; \
		return (ret)(uintptr_t)t32k_rec(#nm, 3, k_, v); }
#define T32K_DEF4(ret, nm, t1, t2, t3, t4) \
	static inline ret t32k_##nm(t1 a, t2 b, t3 c, t4 d) { static const int k_[4] = { 0 }; \
		u64 v[4] = { T32K_U(a), T32K_U(b), T32K_U(c), T32K_U(d) }; return (ret)(uintptr_t)t32k_rec(#nm, 4, k_, v); }
#define T32K_DEF5(ret, nm, t1, t2, t3, t4, t5) \
	static inline ret t32k_##nm(t1 a, t2 b, t3 c, t4 d, t5 e) { static const int k_[5] = { 0 }; \
		u64 v[5] = { T32K_U(a), T32K_U(b), T32K_U(c), T32K_U(d), T32K_U(e) }; return (ret)(uintptr_t)t32k_rec(#nm, 5, k_, v); }
#define T32K_DEF6(ret, nm, t1, t2, t3, t4, t5, t6) \
	static inline ret t32k_##nm(t1 a, t2 b, t3 c, t4 d, t5 e, t6 f) { static const int k_[6] = { 0 }; \
		u64 v[6] = { T32K_U(a), T32K_U(b), T32K_U(c), T32K_U(d), T32K_U(e), T32K_U(f) }; \
		return (ret)(uintptr_t)t32k_rec(#nm, 6, k_, v); }
/* loff_t / s64 arguments and results go through as u64 */
#define T32K_VOID(x)	((void)(x))

/* ---- out-of-line kernel functions (one hook each) ---- */
T32K_DEF2(struct clk *, clk_get, struct device *, const char *)
T32K_DEF2(struct clk *, devm_clk_get, struct device *, const char *)
T32K_DEF1(int, clk_enable, struct clk *)
T32K_DEF1(int, clk_disable, struct clk *)
T32K_DEF1(int, clk_prepare, struct clk *)
T32K_DEF1(int, clk_unprepare, struct clk *)
T32K_DEF1(unsigned long, clk_get_rate, struct clk *)
T32K_DEF1(int, clk_put, struct clk *)
T32K_DEF2(int, devm_clk_put, struct device *, struct clk *)
T32K_DEF2(int, clk_set_rate, struct clk *, unsigned long)
T32K_DEF3(int, i2c_transfer, struct i2c_adapter *, struct i2c_msg *, int)
T32K_DEF1(int, i2c_del_driver, struct i2c_driver *)
T32K_DEF2(int, i2c_register_driver, struct module *, struct i2c_driver *)
T32K_DEF1(struct i2c_adapter *, i2c_get_adapter, int)
T32K_DEF1(int, i2c_put_adapter, struct i2c_adapter *)
T32K_DEF2(struct i2c_client *, i2c_new_device, struct i2c_adapter *, const struct i2c_board_info *)
T32K_DEF1(int, i2c_unregister_device, struct i2c_client *)
T32K_DEF2(int, gpio_request, unsigned int, const char *)
T32K_DEF1(int, gpio_free, unsigned int)
T32K_DEF1(void *, gpio_to_desc, unsigned int)
T32K_DEF2(int, gpiod_direction_output_raw, void *, int)
T32K_DEF1(int, gpiod_direction_input, void *)
T32K_DEF2(int, gpiod_set_debounce, void *, unsigned int)
T32K_DEF3(int, jzgpio_set_func, int, int, unsigned long)
T32K_DEF1(int, msleep, unsigned int)
T32K_DEF1(int, __udelay, unsigned long)
T32K_DEF1(bool, capable, int)
T32K_DEF1(int, platform_driver_unregister, struct platform_driver *)
T32K_DEF2(int, __platform_driver_register, struct platform_driver *, struct module *)
T32K_DEF1(int, platform_device_register, struct platform_device *)
T32K_DEF1(int, platform_device_unregister, struct platform_device *)
T32K_DEF3(struct resource *, platform_get_resource, struct platform_device *, unsigned int, unsigned int)
T32K_DEF2(int, platform_get_irq, struct platform_device *, unsigned int)
T32K_DEF5(struct resource *, __request_region, struct resource *, resource_size_t, resource_size_t, const char *, int)
T32K_DEF3(int, __release_region, struct resource *, resource_size_t, resource_size_t)
T32K_DEF3(void *, __ioremap, phys_addr_t, unsigned long, unsigned long)
T32K_DEF1(int, __iounmap, const volatile void *)
T32K_DEF6(int, request_threaded_irq, unsigned int, irq_handler_t, irq_handler_t, unsigned long, const char *, void *)
T32K_DEF1(int, enable_irq, unsigned int)
T32K_DEF1(int, disable_irq, unsigned int)
T32K_DEF2(int, free_irq, unsigned int, void *)
T32K_DEF2(int, spin_unlock_irqrestore, spinlock_t *, unsigned long)
T32K_DEF1(int, mutex_lock, struct mutex *)
T32K_DEF1(int, mutex_unlock, struct mutex *)
T32K_DEF3(int, __mutex_init, struct mutex *, const char *, struct lock_class_key *)
T32K_DEF0(unsigned long long, sched_clock)
T32K_DEF1(bool, try_module_get, struct module *)
T32K_DEF1(int, module_put, struct module *)
T32K_DEF1(int, complete, struct completion *)
T32K_DEF1(int, wait_for_completion_interruptible, struct completion *)
T32K_DEF2(unsigned long, wait_for_completion_timeout, struct completion *, unsigned long)
T32K_DEF0(int, schedule)
T32K_DEF4(int, __wake_up, wait_queue_head_t *, unsigned int, int, void *)
T32K_DEF1(int, misc_register, struct miscdevice *)
T32K_DEF1(int, misc_deregister, struct miscdevice *)
T32K_DEF5(struct proc_dir_entry *, proc_create_data, const char *, umode_t, struct proc_dir_entry *,
	  const struct file_operations *, void *)
T32K_DEF1(void *, vmalloc, unsigned long)
T32K_DEF1(int, vfree, const void *)
T32K_DEF2(void *, __kmalloc, size_t, gfp_t)
T32K_DEF1(int, kfree, const void *)
T32K_DEF3(int, memcpy, void *, const void *, size_t)
T32K_DEF3(unsigned long, __copy_user, void *, const void *, unsigned long)
T32K_DEF3(struct file *, filp_open, const char *, int, umode_t)
T32K_DEF2(int, filp_close, struct file *, fl_owner_t)
T32K_DEF4(ssize_t, vfs_read, struct file *, char *, size_t, loff_t *)
T32K_DEF4(ssize_t, vfs_write, struct file *, const char *, size_t, loff_t *)
T32K_DEF3(loff_t, vfs_llseek, struct file *, loff_t, int)
T32K_DEF4(int, dma_cache_sync, struct device *, void *, size_t, enum dma_data_direction)
T32K_DEF1(int, getrawmonotonic64, struct timespec64 *)
T32K_DEF0(bool, kthread_should_stop)
T32K_DEF4(struct task_struct *, kthread_create_on_node, void *, void *, int, const char *)
T32K_DEF1(int, wake_up_process, struct task_struct *)
T32K_DEF1(int, kthread_stop, struct task_struct *)
T32K_DEF4(ssize_t, seq_read, struct file *, char *, size_t, loff_t *)
T32K_DEF3(loff_t, seq_lseek, struct file *, loff_t, int)
T32K_DEF2(int, single_release, struct inode *, struct file *)
T32K_DEF4(int, single_open_size, struct file *, void *, void *, size_t)
T32K_DEF1(struct proc_dir_entry *, jz_proc_mkdir, char *)
T32K_DEF1(int, proc_remove, struct proc_dir_entry *)
T32K_DEF2(int, remove_proc_entry, const char *, struct proc_dir_entry *)
T32K_DEF3(unsigned long long, simple_strtoull, const char *, char **, unsigned int)
T32K_DEF2(int, schedule_hrtimeout, ktime_t *, enum hrtimer_mode)
T32K_DEF3(bool, queue_work_on, int, struct workqueue_struct *, struct work_struct *)
T32K_DEF1(bool, flush_work, struct work_struct *)
T32K_DEF1(int, do_gettimeofday, struct timeval *)

/* hooks with local-object or string arguments */
static inline int t32k___init_waitqueue_head(wait_queue_head_t *q, const char *name, struct lock_class_key *key)
{
	static const int k_[3] = { T32K_V, T32K_S, T32K_O };
	u64 v[3] = { T32K_U(q), T32K_U(name), T32K_U(key) };
	return (int)t32k_rec("__init_waitqueue_head", 3, k_, v);
}
static inline long t32k_prepare_to_wait_event(wait_queue_head_t *q, wait_queue_t *w, int state)
{
	static const int k_[3] = { T32K_V, T32K_O, T32K_V };
	u64 v[3] = { T32K_U(q), T32K_U(w), (u64)state };
	return (long)(int)t32k_rec("prepare_to_wait_event", 3, k_, v);
}
static inline int t32k_finish_wait(wait_queue_head_t *q, wait_queue_t *w)
{
	static const int k_[2] = { T32K_V, T32K_O };
	u64 v[2] = { T32K_U(q), T32K_U(w) };
	return (int)t32k_rec("finish_wait", 2, k_, v);
}

/* ---- kernel objects the inlines touch ---- */
extern u8 t32k_ti[32];		/* thread_info: task at 0, preempt_count at 16, addr_limit at 24 */
extern u8 t32k_iomem_resource[32];
extern u8 __this_module[64];	/* the vendor module object (THIS_MODULE) */
extern u8 t32k_system_wq[8];	/* the pointer value the vendor loads from system_wq */
extern u32 ispmem_base, ispmem_size;	/* kernel reserved ISP memory (32 bit words) */
int t32k_isp_printf(unsigned int level, const char *fmt, ...);
#define isp_printf		t32k_isp_printf
#define t32k_ti_word(off)	(*(u32 *)(t32k_ti + (off)))
#define THIS_MODULE		((struct module *)__this_module)
#define iomem_resource		(*(struct resource *)t32k_iomem_resource)
#define system_wq		(*(struct workqueue_struct **)t32k_system_wq)
#define current			((struct task_struct *)(uintptr_t)t32k_ti_word(0))

/* drvdata slots (vendor 0x0..0x3b8): struct device.driver_data at +76, i2c_client.dev at +32,
 * platform_device.dev at +16.  A slot holds a host pointer. */
#define T32K_SLOT(p, off)		(*(void **)((char *)(p) + (off)))
#define dev_set_drvdata(d, v)		(T32K_SLOT(d, 76) = (v))
#define dev_get_drvdata(d)		T32K_SLOT(d, 76)
#define i2c_set_clientdata(c, v)	(T32K_SLOT(c, 108) = (v))
#define i2c_get_clientdata(c)		T32K_SLOT(c, 108)
#define platform_set_drvdata(p, v)	(T32K_SLOT(p, 92) = (v))
#define platform_get_drvdata(p)		T32K_SLOT(p, 92)

/* ---- names the code uses -> hooks ---- */
#define clk_get			t32k_clk_get
#define devm_clk_get		t32k_devm_clk_get
#define clk_enable		t32k_clk_enable
#define clk_disable(c)		T32K_VOID(t32k_clk_disable(c))
#define clk_prepare		t32k_clk_prepare
#define clk_unprepare(c)	T32K_VOID(t32k_clk_unprepare(c))
#define clk_get_rate		t32k_clk_get_rate
#define clk_put(c)		T32K_VOID(t32k_clk_put(c))
#define devm_clk_put(d, c)	T32K_VOID(t32k_devm_clk_put(d, c))
#define clk_set_rate		t32k_clk_set_rate
#define i2c_transfer		t32k_i2c_transfer
#define i2c_del_driver(d)	T32K_VOID(t32k_i2c_del_driver(d))
#define i2c_register_driver	t32k_i2c_register_driver
#define i2c_add_driver(d)	i2c_register_driver(THIS_MODULE, d)
#define i2c_get_adapter		t32k_i2c_get_adapter
#define i2c_put_adapter(a)	T32K_VOID(t32k_i2c_put_adapter(a))
#define i2c_new_device		t32k_i2c_new_device
#define i2c_unregister_device(c) T32K_VOID(t32k_i2c_unregister_device(c))
#define gpio_request		t32k_gpio_request
#define gpio_free(g)		T32K_VOID(t32k_gpio_free(g))
#define gpio_direction_output(g, v)	t32k_gpiod_direction_output_raw(t32k_gpio_to_desc(g), v)
#define gpio_direction_input(g)		t32k_gpiod_direction_input(t32k_gpio_to_desc(g))
#define gpio_set_debounce(g, d)		t32k_gpiod_set_debounce(t32k_gpio_to_desc(g), d)
#define jzgpio_set_func		t32k_jzgpio_set_func
#define msleep(m)		T32K_VOID(t32k_msleep(m))
static inline void t32k_udelay_fn(unsigned long u) { t32k___udelay(u); }
#define __udelay		t32k_udelay_fn
#define capable			t32k_capable
#define platform_driver_unregister(d)	T32K_VOID(t32k_platform_driver_unregister(d))
#define platform_driver_register(d)	t32k___platform_driver_register(d, THIS_MODULE)
#define platform_device_register	t32k_platform_device_register
#define platform_device_unregister(d)	T32K_VOID(t32k_platform_device_unregister(d))
#define platform_get_resource		t32k_platform_get_resource
#define platform_get_irq		t32k_platform_get_irq
#define request_mem_region(s, n, nm)	t32k___request_region(&iomem_resource, s, n, nm, 0)
#define release_mem_region(s, n)	T32K_VOID(t32k___release_region(&iomem_resource, s, n))
#define ioremap(o, s)			((void __iomem *)t32k___ioremap(o, s, 1024))
#define iounmap(a)			T32K_VOID(t32k___iounmap(a))
#define request_threaded_irq		t32k_request_threaded_irq
#define request_irq(i, h, f, n, d)	t32k_request_threaded_irq(i, h, NULL, f, n, d)
#define enable_irq(i)			T32K_VOID(t32k_enable_irq(i))
#define disable_irq(i)			T32K_VOID(t32k_disable_irq(i))
#define free_irq(i, d)			T32K_VOID(t32k_free_irq(i, d))
#define spin_unlock_irqrestore(l, f)	T32K_VOID(t32k_spin_unlock_irqrestore(l, f))
#define spin_lock_init(l)		((void)(l))
#define mutex_lock(m)			T32K_VOID(t32k_mutex_lock(m))
#define mutex_unlock(m)			T32K_VOID(t32k_mutex_unlock(m))
#define __mutex_init(m, n, k)		T32K_VOID(t32k___mutex_init(m, n, k))
#define sched_clock			t32k_sched_clock
#define try_module_get			t32k_try_module_get
#define module_put(m)			T32K_VOID(t32k_module_put(m))
#define init_waitqueue_head(q) \
	do { static struct lock_class_key __key; t32k___init_waitqueue_head((q), #q, &__key); } while (0)
#define init_completion(x)		do { (x)->done = 0; init_waitqueue_head(&x->wait); } while (0)
#define complete(x)			T32K_VOID(t32k_complete(x))
#define wait_for_completion_interruptible	t32k_wait_for_completion_interruptible
#define wait_for_completion_timeout	t32k_wait_for_completion_timeout
#define TASK_NORMAL			3
#define TASK_INTERRUPTIBLE		1
#define wake_up(q)			T32K_VOID(t32k___wake_up(q, TASK_NORMAL, 1, NULL))
#define wake_up_all(q)			T32K_VOID(t32k___wake_up(q, TASK_NORMAL, 0, NULL))
#define schedule()			T32K_VOID(t32k_schedule())
/* 4.4 ___wait_event, non-exclusive, TASK_INTERRUPTIBLE (vendor 0x6d4) */
#define wait_event_interruptible(wq, condition) ({					\
	int __ret = 0;									\
	if (!(condition)) {								\
		wait_queue_t __wait;							\
		memset(&__wait, 0, sizeof(__wait));					\
		for (;;) {								\
			long __int = t32k_prepare_to_wait_event(&(wq), &__wait, TASK_INTERRUPTIBLE); \
			if (condition)							\
				break;							\
			if (__int) {							\
				__ret = (int)__int;					\
				break;							\
			}								\
			schedule();							\
		}									\
		t32k_finish_wait(&(wq), &__wait);					\
	}										\
	__ret; })
#define misc_register			t32k_misc_register
#define misc_deregister(m)		T32K_VOID(t32k_misc_deregister(m))
#define proc_create_data		t32k_proc_create_data
#define vmalloc				t32k_vmalloc
#define vfree(p)			T32K_VOID(t32k_vfree(p))
#define GFP_KERNEL			0x24000c0u
#define PAGE_SIZE			4096u
#define kmalloc(s, f)			t32k___kmalloc(s, f)
#define kfree(p)			T32K_VOID(t32k_kfree(p))
#define VMALLOC_START			0xc0000000u
#define VMALLOC_END			0xfdffe000u
static inline bool is_vmalloc_addr(const void *x)
{
	u32 addr = (u32)(uintptr_t)x;

	return addr >= VMALLOC_START && addr < VMALLOC_END;
}
#define memcpy(d, s, n)			T32K_VOID(t32k_memcpy(d, s, n))
#define VERIFY_READ			0
#define VERIFY_WRITE			1
#define get_fs()			((mm_segment_t){ t32k_ti_word(24) })
#define set_fs(x)			(t32k_ti_word(24) = (x).seg)
#define access_ok(type, addr, size) ({ u32 a_ = (u32)(uintptr_t)(addr), s_ = (u32)(size); \
	(t32k_ti_word(24) & (a_ | (a_ + s_) | s_)) == 0; })
#define __copy_from_user(t, f, n)	t32k___copy_user(t, f, n)
#define __copy_to_user(t, f, n)		t32k___copy_user(t, f, n)
#define filp_open			t32k_filp_open
#define filp_close			t32k_filp_close
#define vfs_read			t32k_vfs_read
#define vfs_write			t32k_vfs_write
#define vfs_llseek			t32k_vfs_llseek
#define dma_cache_sync(d, v, s, r)	T32K_VOID(t32k_dma_cache_sync(d, v, s, r))
#define getrawmonotonic(ts) do { struct timespec64 t64_; t32k_getrawmonotonic64(&t64_);	\
	(ts)->tv_sec = (s32)t64_.tv_sec; (ts)->tv_nsec = t64_.tv_nsec; } while (0)
#define kthread_should_stop		t32k_kthread_should_stop
#define NUMA_NO_NODE			(-1)
#define kthread_create(fn, d, fmt)	t32k_kthread_create_on_node((void *)(fn), d, NUMA_NO_NODE, fmt)
#define wake_up_process(k)		T32K_VOID(t32k_wake_up_process(k))
#define kthread_stop			t32k_kthread_stop
#define IS_ERR(p)			((u32)(uintptr_t)(p) >= (u32)-4095)
#define seq_read			t32k_seq_read
#define seq_lseek			t32k_seq_lseek
#define single_release			t32k_single_release
#define single_open_size(f, s, d, n)	t32k_single_open_size(f, (void *)(s), d, n)
#define jz_proc_mkdir			t32k_jz_proc_mkdir
#define proc_remove(d)			T32K_VOID(t32k_proc_remove(d))
#define remove_proc_entry(n, p)		T32K_VOID(t32k_remove_proc_entry(n, p))
#define simple_strtoull			t32k_simple_strtoull
#define NSEC_PER_SEC			1000000000L
static inline ktime_t ktime_set(const long secs, const unsigned long nsecs)
{
	ktime_t k;

	k.tv64 = (s64)(s32)secs * NSEC_PER_SEC + (u32)nsecs;
	return k;
}
#define set_current_state(s)		(*(u32 *)(uintptr_t)t32k_ti_word(0) = (s))
#define schedule_hrtimeout		t32k_schedule_hrtimeout
#define WORK_CPU_UNBOUND		1
#define schedule_work(w)		t32k_queue_work_on(WORK_CPU_UNBOUND, system_wq, w)
#define flush_work			t32k_flush_work
#define do_gettimeofday(tv)		T32K_VOID(t32k_do_gettimeofday(tv))
static inline int t32k_request_module_vaf(bool wait, const char *fmt, const struct va_format *vaf)
{
	return (int)t32k_rec_vaf("__request_module", wait, fmt, vaf);
}
#define __request_module(w, f, vaf)	t32k_request_module_vaf(w, f, vaf)
#define seq_printf(m, f, vaf)		((void)t32k_rec_vaf("seq_printf", T32K_U(m), f, vaf))
static inline int t32k_seq_vprintf(struct seq_file *m, const char *f, va_list args)
{
	struct va_format vaf;
	va_list c;

	va_copy(c, args);
	vaf.fmt = f;
	vaf.va = &c;
	return (int)t32k_rec_vaf("seq_vprintf", T32K_U(m), "", &vaf);
}
#define seq_vprintf			t32k_seq_vprintf
#define __aligned(x)			__attribute__((aligned(x)))
#define current_thread_info()		((void *)0)

#endif
