#ifndef __TX_ISP_KERNEL_COMPAT_H__
#define __TX_ISP_KERNEL_COMPAT_H__

#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0)
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/timekeeping.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <media/v4l2-mediabus.h>

/* Types removed from the in-kernel API but retained in the T31 userspace ABI. */
struct timespec {
	long tv_sec;
	long tv_nsec;
};

#ifndef TX_ISP_VENDOR_TREE
struct timeval {
	long tv_sec;
	long tv_usec;
};
#endif

typedef phys_addr_t phys_t;

/*
 * The vendor 5.15 tree predates the mainline mediabus rename; its V4L2
 * headers only carry MEDIA_BUS_FMT_*. Alias the legacy names the driver
 * uses so the same source builds on both trees.
 */
#ifndef V4L2_MBUS_FMT_UYVY8_1X16
#define V4L2_MBUS_FMT_UYVY8_1_5X8	MEDIA_BUS_FMT_UYVY8_1_5X8
#define V4L2_MBUS_FMT_VYUY8_1_5X8	MEDIA_BUS_FMT_VYUY8_1_5X8
#define V4L2_MBUS_FMT_YUYV8_1_5X8	MEDIA_BUS_FMT_YUYV8_1_5X8
#define V4L2_MBUS_FMT_YVYU8_1_5X8	MEDIA_BUS_FMT_YVYU8_1_5X8
#define V4L2_MBUS_FMT_UYVY8_2X8		MEDIA_BUS_FMT_UYVY8_2X8
#define V4L2_MBUS_FMT_VYUY8_2X8		MEDIA_BUS_FMT_VYUY8_2X8
#define V4L2_MBUS_FMT_YUYV8_2X8		MEDIA_BUS_FMT_YUYV8_2X8
#define V4L2_MBUS_FMT_YVYU8_2X8		MEDIA_BUS_FMT_YVYU8_2X8
#define V4L2_MBUS_FMT_UYVY8_1X16	MEDIA_BUS_FMT_UYVY8_1X16
#define V4L2_MBUS_FMT_VYUY8_1X16	MEDIA_BUS_FMT_VYUY8_1X16
#define V4L2_MBUS_FMT_YUYV8_1X16	MEDIA_BUS_FMT_YUYV8_1X16
#define V4L2_MBUS_FMT_YVYU8_1X16	MEDIA_BUS_FMT_YVYU8_1X16
#define V4L2_MBUS_FMT_YUYV10_2X10	MEDIA_BUS_FMT_YUYV10_2X10
#define V4L2_MBUS_FMT_YVYU10_2X10	MEDIA_BUS_FMT_YVYU10_2X10
#define V4L2_MBUS_FMT_YUYV10_1X20	MEDIA_BUS_FMT_YUYV10_1X20
#define V4L2_MBUS_FMT_YVYU10_1X20	MEDIA_BUS_FMT_YVYU10_1X20
#endif

/* set_fs() disappeared once kernel_read()/kernel_write() became universal.
   The vendor 5.15 tree still provides it (with a struct mm_segment_t), so
   only shim it on mainline. */
#ifndef TX_ISP_VENDOR_TREE
#ifndef KERNEL_DS
#define KERNEL_DS 0UL
#endif
#define get_fs() 0UL
#define set_fs(segment) do { (void)(segment); } while (0)
#else
/* The vendor 5.15 kernel is built without CONFIG_SET_FS: set_fs()/get_fs()
   and KERNEL_DS do not exist and mm_segment_t is an empty struct. The driver
   only wraps kernel_read()/kernel_write() in a pointless set_fs(KERNEL_DS)
   pair, so make them no-ops. */
#ifndef KERNEL_DS
#define KERNEL_DS	((mm_segment_t) { })
#endif
#ifndef get_fs
#define get_fs()	((mm_segment_t) { })
#endif
#ifndef set_fs
#define set_fs(segment)	do { (void)(segment); } while (0)
#endif
#endif
#define vfs_read(file, buf, count, pos) kernel_read((file), (buf), (count), (pos))
#define vfs_write(file, buf, count, pos) kernel_write((file), (buf), (count), (pos))
#define ioremap_nocache(offset, size) ioremap((offset), (size))
#define INIT_COMPLETION(completion) reinit_completion(&(completion))

/* ACCESS_ONCE() was dropped after 5.1; the driver uses it as an lvalue. */
#ifndef ACCESS_ONCE
#define ACCESS_ONCE(x) (*(volatile typeof(x) *)&(x))
#endif

#ifndef TX_ISP_VENDOR_TREE
#ifndef PDE_DATA
#define PDE_DATA(inode) pde_data(inode)
#endif
#endif

static inline void do_gettimeofday(struct timeval *tv)
{
	struct timespec64 now;

	ktime_get_real_ts64(&now);
	tv->tv_sec = (long)now.tv_sec;
	tv->tv_usec = now.tv_nsec / NSEC_PER_USEC;
}

static inline void getrawmonotonic(struct timespec *ts)
{
	struct timespec64 now;

	ktime_get_raw_ts64(&now);
	ts->tv_sec = (long)now.tv_sec;
	ts->tv_nsec = now.tv_nsec;
}
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0)
#define TX_ISP_PROC_OPS struct proc_ops
#define TX_ISP_PROC_OWNER
#define TX_ISP_PROC_OPEN .proc_open
#define TX_ISP_PROC_READ .proc_read
#define TX_ISP_PROC_WRITE .proc_write
#define TX_ISP_PROC_LSEEK .proc_lseek
#define TX_ISP_PROC_RELEASE .proc_release
#else
#define TX_ISP_PROC_OPS struct file_operations
#define TX_ISP_PROC_OWNER .owner = THIS_MODULE,
#define TX_ISP_PROC_OPEN .open
#define TX_ISP_PROC_READ .read
#define TX_ISP_PROC_WRITE .write
#define TX_ISP_PROC_LSEEK .llseek
#define TX_ISP_PROC_RELEASE .release
#endif

/*
 * Size: with TX_ISP_T31_EXPORT_WHITELIST (default, Kbuild) the module exports
 * only the whitelist in tx_isp_t31_exports.h (built by tx_isp_t31_exports.c);
 * every other EXPORT_SYMBOL becomes a plain declaration, so --gc-sections
 * drops the functions nothing else references.
 */
#if defined(TX_ISP_T31_EXPORT_WHITELIST) && !defined(TX_ISP_T31_EXPORTS_C)
#include <linux/export.h>
#undef EXPORT_SYMBOL
#undef EXPORT_SYMBOL_GPL
#define EXPORT_SYMBOL(sym) extern int __tx_isp_t31_unexported_##sym
#define EXPORT_SYMBOL_GPL(sym) extern int __tx_isp_t31_unexported_##sym
#endif

#endif

/* The legacy V4L2_MBUS_FMT_* spellings of the packed-YUV bus formats were
 * dropped from linux/v4l2-mediabus.h; map them onto MEDIA_BUS_FMT_*. */
#ifndef V4L2_MBUS_FMT_UYVY8_2X8
#define V4L2_MBUS_FMT_UYVY8_2X8 MEDIA_BUS_FMT_UYVY8_2X8
#endif
#ifndef V4L2_MBUS_FMT_UYVY8_1_5X8
#define V4L2_MBUS_FMT_UYVY8_1_5X8 MEDIA_BUS_FMT_UYVY8_1_5X8
#endif
#ifndef V4L2_MBUS_FMT_UYVY8_1X16
#define V4L2_MBUS_FMT_UYVY8_1X16 MEDIA_BUS_FMT_UYVY8_1X16
#endif
#ifndef V4L2_MBUS_FMT_VYUY8_2X8
#define V4L2_MBUS_FMT_VYUY8_2X8 MEDIA_BUS_FMT_VYUY8_2X8
#endif
#ifndef V4L2_MBUS_FMT_VYUY8_1_5X8
#define V4L2_MBUS_FMT_VYUY8_1_5X8 MEDIA_BUS_FMT_VYUY8_1_5X8
#endif
#ifndef V4L2_MBUS_FMT_VYUY8_1X16
#define V4L2_MBUS_FMT_VYUY8_1X16 MEDIA_BUS_FMT_VYUY8_1X16
#endif
#ifndef V4L2_MBUS_FMT_YUYV8_2X8
#define V4L2_MBUS_FMT_YUYV8_2X8 MEDIA_BUS_FMT_YUYV8_2X8
#endif
#ifndef V4L2_MBUS_FMT_YUYV8_1_5X8
#define V4L2_MBUS_FMT_YUYV8_1_5X8 MEDIA_BUS_FMT_YUYV8_1_5X8
#endif
#ifndef V4L2_MBUS_FMT_YUYV8_1X16
#define V4L2_MBUS_FMT_YUYV8_1X16 MEDIA_BUS_FMT_YUYV8_1X16
#endif
#ifndef V4L2_MBUS_FMT_YUYV10_2X10
#define V4L2_MBUS_FMT_YUYV10_2X10 MEDIA_BUS_FMT_YUYV10_2X10
#endif
#ifndef V4L2_MBUS_FMT_YUYV10_1X20
#define V4L2_MBUS_FMT_YUYV10_1X20 MEDIA_BUS_FMT_YUYV10_1X20
#endif
#ifndef V4L2_MBUS_FMT_YVYU8_2X8
#define V4L2_MBUS_FMT_YVYU8_2X8 MEDIA_BUS_FMT_YVYU8_2X8
#endif
#ifndef V4L2_MBUS_FMT_YVYU8_1_5X8
#define V4L2_MBUS_FMT_YVYU8_1_5X8 MEDIA_BUS_FMT_YVYU8_1_5X8
#endif
#ifndef V4L2_MBUS_FMT_YVYU8_1X16
#define V4L2_MBUS_FMT_YVYU8_1X16 MEDIA_BUS_FMT_YVYU8_1X16
#endif
#ifndef V4L2_MBUS_FMT_YVYU10_2X10
#define V4L2_MBUS_FMT_YVYU10_2X10 MEDIA_BUS_FMT_YVYU10_2X10
#endif
#ifndef V4L2_MBUS_FMT_YVYU10_1X20
#define V4L2_MBUS_FMT_YVYU10_1X20 MEDIA_BUS_FMT_YVYU10_1X20
#endif

/* strlcpy() was removed from the kernel in 6.8; strscpy() is the replacement.
 * Return value semantics differ (strscpy returns -E2BIG on truncation) but every
 * call site here ignores it. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 8, 0)
#include <linux/string.h>
static inline size_t strlcpy(char *dst, const char *src, size_t size)
{
	int ret = strscpy(dst, src, size);
	return ret < 0 ? size : (size_t)ret;
}
#endif
