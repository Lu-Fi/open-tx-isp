// SPDX-License-Identifier: GPL-2.0
/*
 * T33 custom AE / AWB libraries (IMP_ISP_SetAeAlgoFunc / SetAwbAlgoFunc,
 * openimp #805; beyond the vendor, user OK 2026-11-22).
 *
 * The vendor libimp 2.0.2.1 uses the T40 protocol (cookie 0x013501f4,
 * request { cookie, num, payload }) on /dev/tx-isp:
 *   0xc004541d AE open   (core 0x1000014): out IMPISPAeInitAttr (128 B) at +8
 *   0xc004541e AE close  (core 0x1000015)
 *   0xc004541b AE get    (core 0x1000016): out IMPISPAeInfo (1996 B) at +8
 *   0xc004541c AE set    (core 0x1000017): in IMPISPAeAttr (56 B) at +8
 *   0xc0045425 AWB open  (core 0x1000018): out IMPISPAwbInitAttr (20 B) at +8
 *   0xc0045426 AWB close (core 0x1000019)
 *   0xc0045423 AWB get   (core 0x100001a): out IMPISPAwbInfo (12316 B) at +8
 *   0xc0045424 AWB set   (core 0x100001b): in IMPISPAwbAttr (16 B) at +8
 * The vendor tx-isp-t33.ko sends these core commands to its default forward
 * (ispcore_core_ops_ioctl 0x103680, jump table .rodata +17404 -> 0x103f60)
 * and maps -ENOIOCTLCMD to 0 (0x104124): no algorithm, the get returns at
 * once with zeros, the set has no effect.
 *
 * Here: the get blocks until the next AE / AWB statistics frame of the live
 * per-frame AE / AWB (t33_calgo_ae_tick / _awb_tick at the end of their
 * works) or T33_CALGO_WAIT_MS (then -EAGAIN, the library worker asks again),
 * and fills the info from the live blocks (tx_isp_t32_tunblk.c).  A result
 * goes through the blocks' manual paths (AE exposure info manual mode, AWB
 * manual mode): the per-frame AE / AWB keep collecting statistics, the
 * library chooses the values.  The close restores the state saved before
 * the first result and wakes a waiting get (-EINVAL); the release of the
 * opening file does the same (killed client).  No ioctl here takes the core lock.
 */
#ifdef T33_CALGO_HOST
#include "t33_calgo_host_env.h"	/* tests/t33_calgo_host.c */
#else
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/seq_file.h>
#include <linux/atomic.h>

#include "tx_isp_t32.h"
#endif

#define T33_CALGO_COOKIE	0x013501f4U
#define T33_CALGO_HDR		8U

static unsigned int t33_calgo_wait_ms = 500;
module_param(t33_calgo_wait_ms, uint, 0644);
MODULE_PARM_DESC(t33_calgo_wait_ms, "T33 custom AE / AWB library: longest wait of a get for the next statistics frame (ms)");

struct t33_calgo {
	bool open, applied;
	atomic_t seq;			/* statistics frames since the module load */
	int seen;			/* the frame the last get handed out */
	u8 *info;
	const void *owner;		/* the /dev/tx-isp file that opened the library */
	wait_queue_head_t wq;
	u8 ae_save[41];
	u32 awb_save[3];
	u32 opens, gets, sets, timeouts;
};

static struct t33_calgo ca[2];	/* 0 = AE, 1 = AWB */
static DEFINE_MUTEX(calgo_lock);
static bool calgo_ready;

static const u32 calgo_info_len[2] = { T33_CALGO_AE_INFO, T33_CALGO_AWB_INFO };

static void calgo_init_once(void)
{
	int k;

	if (calgo_ready)
		return;
	for (k = 0; k < 2; k++)
		init_waitqueue_head(&ca[k].wq);
	calgo_ready = true;
}

static void calgo_tick(int k)
{
	if (!READ_ONCE(ca[k].open))
		return;
	atomic_inc(&ca[k].seq);
	wake_up_interruptible(&ca[k].wq);
}

void t33_calgo_ae_tick(void) { calgo_tick(0); }
void t33_calgo_awb_tick(void) { calgo_tick(1); }

/* { cookie, num }: one sensor on the T33 (num 0) */
static int calgo_hdr(unsigned long arg, u32 *hdr)
{
	if (copy_from_user(hdr, (void __user *)arg, T33_CALGO_HDR))
		return -EFAULT;
	if (hdr[0] != T33_CALGO_COOKIE || hdr[1] != 0)
		return -EINVAL;
	return 0;
}

static int calgo_open(int k, const void *file, unsigned long arg)
{
	u8 init[T33_CALGO_AE_INIT];
	u32 hdr[2], n = k ? T33_CALGO_AWB_INIT : T33_CALGO_AE_INIT;
	int ret = calgo_hdr(arg, hdr);

	if (ret)
		return ret;
	if (k) {
		memset(init, 0, sizeof(init));
		init[0] = 1;		/* IMPISPAWBStatisAttr.awb_sta_en */
		init[12] = 32;		/* local.node_h / node_v: the 32 x 32 zone grid */
		init[13] = 32;
	} else {
		t33_tb_calgo_ae_init(0, init);
	}
	mutex_lock(&calgo_lock);
	calgo_init_once();
	if (ca[k].open) {
		mutex_unlock(&calgo_lock);
		return -EBUSY;
	}
	ca[k].info = kmalloc(calgo_info_len[k], GFP_KERNEL);
	if (!ca[k].info) {
		mutex_unlock(&calgo_lock);
		return -ENOMEM;
	}
	if (copy_to_user((u8 __user *)arg + T33_CALGO_HDR, init, n)) {
		kfree(ca[k].info);
		ca[k].info = NULL;
		mutex_unlock(&calgo_lock);
		return -EFAULT;		/* not registered: the library never sees an open */
	}
	ca[k].applied = false;
	ca[k].owner = file;
	ca[k].seen = atomic_read(&ca[k].seq);
	ca[k].opens++;
	WRITE_ONCE(ca[k].open, true);
	mutex_unlock(&calgo_lock);
	isp_printf(1, "tx-isp-t33: custom %s library open\n", k ? "AWB" : "AE");
	return 0;
}

/* calgo_lock held, library open: restore the state before the first result, unregister, wake a waiting get */
static void calgo_drop_locked(int k)
{
	if (ca[k].applied) {
		if (k)
			t33_tb_calgo_awb_restore(0, ca[k].awb_save);
		else
			t33_tb_calgo_ae_restore(0, ca[k].ae_save);
	}
	WRITE_ONCE(ca[k].open, false);
	ca[k].owner = NULL;
	kfree(ca[k].info);
	ca[k].info = NULL;
}

static void calgo_closed(int k, const char *why)
{
	wake_up_interruptible_all(&ca[k].wq);
	isp_printf(1, "tx-isp-t33: custom %s library %s (%u gets, %u sets, %u timeouts)\n",
		   k ? "AWB" : "AE", why, ca[k].gets, ca[k].sets, ca[k].timeouts);
}

static int calgo_close(int k, unsigned long arg)
{
	u32 hdr[2];

	/* the vendor libimp 2.0.2.1 sends an uninitialised cookie here (QEMU val4): only num counts */
	if (copy_from_user(hdr, (void __user *)arg, T33_CALGO_HDR))
		return -EFAULT;
	if (hdr[1] != 0)
		return -EINVAL;
	mutex_lock(&calgo_lock);
	if (!calgo_ready || !ca[k].open) {
		mutex_unlock(&calgo_lock);
		return -EINVAL;
	}
	calgo_drop_locked(k);
	mutex_unlock(&calgo_lock);
	calgo_closed(k, "closed");
	return 0;
}

/*
 * /dev/tx-isp release: a client that dies with a library open (crash, kill -9) unregisters it here, so the
 * AE / AWB return to automatic and the restarted client can open again (as the T41 module, #197)
 */
void t33_calgo_release(const void *file)
{
	int k;

	for (k = 0; k < 2; k++) {
		bool drop;

		mutex_lock(&calgo_lock);
		drop = calgo_ready && ca[k].open && ca[k].owner == file;
		if (drop)
			calgo_drop_locked(k);
		mutex_unlock(&calgo_lock);
		if (drop)
			calgo_closed(k, "released by its client's close");
	}
}

static int calgo_get(int k, unsigned long arg)
{
	u32 hdr[2];
	int seen, ret = calgo_hdr(arg, hdr);
	long w;

	if (ret)
		return ret;
	mutex_lock(&calgo_lock);
	if (!calgo_ready || !ca[k].open) {
		mutex_unlock(&calgo_lock);
		return -EINVAL;
	}
	seen = ca[k].seen;
	mutex_unlock(&calgo_lock);
	w = wait_event_interruptible_timeout(ca[k].wq,
					     atomic_read(&ca[k].seq) != seen || !READ_ONCE(ca[k].open),
					     msecs_to_jiffies(t33_calgo_wait_ms));
	if (w < 0)
		return (int)w;
	mutex_lock(&calgo_lock);
	if (!ca[k].open) {
		ret = -EINVAL;
	} else if (!w) {
		ca[k].timeouts++;
		ret = -EAGAIN;
	} else {
		ca[k].seen = atomic_read(&ca[k].seq);
		ret = k ? t33_tb_calgo_awb_info(0, ca[k].info) : t33_tb_calgo_ae_info(0, ca[k].info);
		if (!ret && copy_to_user((u8 __user *)arg + T33_CALGO_HDR, ca[k].info, calgo_info_len[k]))
			ret = -EFAULT;
		if (!ret)
			ca[k].gets++;
	}
	mutex_unlock(&calgo_lock);
	return ret;
}

static int calgo_set(int k, unsigned long arg)
{
	u32 req[2 + 14];
	u32 n = T33_CALGO_HDR + (k ? T33_CALGO_AWB_ATTR : T33_CALGO_AE_ATTR);
	int ret;

	if (copy_from_user(req, (void __user *)arg, n))
		return -EFAULT;
	if (req[0] != T33_CALGO_COOKIE || req[1] != 0)
		return -EINVAL;
	mutex_lock(&calgo_lock);
	if (!calgo_ready || !ca[k].open) {
		mutex_unlock(&calgo_lock);
		return -EINVAL;
	}
	ret = 0;
	if (req[2]) {		/* change */
		if (k) {
			/* IMPISPAwbAttr { change, r_gain, b_gain, ct }: a zero gain keeps the AWB as it is */
			if (req[3] && req[4])
				ret = t33_tb_calgo_awb_apply(0, req[3], req[4], ca[k].awb_save, !ca[k].applied);
			else
				ret = -EINVAL;
		} else {
			/* IMPISPAeAttr { change, unit, it, again, dgain, isp dgain, ... }: lines (libimp converts us) */
			ret = t33_tb_calgo_ae_apply(0, req + 4, ca[k].ae_save, !ca[k].applied);
		}
		if (!ret) {
			ca[k].applied = true;
			ca[k].sets++;
		}
	}
	mutex_unlock(&calgo_lock);
	return ret;
}

/* /dev/tx-isp: -ENOIOCTLCMD for every other command */
long t33_calgo_ioctl(const void *file, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case T33_IOC_AE_ALGO_OPEN:	return calgo_open(0, file, arg);
	case T33_IOC_AE_ALGO_CLOSE:	return calgo_close(0, arg);
	case T33_IOC_AE_ALGO_GET:	return calgo_get(0, arg);
	case T33_IOC_AE_ALGO_SET:	return calgo_set(0, arg);
	case T33_IOC_AWB_ALGO_OPEN:	return calgo_open(1, file, arg);
	case T33_IOC_AWB_ALGO_CLOSE:	return calgo_close(1, arg);
	case T33_IOC_AWB_ALGO_GET:	return calgo_get(1, arg);
	case T33_IOC_AWB_ALGO_SET:	return calgo_set(1, arg);
	}
	return -ENOIOCTLCMD;
}

void t33_calgo_show(struct seq_file *m)
{
	seq_printf(m, "t33 custom algo: ae open %d opens %u gets %u sets %u timeouts %u, awb open %d opens %u gets %u sets %u timeouts %u\n",
		   ca[0].open, ca[0].opens, ca[0].gets, ca[0].sets, ca[0].timeouts,
		   ca[1].open, ca[1].opens, ca[1].gets, ca[1].sets, ca[1].timeouts);
}
