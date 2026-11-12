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
 *
 * With TX_ISP_IVDC_DIRECT, GET / SET_FRAME_FORMAT copy the vendor's 116-byte format (ported
 * frame_channel_vidioc_get_fmt / set_fmt, tx_isp_t32_fch.h; the size field
 * of the request says 4): type 1, field 4, colorspace 8.
 *
 * IVDC direct mode (#349, direct_mode=1, tx_isp_t32_ivdcdrv.c; built with
 * TX_ISP_IVDC_DIRECT, so far the T33 module only): channel 0
 * goes to the encoder over the IVDC.  Its format sets the IVDC geometry,
 * REQBUFS its DDR ring, stream on / off start and stop it.  The MSCA gets
 * the IVDC addresses (Y 0x80000000, UV 0x81000000, 4096 bytes a line) for
 * each queued buffer and again at each of its done interrupts, as the
 * vendor's; QBUF needs no UV address.  A buffer is done at the IVDC's frame
 * start (its interrupt, 0x44 bit 17) in queue order; DQBUF hands it out and
 * keeps it queued (vendor: the buffers are queued once, a further QBUF of a
 * queued buffer is ignored).
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
#ifdef TX_ISP_IVDC_DIRECT
/* user copies of the ported format ioctls (before the headers: their hook fields
 * carry the names of the MIPS copy_*_user macros) */
static u32 t32_fs_copy_from_user(u8 *dst, u32 user, u32 n)
{
	return copy_from_user(dst, (const void __user *)(uintptr_t)user, n);
}

static u32 t32_fs_copy_to_user(u32 user, const u8 *src, u32 n)
{
	return copy_to_user((void __user *)(uintptr_t)user, src, n);
}

#pragma push_macro("copy_from_user")
#pragma push_macro("copy_to_user")
#undef copy_from_user
#undef copy_to_user
#include "tx_isp_t32_fch.h"
#pragma pop_macro("copy_to_user")
#pragma pop_macro("copy_from_user")
#define T32_FS_DIRECT(f)	((f)->direct)
#else
#define T32_FS_DIRECT(f)	0
#endif

static unsigned int listen_timeout_ms = 2000;
module_param(listen_timeout_ms, uint, 0644);
MODULE_PARM_DESC(listen_timeout_ms, "LISTEN_BUF wait limit");

#define T32_FS_MAX_BUFS		8
#define T32_QBUF_WORDS		24
#ifdef TX_ISP_IVDC_DIRECT
#define T32_IVDC_LINE		4096	/* MSCA line stride of the IVDC addresses */
#endif

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
#ifdef TX_ISP_IVDC_DIRECT
	int direct;			/* IVDC direct mode (#349), set at REQBUFS */
	int ivdc_next;			/* next buffer the IVDC completes */
	u8 vch[T32_FCH_BYTES] __aligned(4);	/* vendor channel: format +0x248, remote / index */
#endif
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
#ifdef TX_ISP_IVDC_DIRECT
		if (f->direct) {
			/* IVDC: no picture here; the MSCA gets the IVDC addresses again */
			while (!t32_hw_msca_pop_done(c, &y, &uv))
				;
			if (f->streaming)
				t32_hw_msca_push(c, T32_IVDC_Y_ADDR(0), T32_IVDC_UV_ADDR(0));
			spin_unlock(&f->lock);
			continue;
		}
#endif
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
#ifdef TX_ISP_IVDC_DIRECT
	if (f->direct)
		t32_hw_msca_push(f->ch, T32_IVDC_Y_ADDR(0), T32_IVDC_UV_ADDR(0));
	else
#endif
		t32_hw_msca_push(f->ch, f->buf[i].w[T32_QBUF_Y_PA_WORD],
				 f->buf[i].w[T32_QBUF_UV_PA_WORD]);
}

#ifdef TX_ISP_IVDC_DIRECT
/*
 * The channel's input crop from its vendor format (SET_FRAME_FORMAT words
 * 13..17: enable, top, left, width, height): MSCA crop position
 * left << 16 | top and size width << 16 | height (T33 list 23).
 */
int t32_fs_crop(int ch, u32 *pos, u32 *size)
{
	const u8 *fmt;

	if (ch < 0 || ch >= T32_MSCA_CHANNELS)
		return 0;
	fmt = fchan[ch].vch + T32_FCH_FMT;
	if (!t32_g_rd32(fmt, 13 * 4))
		return 0;
	*pos = (t32_g_rd32(fmt, 15 * 4) & 0xffff) << 16 | (t32_g_rd32(fmt, 14 * 4) & 0xffff);
	*size = (t32_g_rd32(fmt, 16 * 4) & 0xffff) << 16 | (t32_g_rd32(fmt, 17 * 4) & 0xffff);
	return 1;
}

/* IVDC frame start of sensor ch (its interrupt): the next queued buffer is done */
int t32_fs_ivdc_frame(u32 ch)
{
	struct t32_fchan *f;
	int i, k, hit = 0;

	if (ch >= T32_MSCA_CHANNELS)
		return 0;
	f = &fchan[ch];
	spin_lock(&f->lock);
	for (k = 0; f->direct && f->streaming && k < f->nbufs; k++) {
		i = (f->ivdc_next + k) % f->nbufs;
		if (f->buf[i].state != BUF_HW)
			continue;
		f->buf[i].state = BUF_DONE;
		f->buf[i].seq = f->seq++;
		do_gettimeofday(&f->buf[i].ts);
		f->done[(f->done_rd + f->done_n++) % T32_FS_MAX_BUFS] = i;
		f->frames++;
		f->ivdc_next = (i + 1) % f->nbufs;
		hit = 1;
		break;
	}
	if (!hit)
		f->drops++;
	spin_unlock(&f->lock);
	if (hit)
		wake_up_interruptible(&f->wq);
	return hit;
}
#endif

static int t32_fs_qbuf(struct t32_fchan *f, u32 *w)
{
	unsigned long flags;
	u32 i = w[0];

	if (i >= (u32)f->nbufs)
		return -EINVAL;
	if (!w[T32_QBUF_Y_PA_WORD] || (!T32_FS_DIRECT(f) && !w[T32_QBUF_UV_PA_WORD]) ||
	    !w[T32_QBUF_SIZE_WORD])
		return -EINVAL;
	spin_lock_irqsave(&f->lock, flags);
#ifdef TX_ISP_IVDC_DIRECT
	if (f->direct && f->buf[i].state != BUF_IDLE) {
		/* queued once: the driver keeps it (vendor frame-skip count) */
		spin_unlock_irqrestore(&f->lock, flags);
		w[3] = T32_BUF_FLAG_TS_MONO | T32_BUF_FLAG_QUEUED;
		return 0;
	}
#endif
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
	b->state = T32_FS_DIRECT(f) && f->streaming ? BUF_HW : BUF_IDLE;	/* direct: queued again */
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
	w[22] = T32_FS_DIRECT(f) ? 3 : 0;	/* frame type: 3 = IVDC (vendor; libimp then encodes from the IVDC) */
	return 0;
}

static int t32_fs_streamon(struct t32_fchan *f)
{
	unsigned long flags;
	int i, first = -1;
	u32 in_w, in_h;

	if (t32_core_sensor_size(&in_w, &in_h))
		return -ENODEV;
#ifdef TX_ISP_IVDC_DIRECT
	if (f->streaming)
		return 0;
	if (f->direct) {
		int r = t32_ivdc_fs_stream(1);	/* IVDC irq, enable (pad event 3) */

		if (r)
			return r;
	}
#endif
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
#ifdef TX_ISP_IVDC_DIRECT
		if (f->direct)
			t32_ivdc_fs_stream(0);
#endif
		return -EINVAL;
	}
	f->width = f->buf[first].w[T32_QBUF_SIZE_WORD] & 0xffff;
	f->height = f->buf[first].w[T32_QBUF_SIZE_WORD] >> 16;
#ifdef TX_ISP_IVDC_DIRECT
	f->stride = f->direct ? T32_IVDC_LINE : f->buf[first].w[T32_QBUF_STRIDE_WORD];
	f->ivdc_next = first;
#else
	f->stride = f->buf[first].w[T32_QBUF_STRIDE_WORD];
#endif
	if (!f->width || !f->height || f->width > in_w || f->height > in_h ||
	    f->stride < f->width) {
		/* no upscaling (T23 MSCA rule), and the buffer holds the picture */
		spin_unlock_irqrestore(&f->lock, flags);
		isp_printf(2, "tx-isp-t32: framechan%d %ux%u stride %u refused (input %ux%u)\n",
			   f->ch, f->width, f->height, f->stride, in_w, in_h);
#ifdef TX_ISP_IVDC_DIRECT
		if (f->direct)
			t32_ivdc_fs_stream(0);
#endif
		return -EINVAL;
	}
	for (i = 0; i < f->nbufs; i++)
		if (f->buf[i].state == BUF_QUEUED)
			t32_fs_push_locked(f, i);
	t32_hw_msca_start(f->ch, f->width, f->height, f->stride, in_w, in_h);
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
#ifdef TX_ISP_IVDC_DIRECT
	int was;
#endif

	spin_lock_irqsave(&f->lock, flags);
#ifdef TX_ISP_IVDC_DIRECT
	was = f->streaming;
#endif
	if (f->streaming)
		t32_hw_msca_stop(f->ch);
	f->streaming = 0;
	for (i = 0; i < f->nbufs; i++)
		f->buf[i].state = BUF_IDLE;
	f->done_n = 0;
	f->done_rd = 0;
	spin_unlock_irqrestore(&f->lock, flags);
#ifdef TX_ISP_IVDC_DIRECT
	if (was && f->direct)
		t32_ivdc_fs_stream(0);	/* pad event 4: irq off, last: 0x0 = 3 */
#endif
	wake_up_interruptible(&f->wq);
}

#ifdef TX_ISP_IVDC_DIRECT
/* ---- vendor format ioctls (tx_isp_t32_fch.h) ---- */

/* the frame channel's remote (vendor: the ISP core, or the IVDC for channel 0 in direct mode) */
static int t32_fs_remote_event(u32 ch, u32 cmd, u8 *data)
{
	if (cmd == T32_EV_SET_FMT && t32_ivdc_direct(ch))
		return t32_ivdc_set_fmt(data);
	return 0;
}

static const u32 t32_fs_zero[T32_MSCA_CHANNELS];
static u32 t32_fs_d9b74[T32_MSCA_CHANNELS];

static const struct t32_fch_os t32_fs_fch_os = {
	.copy_from_user = t32_fs_copy_from_user,
	.copy_to_user = t32_fs_copy_to_user,
	.send_event = t32_fs_remote_event,
	.d9008 = &t32_fs_zero[0],
	.d9004 = &t32_fs_zero[0],
};
#endif

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
#ifdef TX_ISP_IVDC_DIRECT
	case T32_IOC_FS_SET_FORMAT:
		return t32_frame_channel_vidioc_set_fmt(&t32_fs_fch_os, t32_fs_zero, t32_fs_d9b74,
							f->vch, (u32)arg);
	case T32_IOC_FS_GET_FORMAT:
		return t32_frame_channel_vidioc_get_fmt(&t32_fs_fch_os, f->vch, (u32)arg);
#else
	case T32_IOC_FS_SET_FORMAT:
	case T32_IOC_FS_GET_FORMAT:
		/* T32 libimp passes an int here; the geometry comes with QBUF */
		return copy_from_user(&v, up, sizeof(v)) ? -EFAULT : 0;
#endif
	case T32_IOC_FS_REQBUFS:
		if (copy_from_user(rq, up, sizeof(rq)))
			return -EFAULT;
		if (rq[0] > T32_FS_MAX_BUFS)
			rq[0] = T32_FS_MAX_BUFS;
		t32_fs_streamoff(f);
		f->nbufs = rq[0];
		memset(f->buf, 0, sizeof(f->buf));
#ifdef TX_ISP_IVDC_DIRECT
		f->direct = rq[0] && t32_ivdc_direct(f->ch);
		if (f->direct) {
			ret = t32_ivdc_reqbufs(rq[0]);	/* the DDR ring (pad event 7) */
			if (ret) {
				f->nbufs = 0;
				f->direct = 0;
				return ret;
			}
		}
		if (rq[0])
			isp_printf(0, "tx-isp-t32: framechan%d %u buffers%s\n", f->ch, rq[0],
				   f->direct ? " (IVDC direct)" : "");
#else
		if (rq[0])
			isp_printf(0, "tx-isp-t32: framechan%d %u buffers\n", f->ch, rq[0]);
#endif
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
#ifdef TX_ISP_IVDC_DIRECT
	f->direct = 0;
#endif
	return 0;
}

/*
 * misc_open() sets file->private_data to the miscdevice before calling
 * .open; 3.10.14 does that only when the fops have an .open (4.4.94
 * always), so without one ioctl and release see a NULL private_data there.
 */
static int t32_fs_open(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations t32_fs_fops = {
	.owner = THIS_MODULE,
	.open = t32_fs_open,
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
#ifdef TX_ISP_IVDC_DIRECT
		seq_printf(m, "framechan%d: %s%s %ux%u bufs %d frames %u unmatched %u drops %u\n",
			   c, fchan[c].streaming ? "on" : "off", fchan[c].direct ? " ivdc" : "",
			   fchan[c].width, fchan[c].height, fchan[c].nbufs, fchan[c].frames,
			   fchan[c].unmatched, fchan[c].drops);
#else
		seq_printf(m, "framechan%d: %s %ux%u bufs %d frames %u unmatched %u\n",
			   c, fchan[c].streaming ? "on" : "off", fchan[c].width,
			   fchan[c].height, fchan[c].nbufs, fchan[c].frames,
			   fchan[c].unmatched);
#endif
}

int t32_fs_init(void)
{
	int c, ret;

	for (c = 0; c < T32_MSCA_CHANNELS; c++) {
		struct t32_fchan *f = &fchan[c];

		f->ch = c;
#ifdef TX_ISP_IVDC_DIRECT
		t32_g_wr32(f->vch, T32_FCH_REMOTE, c);
		t32_g_wr32(f->vch, T32_FCH_INDEX, c);
#endif
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
