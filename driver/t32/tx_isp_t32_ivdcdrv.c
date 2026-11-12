// SPDX-License-Identifier: GPL-2.0
/*
 * T32 IVDC direct mode (openimp #349): the live glue of the ported vendor
 * IVDC code (Matt Davis' recovered C; each port checked by its oracle):
 *   tx_isp_t32_ivdc.h       ivdc_core_ops_init, ivdc_enable_irq / disable_irq
 *   tx_isp_t32_ivdc_misc.h  ivdc_misc_unlocked_ioctl (/dev/misc-ivdc)
 *   tx_isp_t32_ivdc_pad.h   ivdc_pad_event_handle (set format, reqbufs,
 *                           stream on / off of frame channel 0)
 *   tx_isp_t32_ivdc_irq.h   ivdc_core_interrupt_service_routine
 *
 * Module parameters as the vendor module's: direct_mode (d9014; 1: the
 * sensor's channel 0 goes to the encoder over the IVDC line buffer),
 * ivdc_mem_line (d9010, 0: half the picture), ivdc_mem_stride (ce230,
 * 3840) and ivdc_threshold_line (d900c).  Direct modes 2..4 (two to four
 * sensors) need several VIC inputs, which this driver does not have: they
 * are refused at load (mode 0).
 *
 * Flow (vendor QEMU reference, openimp audit #301 run-vendor-d1m):
 *  - ISP stream on: ivdc_core_ops_init (0x0 = 1, wait, 0x5c mode word);
 *  - misc-ivdc GET_DIRECT / MEM_LINE / SET_PADDR (the DDR overflow ring);
 *  - framechan0 SET_FRAME_FORMAT: the geometry registers (pad event 2);
 *  - REQBUFS: the ring registers 0x30 / 0x34 / 0x38 (pad event 7);
 *  - FRAME_STREAMON: irq mask 0x40, enable 0x04, 0xec, 0x08 (pad event 3);
 *    the MSCA gets channel 0's IVDC addresses (tx_isp_t32_fs.c);
 *  - each frame start (0x44 bit 17) completes frame channel 0's next
 *    buffer; DQBUF hands it out and keeps it queued (no QBUF needed);
 *  - FRAME_STREAMOFF: pad event 4 (last: 0x0 = 3).
 * The vendor's other pad events (1, 5, 8) forward to the ISP core only;
 * the forward is answered here (0), the MSCA part is tx_isp_t32_fs.c.
 *
 * The ported code keeps the vendor's object layouts: the IVDC subdev and
 * its private data are byte arrays here with the words the ports read
 * (register base, state, pads, irq callbacks).  Register writes of the pad
 * port (t32_g_wr32 on the register base) go to the hardware with writel.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/seq_file.h>

#include "tx_isp_t32.h"
#include "tx_isp_t32_regs.h"
#include "tx_isp_t32_glue.h"

#define T32_IVDC_PHYS		0x13100000U
#define T32_IVDC_MAP		0x1000U
#define T32_CPM_PHYS		0x10000000U

static void __iomem *ivdc_base;

/* register writes of the ported pad code: hardware for the register window, memory else */
static inline void t32_ivdc_wr32(u8 *p, u32 off, u32 v)
{
	if (ivdc_base && p >= (u8 *)ivdc_base && p < (u8 *)ivdc_base + T32_IVDC_MAP)
		writel(v, p + off);
	else
		memcpy(p + off, &v, 4);
}
#define t32_g_wr32 t32_ivdc_wr32

/* user copies of the misc port (before the headers: their hook fields carry
 * the names of the MIPS copy_*_user / udelay macros) */
static u32 ivdc_copy_from_user(u8 *dst, u32 user, u32 n)
{
	return copy_from_user(dst, (const void __user *)(uintptr_t)user, n);
}

static u32 ivdc_copy_to_user(u32 user, const u8 *src, u32 n)
{
	return copy_to_user((void __user *)(uintptr_t)user, src, n);
}

#pragma push_macro("copy_from_user")
#pragma push_macro("copy_to_user")
#pragma push_macro("udelay")
#undef copy_from_user
#undef copy_to_user
#undef udelay
#include "tx_isp_t32_ivdc.h"
#include "tx_isp_t32_ivdc_misc.h"
#include "tx_isp_t32_ivdc_pad.h"
#include "tx_isp_t32_ivdc_irq.h"
#pragma pop_macro("udelay")
#pragma pop_macro("copy_to_user")
#pragma pop_macro("copy_from_user")

extern u32 t32_tx_link_mode;	/* vendor d9014 for the ported tuning code (vertical flip) */

static struct t32_ivdc iv = { .flag = { 1, 1, 1 } };
static struct t32_ivdc_mem ivm = { .stride = 3840 };
static u32 ivdc_threshold_line;		/* d900c */
static u32 ivdc_streams;		/* d9898 */
static u32 ivdc_cnt[T32_IVDCI_COUNTERS];	/* d98bc .. d9930 */
static u32 ivdc_frames, ivdc_misses;

module_param_named(direct_mode, iv.mode, uint, 0444);
MODULE_PARM_DESC(direct_mode, "isp direct mode (1: channel 0 over the IVDC to the encoder)");
module_param_named(ivdc_mem_line, ivm.line, uint, 0444);
MODULE_PARM_DESC(ivdc_mem_line, "ivdc mem line");
module_param_named(ivdc_mem_stride, ivm.stride, uint, 0444);
MODULE_PARM_DESC(ivdc_mem_stride, "ivdc mem stride");
module_param(ivdc_threshold_line, uint, 0444);
MODULE_PARM_DESC(ivdc_threshold_line, "ivdc threshold line");

static int ivdc_irq = 8 + T32_IRQ_IVDC;
module_param(ivdc_irq, int, 0444);
MODULE_PARM_DESC(ivdc_irq, "Linux IRQ of the IVDC (INTC 57)");

/* vendor layouts: subdev (pads +0x110 / +0x114, irq +0x9c..+0xbc, private +0x118) */
static u8 ivdc_sd[0x120] __aligned(4);
static u8 ivdc_priv[512] __aligned(4);	/* registers +284, format +328, state +444, lock / mutex +456 */
static u8 ivdc_pad[T32_IVP_PAD_BYTES] __aligned(4);
static void __iomem *cpm_base;
static DEFINE_SPINLOCK(ivdc_lock);
static DEFINE_MUTEX(ivdc_mutex);
static int ivdc_irq_ok;

/* ---- hooks of the ports ---- */
static void ivdc_os_lock(u8 *l, u32 *flags)
{
	unsigned long f;

	spin_lock_irqsave(&ivdc_lock, f);
	*flags = f;
}

static void ivdc_os_unlock(u8 *l, u32 flags)
{
	spin_unlock_irqrestore(&ivdc_lock, flags);
}

static void ivdc_os_msleep(u32 ms) { msleep(ms); }
static void ivdc_os_udelay(u32 us) { udelay(us); }
static void ivdc_os_nop(void) { }
static void ivdc_os_clk_off(u32 clk) { }

static struct t32_ivdc_os ivdc_os = {	/* .cpm at init */
	.lock = ivdc_os_lock,
	.unlock = ivdc_os_unlock,
	.clka = ivdc_os_nop,
	.clkv = ivdc_os_nop,
	.clk_disable_unprepare = ivdc_os_clk_off,
	.msleep = ivdc_os_msleep,
	.udelay = ivdc_os_udelay,
};

/* the subdev's irq callbacks (+0xb8 / +0xbc), called under the IVDC lock */
static void ivdc_line_on(u8 *irqdev, u32 zero)
{
	if (ivdc_irq_ok)
		enable_irq(ivdc_irq);
}

static void ivdc_line_off(u8 *irqdev, u32 zero)
{
	if (ivdc_irq_ok)
		disable_irq_nosync(ivdc_irq);
}

static const struct t32_ivdc_uos ivdc_uos = {
	.copy_from_user = ivdc_copy_from_user,
	.copy_to_user = ivdc_copy_to_user,
};

/* the pad's forward to the ISP core: the MSCA side is tx_isp_t32_fs.c */
static int ivdc_pad_send(u8 *pad, u32 event, u8 *arg) { return 0; }
static void ivdc_pad_irq_on(u8 *sd) { t32_ivdc_enable_irq(&iv, sd); }
static void ivdc_pad_irq_off(u8 *sd) { t32_ivdc_disable_irq(&iv, sd); }
static void ivdc_mutex_lock(u8 *m) { mutex_lock(&ivdc_mutex); }
static void ivdc_mutex_unlock(u8 *m) { mutex_unlock(&ivdc_mutex); }

static const struct t32_ivdcp_os ivdc_pos = {
	.send = ivdc_pad_send,
	.enable_irq = ivdc_pad_irq_on,
	.disable_irq = ivdc_pad_irq_off,
	.mutex_lock = ivdc_mutex_lock,
	.mutex_unlock = ivdc_mutex_unlock,
	.cf2dc = &iv.flag[0],
	.cf2e0 = &iv.flag[1],
	.cf2e4 = &iv.flag[2],
	.ce230 = &ivm.stride,
	.d900c = &ivdc_threshold_line,
	.d9010 = &ivm.line,
	.d9014 = &iv.mode,
	.d9898 = &ivdc_streams,
	.d98b4 = &iv.d98b4,
	.d98b8 = &ivm.paddr,
};

/* frame start of a sensor: frame channel <sensor>'s next buffer is done */
static void ivdc_isr_fps(u32 ch, u32 *fps) { *fps = t32_core_sensor_fps(); }
static u32 ivdc_isr_zero(u32 ch) { return 0; }

static int ivdc_isr_event(u32 pad, u32 cmd, u8 *data)
{
	if (t32_fs_ivdc_frame(pad / T32_IVDCI_PAD_BYTES))
		ivdc_frames++;
	else
		ivdc_misses++;
	return 0;
}

static const struct t32_ivdci_os ivdc_ios = {
	.get_fps = ivdc_isr_fps,
	.csccr_output = ivdc_isr_zero,
	.full_range = ivdc_isr_zero,
	.send_event = ivdc_isr_event,
};

static irqreturn_t t32_ivdc_isr(int irq, void *dev)
{
	return t32_ivdc_core_interrupt_service_routine(&ivdc_ios, ivdc_cnt, &iv.ioc_flag, ivdc_sd) ?
	       IRQ_HANDLED : IRQ_NONE;
}

/* ---- driver interface ---- */
int t32_ivdc_direct(int ch)
{
	return ivdc_base && ch == 0 && iv.mode == 1;
}

long t32_ivdc_misc_ioctl(unsigned int cmd, unsigned long arg)
{
	long r;

	mutex_lock(&ivdc_mutex);
	r = t32_ivdc_misc_unlocked_ioctl(&iv, &ivm, &ivdc_uos, cmd, (u32)arg);
	if (cmd == T32_IVDC_IOC_SET_MODE && iv.mode > 1) {
		isp_printf(2, "tx-isp-t32: direct_mode %u (several sensors) not supported, 0\n", iv.mode);
		iv.mode = 0;
	}
	t32_tx_link_mode = iv.mode;
	mutex_unlock(&ivdc_mutex);
	return r;
}

/* ISP stream on / off: the IVDC core start (mode word) / stop */
void t32_ivdc_stream(int on)
{
	u32 o = on;

	if (!ivdc_base)
		return;
	if (on)
		iv.d98b4 = 1;	/* ivdc_activate_module: the clocks run (core.c) */
	t32_ivdc_core_ops_init(&iv, ivdc_sd, &o);
}

/* frame channel 0 in direct mode: pad events 2 (116-byte format), 7, 3, 4 */
int t32_ivdc_set_fmt(u8 *fmt)
{
	return t32_ivdc_pad_event_handle(&ivdc_pos, ivdc_pad, T32_IVP_EV_BASE + 1, fmt);
}

int t32_ivdc_reqbufs(u32 count)
{
	u8 rq[20] = { 0 };

	t32_g_wr32(rq, 0, count);
	return t32_ivdc_pad_event_handle(&ivdc_pos, ivdc_pad, T32_IVP_EV_BASE + 6, rq);
}

int t32_ivdc_fs_stream(int on)
{
	return t32_ivdc_pad_event_handle(&ivdc_pos, ivdc_pad, T32_IVP_EV_BASE + (on ? 2 : 3), NULL);
}

void t32_ivdc_show(struct seq_file *m)
{
	seq_printf(m, "ivdc: direct_mode %u mem_line %u stride %u paddr %#x streams %u frames %u misses %u\n",
		   iv.mode, ivm.line, ivm.stride, ivm.paddr, ivdc_streams, ivdc_frames, ivdc_misses);
}

int t32_ivdc_init(void)
{
	int ret;

	if (iv.mode > 1) {
		isp_printf(2, "tx-isp-t32: direct_mode %u (several sensors) not supported, 0\n", iv.mode);
		iv.mode = 0;
	}
	t32_tx_link_mode = iv.mode;
	ivdc_base = ioremap(T32_IVDC_PHYS, T32_IVDC_MAP);
	cpm_base = ioremap(T32_CPM_PHYS, 0x100);
	if (!ivdc_base || !cpm_base) {
		ret = -ENOMEM;
		goto err;
	}
	ivdc_os.cpm = (volatile u8 *)cpm_base;
	iv.os = &ivdc_os;
	iv.started_mode = iv.mode;	/* probe: d9894 = d9014 */
	iv.info_priv = ivdc_priv;
	t32_g_wr32(ivdc_priv, T32_IVDC_PRIV_REGS, T32_WORD(ivdc_base));
	t32_g_wr32(ivdc_priv, T32_IVDC_PRIV_STATE, 2);
	t32_g_wr32(ivdc_sd, T32_IVDC_SD_PRIV, T32_WORD(ivdc_priv));
	t32_g_wr32(ivdc_sd, T32_IVDC_SD_IRQ_EN, T32_WORD(ivdc_line_on));
	t32_g_wr32(ivdc_sd, T32_IVDC_SD_IRQ_DIS, T32_WORD(ivdc_line_off));
	t32_g_wr32(ivdc_sd, T32_IVDCI_SD_PADS, 0);	/* event pad = sensor * 36 */
	t32_g_wr32(ivdc_sd, T32_IVP_SD_PADS, T32_WORD(ivdc_pad));
	t32_g_wr32(ivdc_pad, 0, T32_WORD(ivdc_sd));
	ivdc_pad[4] = 0;
	ivdc_pad[5] = 1;
	t32_g_wr32(ivdc_pad, 32, T32_WORD(ivdc_priv));	/* its frame channel: geometry +332 / +336 */
	irq_set_status_flags(ivdc_irq, IRQ_NOAUTOEN);	/* on at the first stream (ivdc_enable_irq) */
	ret = request_irq(ivdc_irq, t32_ivdc_isr, 0, "ivdc", ivdc_sd);
	if (ret)
		goto err;
	ivdc_irq_ok = 1;
	return 0;
err:
	isp_printf(2, "tx-isp-t32: IVDC init failed (%d)\n", ret);
	if (cpm_base)
		iounmap(cpm_base);
	if (ivdc_base)
		iounmap(ivdc_base);
	cpm_base = ivdc_base = NULL;
	return ret;
}

void t32_ivdc_exit(void)
{
	if (!ivdc_base)
		return;
	if (ivdc_irq_ok) {
		if (t32_g_rd32(ivdc_sd, T32_IVDC_SD_IRQ_ON))
			disable_irq(ivdc_irq);
		irq_clear_status_flags(ivdc_irq, IRQ_NOAUTOEN);
		free_irq(ivdc_irq, ivdc_sd);
		ivdc_irq_ok = 0;
	}
	iounmap(cpm_base);
	iounmap(ivdc_base);
	cpm_base = ivdc_base = NULL;
}
