// SPDX-License-Identifier: GPL-2.0
/*
 * T32 / T33 RAW snapshot, online LDC requests and the offline LDC device
 * (openimp #491).  Behaviour of the vendor modules (T32 2.2.0 tx-isp-t32.ko,
 * T33 2.0.2.1 tx-isp-t33.ko; Ghidra decompilations, no code copied):
 *
 * /dev/tx-isp 0xc004548f {vinum, flag} (IMP_FrameSource_SnapRaw_Start / _End):
 *   ispcore event 0x100000b.  flag != 1: every streaming MSCA channel stops
 *   its output (tisp_msca_*_api_chx_stream_off); flag 1: the address FIFOs
 *   are cleared, every buffer the channel holds goes back to the MSCA and the
 *   channel starts again.  Copy failure -ENOMEM, else 0.
 *
 * /dev/tx-isp 0xc004548e {vinum, count, type, mode, frame size, paddr}:
 *   T33 (tisp_func_wdma_save_file(vinum, 0x10, paddr)): the raw WDMA is set
 *   to format 0x10 (0x20054 = 0x10010, line stride 2 x width in both halves
 *   of 0x20060), the next frame end of the sensor (interrupt bit 32,
 *   tisp_ipc_fra_down_interrupt) writes paddr to 0x20058, the WDMA frame done
 *   (bit 68, tisp_wdma_fra_down_interrupt) completes; the request waits up
 *   to 2000 jiffies and always returns 0 (the vendor prints "snap 1 frame
 *   success" either way).
 *   T32 (VIC event 0x100000a, tx_isp_vic_snap_raw): the request is kept in
 *   the VIC state; each frame start of the sensor's VIC programs the VIC's
 *   MDMA (lib_tisp_vic_set_par / lib_tisp_vic_dma_start: size +0x304,
 *   strides +0x310 / +0x314, address +0x320 / +0x340, control +0x300) for
 *   the next frame of count, at most 4 ahead of the done ones; each MDMA
 *   done (second status bit 0, RAW mode 0) counts, the last completes.  Wait
 *   up to 1000 jiffies: 0, or -1 on timeout or signal.  RAW type 1 is
 *   refused by the vendor's lib_tisp_vic_set_par ("format is not
 *   supported", MDMA not enabled), so it ends in the timeout as there.
 *
 * Online LDC (IMP_ISP_AddSensor of the vendor libimp):
 *   0xc00454b9 (T33 only, tx_isp_ldc_online_probe; the T32 module answers
 *   0): 48-byte block {vinum, func, block size, ..}; for func 2 (HLDC) or 4
 *   (LDC) the LUT memory, ((w + b - 1) / b + 3 & ~1) * ((h + b - 1) / b + 1)
 *   * 4 + word 8 bytes, word 9 = size, word 10 = its physical address and,
 *   unless the top half of word 6 is 1, a second one in word 11 (16 slots;
 *   full: -ENOMEM).  Returns the sensor-size event result (0).
 *   0xc00454ba (core command 0x5000006, tx_ldc_online_init): T33 48-byte
 *   block -> the online LDC attribute of vinum (tisp_online_ldc_set_attr);
 *   T32 24-byte block.  0xc00454bb (T33, 0x5000008, tx_ldc_get_online_param):
 *   88-byte block, word 1 = LDC parameters of the IQ bin present (the 80
 *   bytes from word 2).  With LDC mode 1 (offline, 0xc00454b8) the LDC
 *   answers both with 0 and does nothing (vendor).
 *   The attribute is kept and computed as the vendor's; it is not applied to
 *   the pipeline: the vendor applies it only when the ISP was initialised
 *   with LDC on (stIspInitPar +0x2c), which this module never does yet.
 *
 * /dev/offline_ldc (misc, tx_isp_ldc_probe): open enables the LDC interrupt
 *   (Linux IRQ "isp-m1", INTC 38), release disables it, wakes a waiter and
 *   frees the LUT memory.
 *   T33 ioctls: 0 = LUT memory {.., pa out, size, flags bit 2: allocate},
 *   1 = free it (12 slots), 2 = run one job: 40-byte task -> the LDC block
 *   at ISP 0x27000 (tisp_offline_ldc_set_attr: type 0 LDC with the LUT, 2
 *   rotation; sizes, planes, strides, LUT, block size), start, wait up to
 *   1000 jiffies for the done interrupt (0x27080 bit 0).  Every command
 *   returns 0 (vendor; failures only print).
 *   T32 ioctl 2 only: 28-byte task whose word 6 points at the 1096-byte
 *   parameter block, the T30-style LDC engine at 0x13040000
 *   (tx_isp_t32_ldcattr.h, status 0x68 / ack 0x6c).
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/seq_file.h>
#include <linux/version.h>
#include <asm/io.h>
#include "tx_isp_t32.h"
#include "tx_isp_t32_abi.h"
#include "tx_isp_t32_regs.h"
#include "tx_isp_t32_soc.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 13, 0)
#define reinit_completion(c)	INIT_COMPLETION(*(c))	/* T33 kernel 3.10 */
#endif

#define T32_LDC_VIS	4	/* inputs (vinum) with an online LDC record */

static int ldc_irq = 8 + T32_IRQ_ISP_M1;
module_param(ldc_irq, int, 0444);
MODULE_PARM_DESC(ldc_irq, "Linux IRQ of the LDC (isp-m1, INTC 38)");

/* ------------------------------------------------------------------ */
/* RAW snapshot                                                       */

static DEFINE_MUTEX(snap_lock);
static struct completion snap_done;
static struct {
	u32 req[6];		/* vinum, count, type, mode, frame size, paddr */
	int armed;		/* T33: WDMA done completes; T32: capture on */
	int wdma_vinum;		/* T33: frame end that writes the address, -1 none */
	u32 x, n;		/* T32: frames programmed / done */
	u32 stride;		/* T32: lib_tisp_vic_set_par's stride (kept across calls) */
	u32 snaps, done, timeouts;
} snap = { .wdma_vinum = -1 };

#ifdef TX_ISP_T33
#define T33_WDMA_CFG		0x20054
#define T33_WDMA_ADDR		0x20058
#define T33_WDMA_ADDR2		0x2005c
#define T33_WDMA_STRIDE		0x20060
#define T33_SNAP_FMT		0x10	/* tisp_func_wdma_save_file's format: 16 bits a sample */

/* ISP interrupt: frame end of the sensor (bit 32) and WDMA frame done (bit 68) */
void t32_snap_isp_irq(u32 sta2, u32 sta3)
{
	if ((sta2 & 1) && snap.wdma_vinum == 0) {	/* (vinum + 1) % 1 sensor */
		t32_hw_wr(T33_WDMA_ADDR, snap.req[5]);
		snap.wdma_vinum = -1;
	}
	if ((sta3 & 0x10) && snap.armed) {
		snap.armed = 0;
		snap.done++;
		complete(&snap_done);
	}
}

static long t32_snap_raw(const u32 *req)
{
	u32 w, h;

	if (t32_core_sensor_size(&w, &h))
		w = h = 0;
	isp_printf(1, "snap_param.vimum = %d, snap_param.paddr = 0x%08x\n", req[0], req[5]);
	isp_printf(1, "[ISP][%s, %d]:  =======  vinum = %d, paddr is 0x%08x \n",
		   "tisp_func_wdma_save_file", 653, req[0], req[5]);
	reinit_completion(&snap_done);
	t32_hw_wr(T33_WDMA_CFG, T33_SNAP_FMT | 0x10000);
	t32_hw_wr(T33_WDMA_STRIDE, w << 17 | w << 1);
	memcpy(snap.req, req, sizeof(snap.req));
	snap.armed = 1;
	snap.wdma_vinum = req[0] & 0xff;
	snap.snaps++;
	if (!wait_for_completion_timeout(&snap_done, 2000))
		snap.timeouts++;
	snap.armed = 0;
	snap.wdma_vinum = -1;
	isp_printf(1, "snap 1 frame success !\n");
	return 0;
}
#else
#define T32_VIC_MDMA_CTRL	0x300
#define T32_VIC_MDMA_SIZE	0x304
#define T32_VIC_MDMA_YSTR	0x310
#define T32_VIC_MDMA_UVSTR	0x314
#define T32_VIC_MDMA_ADDR	0x320
#define T32_VIC_MDMA_ADDR2	0x340
#define T32_VIC_DMA_MODE	0x1ac
#define T32_VIC_SLOT_REG	0x20c

/* lib_tisp_vic_set_par (memory mode 3: the given address) + lib_tisp_vic_dma_start, one buffer */
static void t32_snap_mdma(u32 vic, u32 type, u32 addr)
{
	u32 w, h, base = T32_VIC_BASE(vic);

	if (t32_core_sensor_size(&w, &h))
		return;
	if (type == 1)
		isp_printf(1, "[%s, %d]: The format is not supported \n", "lib_tisp_vic_set_par", 666);
	else
		snap.stride = ((type == 0 ? w : type == 2 ? w * 3 / 2 : w * 2) + 7) & ~7U;
	t32_hw_wr(base + T32_VIC_DMA_MODE, 0x4440);	/* no WDR */
	t32_hw_wr(base + T32_VIC_MDMA_SIZE, w << 16 | h);
	t32_hw_wr(base + T32_VIC_MDMA_YSTR, snap.stride);
	t32_hw_wr(base + T32_VIC_MDMA_UVSTR, snap.stride);
	t32_hw_wr(base + T32_VIC_MDMA_ADDR, addr);
	t32_hw_wr(base + T32_VIC_MDMA_ADDR2, addr);
	if (type == 1)
		return;
	t32_hw_wr(base + T32_VIC_MDMA_CTRL, type == 0 ? 0x80010001 : type == 2 ? 0x80010008 : 0x80010000);
}

/* VIC interrupt of VIC ch: frame start (bit 4) and MDMA done (second word bit 0) */
void t32_snap_vic_irq(u32 ch, u32 s, u32 s2)
{
	if (!snap.armed)
		return;
	if (s & 0x10) {
		/* isp_vic_set_dma_enable(vic, slot): the slot is ch + 2 when the VIC's slot bits are set */
		u32 slot = ch + ((t32_hw_rd(T32_VIC_BASE(ch) + T32_VIC_SLOT_REG) & 7) ? 2 : 0);

		if (slot == snap.req[0] && snap.x < snap.req[1] && snap.x - snap.n < 4) {
			t32_snap_mdma(ch, snap.req[2] & 0xff, snap.x * snap.req[4] + snap.req[5]);
			snap.x++;
		}
	}
	if ((s2 & 1) && snap.req[3] == 0) {
		if (++snap.n == snap.req[1]) {
			snap.armed = 0;
			snap.x = 0;
			snap.done++;
			complete(&snap_done);
		}
	}
}

static long t32_snap_raw(const u32 *req)
{
	long r;

	isp_printf(1, "Driver vinum = %d, raw_type = %d, wdr_rawmode = %d, paddr = 0x%08x, snap_cnt = %d, frame_size = %d\n",
		   req[0], req[2], req[3], req[5], req[1], req[4]);
	snap.armed = 0;
	snap.x = 0;
	snap.n = 0;
	memcpy(snap.req, req, sizeof(snap.req));
	reinit_completion(&snap_done);
	snap.snaps++;
	snap.armed = 1;
	r = wait_for_completion_interruptible_timeout(&snap_done, 1000);
	if (r == 0) {
		isp_printf(1, "Wait for completion timeout after %d ms\n", 10000);
		snap.timeouts++;
		r = -1;
	} else if (r < 0) {
		isp_printf(1, "Wait interrupted by signal\n");
		r = -1;
	} else {
		isp_printf(1, "snap %d raw successed! \n", req[1]);
		r = 0;
	}
	snap.armed = 0;
	return r;
}
#endif

long t32_snap_ioctl(unsigned int cmd, unsigned long arg)
{
	u32 b[6];
	long r;

	if (cmd == T32_IOC_SNAP_CTL) {
		if (copy_from_user(b, (void __user *)arg, 8)) {
			isp_printf(2, "Failed to copy from user\n");
			return -ENOMEM;
		}
		t32_fs_snap_pause(b[1] == 1);
		return 0;
	}
	if (copy_from_user(b, (void __user *)arg, sizeof(b))) {
		isp_printf(2, "Failed to copy from user\n");
		return -ENOMEM;
	}
	mutex_lock(&snap_lock);
	r = t32_snap_raw(b);
	mutex_unlock(&snap_lock);
	return r;
}

/* ------------------------------------------------------------------ */
/* online LDC                                                         */

/* tisp_online_ldc_set_attr's per-input record (stLDCAttr, 48 bytes) */
static struct t32_ldc_on {
	u32 valid;
	u32 w0, size, out_size, block, stride, type, angle, lut;
} ldc_on[T32_LDC_VIS];
static u32 ldc_probes, ldc_inits;

void t32_ldc_probe_count(void)
{
	ldc_probes++;
}

#ifdef TX_ISP_T33
/* tx_ldc_online_init: the API block to the online attribute (vendor fields; not applied, see above) */
static void t32_ldc_online_init(const u32 *u)
{
	u32 vin = u[0] & 0xff, func = u[1];
	struct t32_ldc_on *a;

	if (vin >= T32_LDC_VIS)
		return;
	a = &ldc_on[vin];
	a->w0 = 0;
	a->size = u[5];			/* w | h << 16 */
	if (func == 1) {
		a->type = 2;
		a->angle = u[4];
	} else if (func == 2 || func == 4) {
		a->type = func == 2 ? 1 : 0;
		a->block = u[2];
		a->lut = u[7] + u[8];
	}
	a->out_size = a->size;
	if (a->type == 2 && (a->angle & ~2U) == 1)
		a->out_size = (a->size & 0xffff) << 16 | a->size >> 16;
	if (a->type != 2 && a->block)
		a->stride = (((a->size & 0xffff) + a->block - 1) / a->block * 4 + 0xb) & ~7U;
	a->valid = 1;
}

long t32_ldc_online_ioctl(unsigned int cmd, unsigned long arg, u32 ldc_mode)
{
	u32 b[22];

	if (ldc_mode == 1)
		return 0;
	if (cmd == T32_IOC_LDC_ONLINE_INIT) {
		if (copy_from_user(b, (void __user *)arg, 48)) {
			isp_printf(2, "[ %s:%d ] copy from user error\n", "tx_ldc_online_init", 56);
			isp_printf(2, "[ %s:%d ] Failed to ioctl!\n", "ldc_core_ops_ioctl", 207);
			return -EFAULT;
		}
		t32_ldc_online_init(b);
		ldc_inits++;
		return 0;
	}
	/* T32_IOC_LDC_ONLINE_PAR: lib_tisp_ldc_get_par, no IQ-bin LDC parameters here */
	if (copy_from_user(b, (void __user *)arg, 88)) {
		isp_printf(2, "[ %s:%d ] copy from user error\n", "tx_ldc_get_online_param", 111);
		isp_printf(2, "[ %s:%d ] Failed to ioctl!\n", "ldc_core_ops_ioctl", 207);
		return -EFAULT;
	}
	b[1] = 0;
	if (copy_to_user((void __user *)arg, b, 88)) {
		isp_printf(2, "[ %s:%d ] copy from user error\n", "tx_ldc_get_online_param", 125);
		isp_printf(2, "[ %s:%d ] Failed to ioctl!\n", "ldc_core_ops_ioctl", 207);
		return -EFAULT;
	}
	return 0;
}
#else
/* T32: 0x5000006 with a 24-byte block {LDC id, ..}; 0xc00454b9 / bb are not served (0) */
long t32_ldc_online_ioctl(unsigned int cmd, unsigned long arg, u32 ldc_mode)
{
	u32 b[6];

	if (cmd != T32_IOC_LDC_ONLINE_INIT)
		return 0;
	if (copy_from_user(b, (void __user *)arg, sizeof(b))) {
		isp_printf(2, "[ %s:%d ] copy from user error\n", "tx_ldc_online_init", 56);
		return -EFAULT;
	}
	if (b[0] < T32_LDC_VIS) {
		ldc_on[b[0]].w0 = b[0];
		ldc_on[b[0]].size = b[2] & 0xffff;
		ldc_on[b[0]].valid = 1;
	}
	return 0;
}
#endif

/* ------------------------------------------------------------------ */
/* /dev/offline_ldc                                                   */

#ifdef TX_ISP_T33
#define LDC_REG(o)		(0x27000 + (o))
#define LDC_SLOTS		12
static void *ldc_lut[LDC_SLOTS];
#else
#define T32_LDC_PHYS		0x13040000U
#define T32_LDC_PARAM		1096
static void __iomem *ldc_base;
static u8 *ldc_param;		/* the offline state: 20 header bytes + the parameter block */
#endif
static struct completion ldc_done;
static DEFINE_MUTEX(ldc_lock);
static int ldc_irq_on, ldc_irq_ok;
static u32 ldc_jobs, ldc_job_timeouts, ldc_irqs, ldc_errors;

static inline void ldc_wr(u32 off, u32 v)
{
#ifdef TX_ISP_T33
	t32_hw_wr(LDC_REG(off), v);
#else
	writel(v, ldc_base + off);
#endif
}

static inline u32 ldc_rd(u32 off)
{
#ifdef TX_ISP_T33
	return t32_hw_rd(LDC_REG(off));
#else
	return readl(ldc_base + off);
#endif
}

static irqreturn_t t32_ldc_isr(int irq, void *dev)
{
#ifdef TX_ISP_T33
	u32 st = ldc_rd(0x80);		/* tisp_ldc_t33_intp_handle */

	if (!st)
		return IRQ_NONE;
	ldc_wr(0x84, st);
#else
	u32 st = ldc_rd(0x68);		/* lib_tisp_ldc_intp_handle */

	if (!st)
		return IRQ_NONE;
	ldc_wr(0x6c, st);
#endif
	ldc_irqs++;
	if (st & 2)
		ldc_errors++;
	if (st & 1)
		complete(&ldc_done);
	return IRQ_HANDLED;
}

static int t32_ldc_open(struct inode *inode, struct file *file)
{
	init_completion(&ldc_done);
	if (ldc_irq_ok && !ldc_irq_on) {
		enable_irq(ldc_irq);
		ldc_irq_on = 1;
	}
	return 0;
}

static int t32_ldc_release(struct inode *inode, struct file *file)
{
#ifdef TX_ISP_T33
	int i;
#endif

	if (ldc_irq_ok && ldc_irq_on) {
		disable_irq(ldc_irq);
		ldc_irq_on = 0;
	}
	complete(&ldc_done);
#ifdef TX_ISP_T33
	/* the vendor frees the first used slot only; all of them here (no leak) */
	mutex_lock(&ldc_lock);
	for (i = 0; i < LDC_SLOTS; i++) {
		kfree(ldc_lut[i]);
		ldc_lut[i] = NULL;
	}
	mutex_unlock(&ldc_lock);
#endif
	return 0;
}

#ifdef TX_ISP_T33
/* tisp_offline_ldc_set_attr: a[] the vendor's 12-word job record */
static void t33_ldc_set_attr(const u32 *a)
{
	u32 w = a[1] & 0xffff, h = a[1] >> 16, oh = h, ow = w, bs, code;

	ldc_wr(0x88, 0);
	ldc_wr(0x84, 0xffffffff);
	if (a[0] != 1)
		return;
	if (a[2] == 2 && (a[4] & ~2U) == 1) {	/* 90 / 270 degrees: out h x w */
		oh = w;
		ow = h;
	}
	ldc_wr(0x44, a[1]);
	ldc_wr(0x48, oh << 16 | ow);
	ldc_wr(0x40, a[4] << 16 | a[2]);
	ldc_wr(0x50, a[5]);
	ldc_wr(0x4c, a[6]);
	ldc_wr(0x54, a[9]);
	ldc_wr(0x58, a[8]);
	ldc_wr(0x5c, a[7]);
	ldc_wr(0x60, a[5]);
	ldc_wr(0x64, a[10]);
	ldc_wr(0x68, a[8]);
	if (a[2] != 2) {
		bs = a[3];
		ldc_wr(0x6c, a[11]);
		ldc_wr(0x70, bs ? ((bs - 1 + w) / bs * 4 + 0xb) & ~7U : 0);
		code = bs == 8 ? 0 : bs == 16 ? 1 : bs == 32 ? 2 : bs == 64 ? 3 : 4;
		if (code < 4)
			ldc_wr(0x74, code);
		else
			isp_printf(1, "[ISP][%s, %d]: block size error !!! \n", "tisp_offline_ldc_set_attr", 309);
	}
	ldc_wr(0x30, 1);
	ldc_wr(0x20, 0xffffffff);
	ldc_wr(0x24, 1);
}

static long t32_ldc_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	u32 m[5], u[10], a[12];
	int i;

	if (cmd == 0 || cmd == 1) {
		/* {.., pa, size, flags, ..}: flags bit 2 = LUT memory */
		if (copy_from_user(m, (void __user *)arg, sizeof(m))) {
			isp_printf(2, "Failed to copy from user\n");
			return 0;
		}
		if (!(m[3] & 4)) {
			/* no LUT memory asked for: 0 echoes the block, 1 does nothing */
			if (cmd == 0 && copy_to_user((void __user *)arg, m, sizeof(m)))
				isp_printf(2, "Failed to copy from user\n");
			return 0;
		}
		mutex_lock(&ldc_lock);
		if (cmd == 1) {
			void *p;

			if (!(m[1] & 0x7fffffff))
				isp_printf(2, "LUT address error!\n");
			for (i = 0; i < LDC_SLOTS; i++) {
				if (ldc_lut[i] && virt_to_phys(ldc_lut[i]) == m[1]) {
					p = ldc_lut[i];
					ldc_lut[i] = NULL;
					kfree(p);
					break;
				}
			}
			mutex_unlock(&ldc_lock);
			return 0;
		}
		for (i = 0; i < LDC_SLOTS && ldc_lut[i]; i++)
			;
		if (i < LDC_SLOTS && m[2])
			ldc_lut[i] = kmalloc(m[2], GFP_KERNEL);
		if (i == LDC_SLOTS || !m[2] || !ldc_lut[i]) {
			mutex_unlock(&ldc_lock);
			isp_printf(2, "ldc lut mem malloc(%d) failed!\n", m[2]);
			return 0;
		}
		/* no dirty line may later land on what user space writes through /dev/mem */
		dma_cache_wback_inv((unsigned long)ldc_lut[i], m[2]);
		m[1] = virt_to_phys(ldc_lut[i]);
		mutex_unlock(&ldc_lock);
		if (copy_to_user((void __user *)arg, m, sizeof(m)))
			isp_printf(2, "Failed to copy from user\n");
		return 0;
	}
	if (cmd != 2)
		return 0;
	if (copy_from_user(u, (void __user *)arg, sizeof(u))) {
		isp_printf(2, "Failed to copy from user\n");
		return 0;
	}
	/* ldc_offline_unlocked_ioctl: the 40-byte task to the job record */
	memset(a, 0, sizeof(a));
	a[0] = 1;
	a[1] = u[0];			/* w | h << 16 */
	if (u[7] == 1) {		/* I2D: rotation */
		a[2] = 2;
		a[4] = u[9];
	} else if (u[7] == 4) {		/* LDC with a LUT */
		a[2] = 0;
		a[3] = u[8];
		a[11] = u[6];
	} else {
		isp_printf(2, "The ldc func select error!\n");
		return 0;
	}
	a[6] = u[1];			/* Y in */
	a[7] = u[2];			/* UV in */
	a[9] = u[3];			/* Y out */
	a[10] = u[4];			/* UV out */
	a[5] = u[5] & 0xffff;		/* stride in */
	a[8] = u[5] >> 16;		/* stride out */
	mutex_lock(&ldc_lock);
	reinit_completion(&ldc_done);
	ldc_jobs++;
	t33_ldc_set_attr(a);
	if (!wait_for_completion_timeout(&ldc_done, 1000)) {
		ldc_job_timeouts++;
		isp_printf(2, "The LDC module failed in this operation.\n");
	}
	mutex_unlock(&ldc_lock);
	return 0;
}
#else
static long t32_ldc_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	u32 t[7], i;
	const u8 *p;

	if (cmd != 2)
		return 0;
	if (copy_from_user(t, (void __user *)arg, sizeof(t))) {
		isp_printf(2, "Failed to copy from user\n");
		return 0;
	}
	mutex_lock(&ldc_lock);
	/* tisp_offline_ldc_set_attr (tx_isp_t32_ldcattr.h): header, then the user's parameter block */
	if (copy_from_user(ldc_param + 20, (void __user *)(uintptr_t)t[6], T32_LDC_PARAM)) {
		isp_printf(2, "[ %s:%d ] copy from user error\n", "tisp_offline_ldc_set_attr", 410);
		goto wait;
	}
	p = ldc_param + 20;
	*(u16 *)(ldc_param + 0) = t[1] & 0xffff;
	*(u16 *)(ldc_param + 2) = t[1] >> 16;
	memcpy(ldc_param + 4, &t[2], 16);
#define PW(o)	(*(const u32 *)(p + (o)))
	ldc_wr(16, PW(16) << 16 | PW(12));
	ldc_wr(20, PW(32) << 16 | PW(28));
	ldc_wr(28, PW(36) << 16 | PW(40));
	ldc_wr(32, PW(44));
	ldc_wr(36, 0);
	ldc_wr(52, PW(20));
	ldc_wr(60, PW(20));
	ldc_wr(68, PW(24));
	ldc_wr(76, PW(24));
	ldc_wr(80, PW(48));
#undef PW
	for (i = 0; i < 255; i++) {
		ldc_wr(0x1000 + 4 * i, (u32)(int)*(const s16 *)(p + 52 + 2 * i));
		ldc_wr(0x3000 + 4 * i, (u32)(int)*(const s16 *)(p + 564 + 2 * i));
	}
	ldc_wr(48, t[2]);
	ldc_wr(64, t[3]);
	ldc_wr(56, t[4]);
	ldc_wr(72, t[5]);
	ldc_wr(8, 0);
	ldc_wr(112, 0);
	reinit_completion(&ldc_done);
	ldc_jobs++;
	ldc_wr(4, 1);
wait:
	if (!wait_for_completion_timeout(&ldc_done, 1000)) {
		ldc_job_timeouts++;
		isp_printf(2, "The LDC module failed in this operation.\n");
	}
	mutex_unlock(&ldc_lock);
	return 0;
}
#endif

static const struct file_operations t32_ldc_fops = {
	.owner = THIS_MODULE,
	.open = t32_ldc_open,
	.release = t32_ldc_release,
	.unlocked_ioctl = t32_ldc_ioctl,
};

static struct miscdevice t32_ldc_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "offline_ldc",
	.fops = &t32_ldc_fops,
};
static int ldc_misc_ok;

void t32_snapldc_show(struct seq_file *m)
{
	int i;

	seq_printf(m, "snapraw: %u requests, %u done, %u timeouts; offline_ldc: %u jobs, %u timeouts, %u irqs (%u errors)\n",
		   snap.snaps, snap.done, snap.timeouts, ldc_jobs, ldc_job_timeouts, ldc_irqs, ldc_errors);
	seq_printf(m, "ldc online: %u probes, %u attributes\n", ldc_probes, ldc_inits);
	for (i = 0; i < T32_LDC_VIS; i++)
		if (ldc_on[i].valid)
			seq_printf(m, "ldc online VI%d: type %u size %#x out %#x block %u stride %u lut %#x angle %u\n", i,
				   ldc_on[i].type, ldc_on[i].size, ldc_on[i].out_size, ldc_on[i].block, ldc_on[i].stride,
				   ldc_on[i].lut, ldc_on[i].angle);
}

int t32_snapldc_init(void)
{
	init_completion(&snap_done);
	init_completion(&ldc_done);
#ifndef TX_ISP_T33
	ldc_param = kzalloc(20 + T32_LDC_PARAM, GFP_KERNEL);
	ldc_base = ioremap(T32_LDC_PHYS, 0x4000);
	if (!ldc_param || !ldc_base) {
		kfree(ldc_param);
		ldc_param = NULL;
		if (ldc_base)
			iounmap(ldc_base);
		ldc_base = NULL;
		return -ENOMEM;
	}
#endif
	/* tx_isp_request_irq: requested at probe, disabled until the node is opened */
	if (!request_irq(ldc_irq, t32_ldc_isr, IRQF_SHARED, "isp-m1", &t32_ldc_misc)) {
		ldc_irq_ok = 1;
		disable_irq(ldc_irq);
	} else {
		isp_printf(2, "%s[%d] Failed to request irq(%d).\n", "tx_isp_request_irq", 96, ldc_irq);
	}
	if (misc_register(&t32_ldc_misc) < 0)
		isp_printf(2, "Failed to register offline_ldc!\n");
	else
		ldc_misc_ok = 1;
	return 0;
}

void t32_snapldc_exit(void)
{
	if (ldc_misc_ok)
		misc_deregister(&t32_ldc_misc);
	ldc_misc_ok = 0;
	if (ldc_irq_ok) {
		if (!ldc_irq_on)
			enable_irq(ldc_irq);
		free_irq(ldc_irq, &t32_ldc_misc);
	}
	ldc_irq_ok = ldc_irq_on = 0;
#ifdef TX_ISP_T33
	{
		int i;

		for (i = 0; i < LDC_SLOTS; i++) {
			kfree(ldc_lut[i]);
			ldc_lut[i] = NULL;
		}
	}
#else
	if (ldc_base)
		iounmap(ldc_base);
	ldc_base = NULL;
	kfree(ldc_param);
	ldc_param = NULL;
#endif
}
