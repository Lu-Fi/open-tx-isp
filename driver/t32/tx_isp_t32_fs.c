// SPDX-License-Identifier: GPL-2.0
/*
 * T32 frame channels (/dev/framechanN): the buffer ioctls of libimp 2.2.0.
 *
 * Buffers are user memory given by physical address (memory 2, "userptr"
 * with a pa): QBUF carries Y pa (word 13), length (14), UV pa (16), stride
 * (17) and the picture size h << 16 | w (19).  The channel geometry comes
 * only from these words on T32.  A queued buffer is pushed to the MSCA
 * channel once the channel streams; the ISP interrupt drains the channel's
 * done FIFO and moves the matching buffer to the done list; LISTEN_BUF waits
 * for one and DQBUF hands it back.  Answers and flags as the vendor driver
 * gives them (S0 r10/r12): QBUF flags 0x2002, DQBUF flags 0x2004, timestamp,
 * fps word (num << 16 | den) at 15, state 2 at 20.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/slab.h>
#include <linux/seq_file.h>

#include "tx_isp_t32.h"
#include "tx_isp_t32_abi.h"
#include "tx_isp_t32_msca.h"

static unsigned int listen_timeout_ms = 2000;
module_param(listen_timeout_ms, uint, 0644);
MODULE_PARM_DESC(listen_timeout_ms, "LISTEN_BUF wait limit");

#define T32_FS_MAX_BUFS		8
#define T32_QBUF_WORDS		24

#define T32_BUF_FLAG_QUEUED	0x0002
#define T32_BUF_FLAG_DONE	0x0004
#define T32_BUF_FLAG_TS_MONO	0x2000

enum { BUF_IDLE, BUF_QUEUED, BUF_HW, BUF_DONE };

struct t32_fbuf {
	u32 w[T32_QBUF_WORDS];		/* last QBUF words (echoed back) */
	int state;
	u32 seq;
	struct timeval ts;
};

struct t32_fchan {
	int ch;
	spinlock_t lock;
	wait_queue_head_t wq;
	int nbufs;
	int streaming;
	struct t32_fbuf buf[T32_FS_MAX_BUFS];
	int done[T32_FS_MAX_BUFS];	/* FIFO of done indices */
	int done_rd, done_n;
	u32 seq;
	u32 width, height, stride;
	u32 frames, drops, unmatched;
	struct miscdevice misc;
	char name[12];
};

static struct t32_fchan fchan[T32_MSCA_CHANNELS];

/* ISP interrupt: channel done bits */
void t32_fs_done_irq(u32 done)
{
	int c;

	for (c = 0; c < T32_MSCA_CHANNELS; c++) {
		struct t32_fchan *f = &fchan[c];
		u32 y, uv;
		int i, woke = 0;

		if (!(done & (1U << c)))
			continue;
		spin_lock(&f->lock);
		while (!t32_hw_msca_pop_done(c, &y, &uv)) {
			for (i = 0; i < f->nbufs; i++)
				if (f->buf[i].state == BUF_HW &&
				    f->buf[i].w[T32_QBUF_Y_PA_WORD] == y)
					break;
			if (i == f->nbufs) {
				f->unmatched++;
				continue;
			}
			f->buf[i].state = BUF_DONE;
			f->buf[i].seq = f->seq++;
			do_gettimeofday(&f->buf[i].ts);
			f->done[(f->done_rd + f->done_n++) % T32_FS_MAX_BUFS] = i;
			f->frames++;
			woke = 1;
		}
		spin_unlock(&f->lock);
		if (woke)
			wake_up_interruptible(&f->wq);
	}
}

static void t32_fs_push_locked(struct t32_fchan *f, int i)
{
	f->buf[i].state = BUF_HW;
	t32_hw_msca_push(f->ch, f->buf[i].w[T32_QBUF_Y_PA_WORD],
			 f->buf[i].w[T32_QBUF_UV_PA_WORD]);
}

static int t32_fs_qbuf(struct t32_fchan *f, u32 *w)
{
	unsigned long flags;
	u32 i = w[0];

	if (i >= (u32)f->nbufs)
		return -EINVAL;
	if (!w[T32_QBUF_Y_PA_WORD] || !w[T32_QBUF_UV_PA_WORD] || !w[T32_QBUF_SIZE_WORD])
		return -EINVAL;
	spin_lock_irqsave(&f->lock, flags);
	if (f->buf[i].state == BUF_HW || f->buf[i].state == BUF_DONE) {
		spin_unlock_irqrestore(&f->lock, flags);
		return -EBUSY;
	}
	memcpy(f->buf[i].w, w, sizeof(f->buf[i].w));
	f->buf[i].state = BUF_QUEUED;
	if (f->streaming)
		t32_fs_push_locked(f, i);
	spin_unlock_irqrestore(&f->lock, flags);
	w[3] = T32_BUF_FLAG_TS_MONO | T32_BUF_FLAG_QUEUED;
	return 0;
}

static int t32_fs_dqbuf(struct t32_fchan *f, u32 *w)
{
	struct t32_fbuf *b;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&f->lock, flags);
	if (!f->done_n) {
		spin_unlock_irqrestore(&f->lock, flags);
		return -EAGAIN;
	}
	i = f->done[f->done_rd];
	f->done_rd = (f->done_rd + 1) % T32_FS_MAX_BUFS;
	f->done_n--;
	b = &f->buf[i];
	b->state = BUF_IDLE;
	memcpy(w, b->w, sizeof(b->w));
	spin_unlock_irqrestore(&f->lock, flags);
	w[0] = i;
	w[2] = 0;
	w[3] = T32_BUF_FLAG_TS_MONO | T32_BUF_FLAG_DONE;
	w[5] = b->ts.tv_sec;
	w[6] = b->ts.tv_usec;
	w[11] = b->seq;
	w[15] = t32_core_sensor_fps();
	w[20] = 2;
	w[22] = 0;
	return 0;
}

static int t32_fs_streamon(struct t32_fchan *f)
{
	unsigned long flags;
	int i, first = -1;

	spin_lock_irqsave(&f->lock, flags);
	if (f->streaming) {
		spin_unlock_irqrestore(&f->lock, flags);
		return 0;
	}
	for (i = 0; i < f->nbufs; i++)
		if (f->buf[i].state == BUF_QUEUED) {
			first = i;
			break;
		}
	if (first < 0) {
		spin_unlock_irqrestore(&f->lock, flags);
		return -EINVAL;
	}
	f->width = f->buf[first].w[T32_QBUF_SIZE_WORD] & 0xffff;
	f->height = f->buf[first].w[T32_QBUF_SIZE_WORD] >> 16;
	f->stride = f->buf[first].w[T32_QBUF_STRIDE_WORD];
	for (i = 0; i < f->nbufs; i++)
		if (f->buf[i].state == BUF_QUEUED)
			t32_fs_push_locked(f, i);
	t32_hw_msca_start(f->ch, f->width, f->height, f->stride);
	f->streaming = 1;
	spin_unlock_irqrestore(&f->lock, flags);
	isp_printf(0, "tx-isp-t32: framechan%d on %ux%u stride %u\n", f->ch,
		   f->width, f->height, f->stride);
	return 0;
}

static void t32_fs_streamoff(struct t32_fchan *f)
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&f->lock, flags);
	if (f->streaming)
		t32_hw_msca_stop(f->ch);
	f->streaming = 0;
	for (i = 0; i < f->nbufs; i++)
		f->buf[i].state = BUF_IDLE;
	f->done_n = 0;
	f->done_rd = 0;
	spin_unlock_irqrestore(&f->lock, flags);
	wake_up_interruptible(&f->wq);
}

static int t32_fs_listen(struct t32_fchan *f, u32 *count)
{
	long left;

	left = wait_event_interruptible_timeout(f->wq, f->done_n || !f->streaming,
						msecs_to_jiffies(listen_timeout_ms));
	if (left < 0)
		return left;
	*count = f->done_n;
	return 0;
}

static long t32_fs_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct t32_fchan *f = container_of(file->private_data, struct t32_fchan, misc);
	void __user *up = (void __user *)arg;
	u32 w[T32_QBUF_WORDS], v, rq[5];
	long ret = 0;

	switch (cmd) {
	case T32_IOC_FS_SET_FORMAT:
	case T32_IOC_FS_GET_FORMAT:
		/* T32 libimp passes an int here; the geometry comes with QBUF */
		return copy_from_user(&v, up, sizeof(v)) ? -EFAULT : 0;
	case T32_IOC_FS_REQBUFS:
		if (copy_from_user(rq, up, sizeof(rq)))
			return -EFAULT;
		if (rq[0] > T32_FS_MAX_BUFS)
			rq[0] = T32_FS_MAX_BUFS;
		t32_fs_streamoff(f);
		f->nbufs = rq[0];
		memset(f->buf, 0, sizeof(f->buf));
		if (rq[0])
			isp_printf(0, "tx-isp-t32: framechan%d %u buffers\n", f->ch, rq[0]);
		return copy_to_user(up, rq, sizeof(rq)) ? -EFAULT : 0;
	case T32_IOC_FS_QBUF:
		if (copy_from_user(w, up, sizeof(w)))
			return -EFAULT;
		ret = t32_fs_qbuf(f, w);
		if (!ret && copy_to_user(up, w, sizeof(w)))
			ret = -EFAULT;
		return ret;
	case T32_IOC_FS_DQBUF:
		if (copy_from_user(w, up, sizeof(w)))
			return -EFAULT;
		ret = t32_fs_dqbuf(f, w);
		if (!ret && copy_to_user(up, w, sizeof(w)))
			ret = -EFAULT;
		return ret;
	case T32_IOC_FS_STREAMON:
		if (copy_from_user(&v, up, sizeof(v)))
			return -EFAULT;
		return t32_fs_streamon(f);
	case T32_IOC_FS_STREAMOFF:
		if (copy_from_user(&v, up, sizeof(v)))
			return -EFAULT;
		t32_fs_streamoff(f);
		return 0;
	case T32_IOC_FS_LISTEN_BUF:
		ret = t32_fs_listen(f, &v);
		if (!ret && copy_to_user(up, &v, sizeof(v)))
			ret = -EFAULT;
		return ret;
	case T32_IOC_FS_SET_BANKS:
		return copy_from_user(&v, up, sizeof(v)) ? -EFAULT : 0;
	}
	isp_printf(1, "tx-isp-t32: framechan%d ioctl %#x not handled\n", f->ch, cmd);
	return -ENOTTY;
}

static int t32_fs_release(struct inode *inode, struct file *file)
{
	struct t32_fchan *f = container_of(file->private_data, struct t32_fchan, misc);

	/* the process is gone: its buffers are no target for the MSCA any more */
	t32_fs_streamoff(f);
	f->nbufs = 0;
	return 0;
}

static const struct file_operations t32_fs_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = t32_fs_ioctl,
	.release = t32_fs_release,
};

void t32_fs_stop_all(void)
{
	int c;

	for (c = 0; c < T32_MSCA_CHANNELS; c++)
		t32_fs_streamoff(&fchan[c]);
}

void t32_fs_show(struct seq_file *m)
{
	int c;

	for (c = 0; c < T32_MSCA_CHANNELS; c++)
		seq_printf(m, "framechan%d: %s %ux%u bufs %d frames %u unmatched %u\n",
			   c, fchan[c].streaming ? "on" : "off", fchan[c].width,
			   fchan[c].height, fchan[c].nbufs, fchan[c].frames,
			   fchan[c].unmatched);
}

int t32_fs_init(void)
{
	int c, ret;

	for (c = 0; c < T32_MSCA_CHANNELS; c++) {
		struct t32_fchan *f = &fchan[c];

		f->ch = c;
		spin_lock_init(&f->lock);
		init_waitqueue_head(&f->wq);
		snprintf(f->name, sizeof(f->name), "framechan%d", c);
		f->misc.minor = MISC_DYNAMIC_MINOR;
		f->misc.name = f->name;
		f->misc.fops = &t32_fs_fops;
		ret = misc_register(&f->misc);
		if (ret) {
			while (--c >= 0)
				misc_deregister(&fchan[c].misc);
			return ret;
		}
	}
	return 0;
}

void t32_fs_exit(void)
{
	int c;

	t32_fs_stop_all();
	for (c = 0; c < T32_MSCA_CHANNELS; c++)
		misc_deregister(&fchan[c].misc);
}
