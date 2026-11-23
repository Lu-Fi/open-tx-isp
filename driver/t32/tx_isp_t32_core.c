// SPDX-License-Identifier: GPL-2.0
/*
 * T32 (PRJ007) TX-ISP driver, S1 skeleton.
 *
 * Scope of S1: the device nodes and the ioctl answers vendor libimp 2.2.0
 * needs to get through IMP_ISP_Open, IMP_ISP_AddSensor and
 * IMP_ISP_EnableSensor with the same ioctl sequence as the vendor driver
 * (reference: QEMU S0 runs, audit/T32_S0_1025.md), sensor registration
 * through the prebuilt T32 sensor modules, and the sensor's own stream-on.
 * Not in S1: the ISP pipeline itself (VIC/CSI/MSCA programming, frames,
 * tisp/3A, IQ bin parsing).  Those ioctls are answered so libimp proceeds,
 * and logged; nothing is written to the ISP yet.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/gfp.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/delay.h>

#include "tx_isp_t32.h"
#include "tx_isp_t32_abi.h"
#include "tx_isp_t32_abi_attr.h"
#include "tx_isp_t32_regs.h"
#ifdef TX_ISP_T33
#include "tx_isp_t33_clkp.h"
#endif
#include "tx_isp_t32_mem.h"
#ifdef TX_ISP_T33
#include "tx_isp_t33_mem.h"
#endif
#include "tx_isp_t32_msca.h"
#include "tx_isp_t32_soc.h"
#ifndef TX_ISP_T33
#include "tx_isp_t32_front.h"	/* t32_top_ipc_get_mem_size / _set_mem_cfg (#759) */

static int isp_cache_line;	/* module parameter (defined with the others below) */
#endif

static unsigned int isp_memopt = T32_MEMOPT_DEFAULT;
module_param(isp_memopt, uint, 0444);
MODULE_PARM_DESC(isp_memopt, "MDNS memory ratio in 1/1000 (<=1000 uncompressed, max 2000)");

/*
 * DRIVER_VERSION: libimp passes a 64-byte buffer (soc name at 0, version at
 * 32) despite the int-sized ioctl and refuses IMP_ISP_Open unless both match
 * its own strings (libimp 2.2.0: "PRJ007" / "20251107a").
 */
static char *driver_soc = T32_SOC_NAME;
module_param(driver_soc, charp, 0444);
MODULE_PARM_DESC(driver_soc, "SoC name answered to DRIVER_VERSION");
static char *driver_version = "20251107a";
module_param(driver_version, charp, 0444);
MODULE_PARM_DESC(driver_version, "ISP driver version answered to DRIVER_VERSION (must match libimp)");
#ifdef TX_ISP_T33
#include "tx_isp_t33_drvver.h"
#ifdef TX_ISP_T33
#include "tx_isp_t33_tisp.h"
#endif
static int t33_put_user_block(void *dst, const void *src, u32 n)	/* private_copy_to_user */
{
	return copy_to_user((void __user *)dst, src, n) ? 1 : 0;
}
#endif

/*
 * DEFAULT_BIN: libimp passes an 880-byte buffer (zeroed) and checks two
 * strings the driver puts into it against the IQ bin it is about to load:
 * the bin format version at 863 (bin bytes 0-7, "1.00") and the SoC at 871
 * (bin bytes 152.., "PRJ007").  Like the vendor (tx_isp_default_bin) the
 * buffer comes back as libimp sent it with these fields filled: WDR (word
 * 852) and day / night (word 856) of the bin last set with SET_TIZIANO_PAR
 * for the VI in byte 862 (IMP_ISP_Tuning_GetISPRunningMode reads 856), the
 * fast-boot "first bin" flag at 860 (0: no kernel fast-boot bins).  The
 * mode is kept over RELEASE_SENSOR, as the vendor module does (#303).
 */
/* T32_DEFAULT_BIN_SIZE / _VER / _SOC: tx_isp_t32_soc.h (T33: same 880 / 863 / 871) */
#define T32_DEFAULT_BIN_WDR	852
#define T32_DEFAULT_BIN_DN	856
#define T32_DEFAULT_BIN_FIRST	860
#define T32_DEFAULT_BIN_VI	862
#define T32_BIN_VIS		4	/* VI numbers whose bin mode is kept */

/* SET_TIZIANO_PAR: 20 bytes (the request's size field says 4) */
struct t32_tiziano_par {
	u32 pa;
	u32 size;
	u32 wdr;
	u32 night;
	u8 update_csccr;
	u8 vinum;
	u8 pad[2];
};
static char *bin_version = "1.00";
module_param(bin_version, charp, 0444);
MODULE_PARM_DESC(bin_version, "IQ bin format version the driver accepts");

static int t32_trace = 1;
module_param(t32_trace, int, 0644);
MODULE_PARM_DESC(t32_trace, "log every ioctl (S1 bring-up)");

#define T32_MAX_VI	1

struct t32_alloc {
	struct list_head node;
	void *kva;
	u32 pa;
	u32 size;
	unsigned int order;
};

struct t32_vi {
	struct i2c_client *client;
	void *sd;			/* struct tx_isp_sensor (sd at 0) */
	struct module *owner;
	struct t32_sensor_reginfo info;
	int registered;
	int input_on;			/* S_INPUT enable */
	int streaming;
	u32 width, height;
	u32 isp_mem_pa, isp_mem_size;
	u32 ipc_mem_pa, ipc_mem_size;
#ifndef TX_ISP_T33
	u32 top_ipc[19];		/* T32: stMemInfo words of tisp_top_ipc_* (9 = base, 13 = size; #759) */
#endif
};

static struct t32_core {
	struct mutex lock;
	struct t32_vi vi[T32_MAX_VI];
	struct list_head allocs;
	void *pending_sd;		/* set by tx_isp_subdev_init during probe */
	u32 tiziano_pa;
	u32 default_bin;
	u32 bin_wdr[T32_BIN_VIS], bin_night[T32_BIN_VIS];	/* last SET_TIZIANO_PAR */
	u32 bypass[T32_BIN_VIS];	/* BYPASS_SENSOR flag per input */
	unsigned int ioctls;
	u32 run_mode;			/* RUN_SWITCH 0 normal / 1 one-shot (AOV) */
	u32 ldc_mode;			/* LDC_MODE 0 online / 1 offline */
	struct proc_dir_entry *proc;
} core;

/* ------------------------------------------------------------------ */
/* subdevices: the sensor modules call these from their i2c probe     */

static int t32_subdev_notify(void *module, unsigned int what, void *data)
{
	/* module is &sd->module, i.e. sd itself */
	if (what == T32_EVENT_SYNC_SENSOR_ATTR && data) {
		u32 w = T32_AT(data, T32_VIDEO_MBUS_WIDTH, u32);
		u32 h = T32_AT(data, T32_VIDEO_MBUS_HEIGHT, u32);

		isp_printf(0, "tx-isp-t32: sensor attr sync %ux%u\n", w, h);
#ifdef TX_ISP_T33
		t33_aec_sensor_sync();	/* lib_tisp_sync_sensor_attr's AE part (openimp #574) */
#else
		t32_aec_sensor_sync(data);	/* ispcore_sync_sensor_attr -> lib_tisp_sync_sensor_attr (openimp #724) */
#endif
		return 0;
	}
	isp_printf(0, "tx-isp-t32: subdev notify %#x\n", what);
	return 0;
}

int tx_isp_subdev_init(struct platform_device *pdev, void *sd, void *ops)
{
	if (!sd)
		return -EINVAL;
	T32_AT(sd, T32_SD_OPS, void *) = ops;
	T32_AT(sd, T32_SD_MODULE_NAME, const char *) =
		pdev && pdev->name ? pdev->name : "sensor";
	T32_AT(sd, T32_SD_MODULE_NOTIFY, void *) = (void *)t32_subdev_notify;
	T32_AT(sd, T32_SD_MODULE_PARENT, void *) = &core;
	core.pending_sd = sd;
	return 0;
}
EXPORT_SYMBOL(tx_isp_subdev_init);

void tx_isp_subdev_deinit(void *sd)
{
	int i;

	if (!sd)
		return;
	/*
	 * No core.lock here: the sensor's i2c remove runs this from inside
	 * RELEASE_SENSOR and module exit, which hold the lock.
	 */
	for (i = 0; i < T32_MAX_VI; i++)
		if (core.vi[i].sd == sd) {
			core.vi[i].sd = NULL;
			core.vi[i].registered = 0;
		}
	if (core.pending_sd == sd)
		core.pending_sd = NULL;
	T32_AT(sd, T32_SD_OPS, void *) = NULL;
	T32_AT(sd, T32_SD_MODULE_NOTIFY, void *) = NULL;
}
EXPORT_SYMBOL(tx_isp_subdev_deinit);

/* the sensor module is going away: forget every reference into it */
void t32_core_sensor_driver_gone(struct i2c_driver *drv)
{
	int i, gone = 0;

	mutex_lock(&core.lock);
	for (i = 0; i < T32_MAX_VI; i++) {
		struct t32_vi *vi = &core.vi[i];

		if (vi->client && vi->client->dev.driver == &drv->driver) {
			vi->sd = NULL;
			vi->registered = 0;
			vi->streaming = 0;
			gone = 1;
		}
	}
	if (gone)
		t32_ivdc_unlive();	/* #828: no STREAMOFF will come for this sensor */
	mutex_unlock(&core.lock);
}

typedef int (*t32_sd_fn)(void *sd, void *arg);

/* ops->group->fn of a sensor subdevice, or NULL */
static t32_sd_fn t32_sd_op(void *sd, unsigned int group, unsigned int fn)
{
	void *ops, *grp;

	if (!sd)
		return NULL;
	ops = T32_AT(sd, T32_SD_OPS, void *);
	if (!ops)
		return NULL;
	grp = T32_AT(ops, group, void *);
	if (!grp)
		return NULL;
	return T32_AT(grp, fn, t32_sd_fn);
}

/* ------------------------------------------------------------------ */
/* sensor registration                                                */

static void t32_vi_release(struct t32_vi *vi)
{
	if (vi->sd && vi->registered)
		tx_isp_sinfo_sensor_unbind(vi->sd, vi->owner);
	if (vi->client)
		i2c_unregister_device(vi->client);
	memset(vi, 0, sizeof(*vi));
}

static int t32_register_sensor(struct t32_vi *vi, const struct t32_sensor_reginfo *info)
{
	struct i2c_board_info bi;
	struct i2c_adapter *adap;
	struct i2c_client *client;
	char chip[T32_CHIP_IDENT_SIZE];
	t32_sd_fn ident;
	void *sd;
	int ret;

	if (vi->registered) {
		if (!strncmp(vi->info.name, info->name, sizeof(info->name)))
			return 0;
		return -EBUSY;
	}
	if (info->cbus_type != 1) {
		isp_printf(2, "tx-isp-t32: sensor %.32s: only I2C sensors in S1 (cbus %u)\n",
			   info->name, info->cbus_type);
		return -EINVAL;
	}
	adap = i2c_get_adapter(info->i2c.i2c_adapter_id);
	if (!adap) {
		isp_printf(2, "tx-isp-t32: no i2c adapter %d\n", info->i2c.i2c_adapter_id);
		return -ENODEV;
	}
	memset(&bi, 0, sizeof(bi));
	strlcpy(bi.type, info->i2c.type, sizeof(bi.type));
	bi.addr = info->i2c.addr;
	core.pending_sd = NULL;
	client = i2c_new_device(adap, &bi);
	i2c_put_adapter(adap);
	if (!client) {
		isp_printf(2, "tx-isp-t32: cannot add i2c client %s@%#x\n", bi.type, bi.addr);
		return -EBUSY;
	}
	sd = i2c_get_clientdata(client);
	if (!sd || !client->dev.driver) {
		isp_printf(2, "tx-isp-t32: no driver bound to %s (sensor module loaded?)\n", bi.type);
		i2c_unregister_device(client);
		return -ENODEV;
	}
	vi->client = client;
	vi->sd = sd;
	vi->owner = client->dev.driver->owner;
	vi->info = *info;

	/* the sensor reads its wiring from its own copy of the registration */
	memcpy((char *)sd + T32_SENSOR_INFO, info, sizeof(*info));
	T32_AT(sd, T32_SENSOR_VIDEO + T32_VIDEO_INFO, void *) = (char *)sd + T32_SENSOR_INFO;

	ident = t32_sd_op(sd, T32_OPS_CORE, T32_CORE_G_CHIP_IDENT);
	memset(chip, 0, sizeof(chip));
	ret = ident ? ident(sd, chip) : -ENOIOCTLCMD;
	if (ret) {
		isp_printf(2, "tx-isp-t32: sensor %s not detected (%d)\n", bi.type, ret);
		vi->sd = NULL;
		t32_vi_release(vi);
		return -ENODEV;
	}
	vi->registered = 1;
	vi->width = T32_AT(sd, T32_SENSOR_VIDEO + T32_VIDEO_MBUS_WIDTH, u32);
	vi->height = T32_AT(sd, T32_SENSOR_VIDEO + T32_VIDEO_MBUS_HEIGHT, u32);
	tx_isp_sinfo_sensor_bind(sd, vi->owner);
	/* keep the address in the driver's slot after the unbind (openimp #513) */
	tx_isp_sinfo_driver_add(to_i2c_driver(client->dev.driver), info->i2c.addr, vi->owner);
	isp_printf(1, "tx-isp-t32: sensor %.32s registered (%s @ i2c%d 0x%02x, %ux%u)\n",
		   info->name, chip, info->i2c.i2c_adapter_id, info->i2c.addr,
		   vi->width, vi->height);
	return 0;
}

static int t32_sensor_stream(struct t32_vi *vi, int on)
{
	struct { s32 enable, vinum; } arg = { on, 0 };
	t32_sd_fn init, stream;
	int ret = 0;

	if (!vi->registered || !vi->sd)
		return -ENODEV;
	init = t32_sd_op(vi->sd, T32_OPS_CORE, T32_CORE_INIT);
	stream = t32_sd_op(vi->sd, T32_OPS_VIDEO, T32_VIDEO_S_STREAM);
	if (on) {
		/*
		 * IVDC core init (soft reset, mode word) before the sensor
		 * streams, as the vendor (ivdc_core_ops_init 0xd074 runs while
		 * IMP_ISP_AddSensor, long before the sensor S_STREAM).  Issued
		 * right after the sensor stream on, the T33L IVDC soft reset
		 * did not complete (busy bit 0 polled forever, openimp #509).
		 */
		ret = t32_ivdc_stream(1);	/* -110: the IVDC reset did not finish (#756, beyond vendor) */
		if (!ret && init)
			ret = init(vi->sd, &arg);
		if (!ret && stream)
			ret = stream(vi->sd, &arg);
		if (!ret)
			t32_hw_stream_on();
		else
			t32_ivdc_stream(0);
	} else {
		t32_hw_stream_off();
		t32_ivdc_stream(0);
		if (stream)
			ret = stream(vi->sd, &arg);
		if (init)
			init(vi->sd, &arg);
	}
	if (!ret)
		vi->streaming = on;
	return ret;
}

/* ------------------------------------------------------------------ */
/* MEMORY_MALLOC / FREE: physically contiguous buffers libimp maps     */
/* through /dev/mem (IQ bins for SET_TIZIANO_PAR)                      */

/*
 * T33: the list is changed under core.lock (ioctls, release); t32_core_iq_bin is called from the AE / AWB event
 * work without core.lock (the block refreshes, t33_gr_lock) and reads the bin copy below: the copy's switch and
 * that read take t32_alloc_lock (openimp #579).  T32 build: no-ops.
 *
 * T33 IQ bin copy (openimp #684 / #690): the vendor module parses the bin of SET_TIZIANO_PAR into its own
 * vmalloc'ed tparamsP (tisp_bin_data_parse, private_vmalloc), so libimp's MEMORY_FREE of the 150 KB kmalloc
 * buffer right after it frees the pages at once.  The blocks here read the bin in place, so the driver keeps
 * a vmalloc copy (order-0 pages) of the bin (pa up to the end of its buffer) instead of keeping libimp's
 * order-6 buffer: before, every streamer start left one order-6 block "retired" until rmmod (#628), and after
 * ~12 restarts no order-6 block was left for the next bin (AddSensor ENOMEM).  The copy before the last one is
 * kept until the next switch, so an event work that took the pointer before a switch never reads freed memory.
 */
#ifdef TX_ISP_T33
static DEFINE_SPINLOCK(t32_alloc_lock);
#define T32_ALLOC_LOCK()	spin_lock(&t32_alloc_lock)
#define T32_ALLOC_UNLOCK()	spin_unlock(&t32_alloc_lock)
static u8 *t33_bin_cur, *t33_bin_prev;
static u32 t33_bin_cur_bytes;
u32 t33_bin_copies;	/* SET_TIZIANO_PAR copies (/proc/jz/isp/isp) */
#else
#define T32_ALLOC_LOCK()	do { } while (0)
#define T32_ALLOC_UNLOCK()	do { } while (0)
#endif

static struct t32_alloc *t32_alloc_find_kva(u32 kva)
{
	struct t32_alloc *a;

	list_for_each_entry(a, &core.allocs, node)
		if ((u32)(unsigned long)a->kva == kva)
			return a;
	return NULL;
}

static struct t32_alloc *t32_alloc_find_pa(u32 pa)
{
	struct t32_alloc *a;

	list_for_each_entry(a, &core.allocs, node)
		if (pa >= a->pa && pa < a->pa + a->size)
			return a;
	return NULL;
}

static int t32_malloc(struct t32_buf_info *bi)
{
	struct t32_alloc *a;
	unsigned long p;

	if (!bi->size || bi->size > (16U << 20))
		return -EINVAL;
	a = kzalloc(sizeof(*a), GFP_KERNEL);
	if (!a)
		return -ENOMEM;
	a->order = get_order(bi->size);
	p = t32_dma_zpages(a->order);	/* ISP DMA memory: zero lines written back (openimp #530) */
	if (!p) {
		kfree(a);
		return -ENOMEM;
	}
	a->kva = (void *)p;
	a->pa = virt_to_phys(a->kva);
	a->size = bi->size;
	T32_ALLOC_LOCK();
	list_add_tail(&a->node, &core.allocs);
	T32_ALLOC_UNLOCK();
	bi->word0 = (u32)p;
	bi->word1 = a->pa;
	return 0;
}

static int t32_free(const struct t32_buf_info *bi)
{
	struct t32_alloc *a = t32_alloc_find_kva(bi->word0);

	if (!a)
		return -EINVAL;
	/* T33: the blocks read the driver's copy of the bin (openimp #628, #684), the pages go at once */
	T32_ALLOC_LOCK();
	if (core.tiziano_pa >= a->pa && core.tiziano_pa < a->pa + a->size)
		core.tiziano_pa = 0;
	list_del(&a->node);
	T32_ALLOC_UNLOCK();
	free_pages((unsigned long)a->kva, a->order);
	kfree(a);
	return 0;
}

static void t32_free_all(void)
{
	struct t32_alloc *a, *n;

	list_for_each_entry_safe(a, n, &core.allocs, node) {
		T32_ALLOC_LOCK();
		list_del(&a->node);
		T32_ALLOC_UNLOCK();
		free_pages((unsigned long)a->kva, a->order);
		kfree(a);
	}
	T32_ALLOC_LOCK();
	core.tiziano_pa = 0;
	T32_ALLOC_UNLOCK();
}

static int t32_set_tiziano_par(const struct t32_tiziano_par *par)
{
	u32 pa = par->pa;
	struct t32_alloc *a = t32_alloc_find_pa(pa);
	const u32 *w;
#ifndef TX_ISP_T33
	bool wdr_change;
#endif

	if (!a)
		return -EINVAL;
#ifdef TX_ISP_T33
	{
		/* the vendor's tisp_bin_data_parse copy (private_vmalloc), see t32_alloc_lock above (#684) */
		u32 bytes = a->pa + a->size - pa;
		u8 *c = vmalloc(bytes), *old;

		if (!c) {
			isp_printf(2, "tx-isp-t32: no memory for the IQ bin copy (%u bytes)\n", bytes);
			return -ENOMEM;
		}
		memcpy(c, (const void *)CKSEG1ADDR(pa), bytes);	/* libimp wrote it through /dev/mem */
		T32_ALLOC_LOCK();
		old = t33_bin_prev;
		t33_bin_prev = t33_bin_cur;
		t33_bin_cur = c;
		t33_bin_cur_bytes = bytes;
		T32_ALLOC_UNLOCK();
		vfree(old);
		t33_bin_copies++;
	}
#endif
	core.tiziano_pa = pa;
#ifndef TX_ISP_T33
	/* vendor: the switch runs when the stored WDR mode of the input equals (wdr == 1) */
	wdr_change = par->vinum < T32_BIN_VIS && (core.bin_wdr[par->vinum] == 1) != (par->wdr == 1);
#endif
	if (par->vinum < T32_BIN_VIS) {
		core.bin_wdr[par->vinum] = par->wdr;
		core.bin_night[par->vinum] = par->night;
	}
	/* libimp wrote the bin through an uncached /dev/mem mapping */
	w = (const u32 *)CKSEG1ADDR(pa);
	isp_printf(0, "tx-isp-t32: IQ parameters at pa %#x: %08x %08x %08x %08x \n",
		   pa, w[0], w[1], w[2], w[3]);
#ifdef TX_ISP_T33
	/* lib_tisp_refresh_para: a bin handed over while the ISP runs (ispInitFinish) is switched to (#504) */
	/* inside the vendor's VIC hold (ispcore_core_ops_ioctl 0x100001e, #692): then
	 * lib_tisp_set_csccr(vinum, night) when the request asks for it (CSCCR UV clip, #507) */
	if (par->vinum == 0 && core.vi[0].streaming)
		t33_core_bin_switch(par);
#else
	/* T32 (#534): the vendor's switch of a streaming sensor; a WDR mode change is the WDR path (not here) */
	if (par->vinum == 0 && core.vi[0].streaming && !wdr_change)
		t32_core_bin_switch(par);
#endif
	return 0;
}

/* ------------------------------------------------------------------ */
/* /dev/tx-isp                                                        */

#define T32_GET(arg, var) (copy_from_user(&(var), (void __user *)(arg), sizeof(var)) ? -EFAULT : 0)
#define T32_PUT(arg, var) (copy_to_user((void __user *)(arg), &(var), sizeof(var)) ? -EFAULT : 0)

/*
 * Vendor /dev/tx-isp commands the vendor libimp 2.0.2.1 issues (openimp #429):
 *
 * G_REGISTER 0x8040540e / S_REGISTER 0xc040540d (tx_isp_sensor_g_register /
 * tx_isp_sensor_s_register, T33 vendor module 0x267a0, lines 0x3c4/0x3d2 and
 * 0x3a9): the 64 B block is copied in and handed to the VIN subdevice as
 * sensor-ops event 0x02000012 / 0x02000011.  The VIN (vendor
 * subdev_sensor_ops_ioctl, T33 0x150e8, T32 vendor ko the same with the sd
 * ops at 0x108) does NOT forward the event to the sensor's sensor-ops ioctl
 * (sensor_cv2005_t33.ko answers 0x02000012 with 0 and no read, jump table
 * .rodata+0x20 -> 0x14a0): it takes the sensor of block word 8 (vinum), locks
 * it and calls the sensor's CORE op g_register (core ops +0x10, cv2005
 * 0x36c) / s_register (core ops +0x1c, cv2005 0x14cc) with the block;
 * -ENOIOCTLCMD (-515) or a missing op count as 0, no active sensor is
 * "Don't have active sensor!" (vin lines 0x26f / 0x281) and -1.  G_REGISTER
 * then copies the block back (openimp #709: the old sensor-ops ioctl call
 * left the block at 0, chip id read 0x0000 instead of 0x2005).
 */
#define T32_REG_BLOCK		64
#define T32_REG_BLOCK_VINUM	8	/* u32 word: input number */
#define T32_CORE_G_REGISTER	0x10
#define T32_CORE_S_REGISTER	0x1c

/* the sensor picture size of the running input (T33 OSD bounds) */
void t32_core_in_size(u32 *w, u32 *h)
{
	*w = core.vi[0].width;
	*h = core.vi[0].height;
}

/* vendor VIN 0x02000011 / 0x02000012 (subdev_sensor_ops_ioctl) */
static int t32_vin_sensor_reg(u32 *b, unsigned int op, const char *fn, int line)
{
	int (*reg)(void *sd, void *arg);
	struct t32_vi *vi;
	void *ops, *cops;
	int r;

	vi = b[T32_REG_BLOCK_VINUM] < T32_MAX_VI ? &core.vi[b[T32_REG_BLOCK_VINUM]] : NULL;
	if (!vi || !vi->registered || !vi->sd) {
		isp_printf(1, "[ %s:%d ] Don't have active sensor!\n", fn, line);
		return -1;
	}
	ops = T32_AT(vi->sd, T32_SD_OPS, void *);
	cops = ops ? T32_AT(ops, T32_OPS_CORE, void *) : NULL;
	reg = cops ? T32_AT(cops, op, void *) : NULL;
	r = reg ? reg(vi->sd, b) : -ENOIOCTLCMD;
	return r == -ENOIOCTLCMD ? 0 : r;
}

static long t32_sensor_g_register(unsigned long arg)
{
	u32 b[T32_REG_BLOCK / 4];
	int r;

	if (copy_from_user(b, (void __user *)arg, sizeof(b))) {
		isp_printf(2, "[ %s:%d ] copy from user error\n", "tx_isp_sensor_g_register", 0x3c4);
		return -EFAULT;
	}
	r = t32_vin_sensor_reg(b, T32_CORE_G_REGISTER, "tx_isp_vin_g_register", 0x26f);
	if (r)
		return r;
	if (copy_to_user((void __user *)arg, b, sizeof(b))) {
		isp_printf(2, "[ %s:%d ] copy from user error\n", "tx_isp_sensor_g_register", 0x3d2);
		return -EFAULT;
	}
	return 0;
}

static long t32_sensor_s_register(unsigned long arg)
{
	u32 b[T32_REG_BLOCK / 4];

	if (copy_from_user(b, (void __user *)arg, sizeof(b))) {
		isp_printf(2, "[ %s:%d ] copy from user error\n", "tx_isp_sensor_s_register", 0x3a9);
		return -EFAULT;
	}
	return t32_vin_sensor_reg(b, T32_CORE_S_REGISTER, "tx_isp_vin_s_register", 0x281);
}

/*
 * SET_FRAME_DROP 0xc004542c / GET_FRAME_DROP 0xc004542d (tx_isp_set_frame_drop
 * / tx_isp_get_frame_drop, vendor 0x267a0; tisp_msca_api_{set,get}_frame_drop,
 * single-sensor direct path): 40 B block {u8 vi, 3 x {u32 enable, u8 lsize,
 * u32 fmark}}.  Set per output k: enable 1 -> skip reg = lsize (>= 32: message,
 * -1, the last output's result is the ioctl result like the vendor loop),
 * mask reg = fmark; otherwise skip 0, mask 0xffffffff.  Get: lsize and mask
 * read back, enable word forced to 1, result 0.  The vi byte is not used
 * (one MSCA).
 */
#define T32_FD_BLOCK	40
#define T32_FD_OUTPUTS	3

#ifdef TX_ISP_T33
/* T33: the oracle-proven vendor port (tx_isp_t33_msca_api3.h, tests/t33_binoracle/msca-api3.conf) runs per output */
#include "tx_isp_t33_msca_api3.h"

static u32 t33_fd_rd(u32 a) { return t32_hw_rd(a); }
static void t33_fd_wr(u32 a, u32 v) { t32_hw_wr(a, v); }
static int t33_fd_print(int lvl, const char *fmt, ...)
{
	isp_printf(lvl, "%s", fmt);
	return 0;
}

static const struct t33_msca_io t33_fd_io = { .sys_rd = t33_fd_rd, .sys_wr = t33_fd_wr, .print = t33_fd_print };

static long t32_frame_drop_set(unsigned long arg)
{
	u32 b[T32_FD_BLOCK / 4];
	long r = 0;
	int k;

	if (copy_from_user(b, (void __user *)arg, sizeof(b))) {
		isp_printf(2, "[ %s:%d ] copy from user error\n", "tx_isp_set_frame_drop", 0x775);
		return -EFAULT;
	}
	for (k = 0; k < T32_FD_OUTPUTS; k++)
		r = t33_msca_set_frame_drop(&t33_fd_io, 1, k, (const u8 *)&b[1 + 3 * k]);
	return r;
}
#else
static long t32_frame_drop_set(unsigned long arg)
{
	u32 b[T32_FD_BLOCK / 4];
	long r = 0;
	int k;

	if (copy_from_user(b, (void __user *)arg, sizeof(b))) {
		isp_printf(2, "[ %s:%d ] copy from user error\n", "tx_isp_set_frame_drop", 0x775);
		return -EFAULT;
	}
	for (k = 0; k < T32_FD_OUTPUTS; k++) {
		u32 *e = &b[1 + 3 * k], skip = 0, mask = 0xffffffff;

		r = 0;
		if (e[0] == 1) {
			skip = *(u8 *)(e + 1);
			if (skip >= 32) {
				isp_printf(1, "FPS over 32 in the numerator or denominator!!! Setup failed!!!\n");
				r = -1;
				continue;
			}
			mask = e[2];
		}
		t32_hw_wr(T32_MSCA_FDROP_SKIP(k), skip);
		t32_hw_wr(T32_MSCA_FDROP_MASK(k), mask);
	}
	return r;
}
#endif

#ifdef TX_ISP_T33
/*
 * The default IQ bin path table (tx_isp_t33_bin.h, oracle block bin): ctx -> priv -> word 276 -> table of 4 x 4
 * records of 136 B {idx, sel, path[128]}; the vendor keeps it in the ISP core context, here three static blocks.
 */
#include "tx_isp_t33_bin.h"
static u8 t33_bin_tab[4096], t33_bin_priv[300];
static u32 t33_bin_ctx[1];
static int t33_bin_cfu(void *to, const void *from, unsigned long n) { return copy_from_user(to, (const void __user *)from, n); }
static int t33_bin_ctu(void *to, const void *from, unsigned long n) { return copy_to_user((void __user *)to, from, n); }
static int t33_bin_sprintf(char *buf, const char *fmt, const char *arg) { return scnprintf(buf, 128, "%.127s", fmt); }	/* bounded: the vendor sprintf can run past the 128-byte path field */
static void *t33_bin_memcpy(void *to, const void *from, unsigned long n) { return memcpy(to, from, n); }
static int t33_bin_printf(int level, const char *fmt, ...) { isp_printf(level, "%s\n", fmt); return 0; }
/* positional: the member names copy_from_user / copy_to_user are kernel macros here */
static const struct t33_bin_ops t33_bin_ops = {
	t33_bin_cfu, t33_bin_ctu, t33_bin_sprintf, t33_bin_memcpy, t33_bin_printf,
};

static long t33_bin_path_ioctl(unsigned int cmd, unsigned long arg)
{
	*(u32 *)t33_bin_priv = 0;
	*(u32 *)(t33_bin_priv + 276) = (u32)(unsigned long)t33_bin_tab;
	t33_bin_ctx[0] = (u32)(unsigned long)t33_bin_priv;
	if (cmd == T32_IOC_SET_BIN_PATH)
		return t33_bin_set_path((u8 *)t33_bin_ctx, (u8 *)arg, &t33_bin_ops);
	return t33_bin_get_path((u8 *)t33_bin_ctx, (u8 *)arg, &t33_bin_ops);
}
#endif

static long t32_frame_drop_get(unsigned long arg)
{
	u32 b[T32_FD_BLOCK / 4];
	int k;

	if (copy_from_user(b, (void __user *)arg, sizeof(b))) {
		isp_printf(2, "[ %s:%d ] copy from user error\n", "tx_isp_get_frame_drop", 0x7a6);
		return -EFAULT;
	}
#ifdef TX_ISP_T33
	for (k = 0; k < T32_FD_OUTPUTS; k++)
		t33_msca_get_frame_drop(&t33_fd_io, 1, 0, k, (u8 *)&b[1 + 3 * k]);
#else
	for (k = 0; k < T32_FD_OUTPUTS; k++) {
		u32 *e = &b[1 + 3 * k];

		*(u8 *)(e + 1) = (u8)t32_hw_rd(T32_MSCA_FDROP_SKIP(k));
		e[2] = t32_hw_rd(T32_MSCA_FDROP_MASK(k));
		e[0] = 1;
	}
#endif
	if (copy_to_user((void __user *)arg, b, sizeof(b))) {
		isp_printf(2, "[ %s:%d ] copy to user error\n", "tx_isp_get_frame_drop", 0x7b5);
		return -EFAULT;
	}
	return 0;
}

/*
 * 0xc00454b9, online LDC LUT memory (T33 vendor tx_isp_ldc_online_probe; the
 * T32 module has no handler: 0, block untouched).  48-byte block {vinum,
 * func, block size (u16), .., word 6 (top half 1: one buffer), .., word 8
 * extra bytes, word 9 size out, word 10 / 11 physical addresses out}; the
 * sensor size comes from the core's event 0x100001c (width low, height high).
 * Core lock held.
 */
static long t32_ldc_online_probe(unsigned long arg)
{
#ifdef TX_ISP_T33
	struct t32_buf_info bi;
	u32 b[12], w = 0, h = 0, bs, size;
	int k, n;

	if (copy_from_user(b, (void __user *)arg, sizeof(b))) {
		isp_printf(2, "[ %s:%d ] copy from user error\n", "tx_isp_ldc_online_probe", 0xa2b);
		return -EFAULT;
	}
	if (b[0] < T32_MAX_VI && core.vi[b[0]].registered) {
		w = core.vi[b[0]].width;
		h = core.vi[b[0]].height;
	}
	if ((b[1] == 2 || b[1] == 4) && (bs = b[2] & 0xffff)) {
		b[2] = bs;
		size = (((w + bs - 1) / bs + 3) & ~1U) * ((h + bs - 1) / bs + 1) * 4 + b[8];
		n = (b[6] >> 16) == 1 ? 1 : 2;
		for (k = 0; k < n; k++) {
			memset(&bi, 0, sizeof(bi));
			bi.size = size;
			if (t32_malloc(&bi)) {
				isp_printf(1, "[ %s:%d ] Failed to allocate memory\n", "tx_isp_ldc_online_probe",
					   k ? 0xa5c : 0xa47);
				return -ENOMEM;
			}
			/* no dirty line may later land on what libimp writes through /dev/mem */
			dma_cache_wback_inv((unsigned long)bi.word0, size);
			b[9] = size;
			b[10 + k] = bi.word1;
		}
	}
	if (copy_to_user((void __user *)arg, b, sizeof(b))) {
		isp_printf(2, "[ %s:%d ] copy from user error\n", "tx_isp_ldc_online_probe", 0xa67);
		return -EFAULT;
	}
#endif
	return 0;
}

static long t32_isp_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct t32_vi *vi = &core.vi[0];
	struct t32_buf_info bi;
	struct t32_initarg ia;
	u32 v;
	long ret = 0;

	/* RAW snapshot: waits for frames, outside the core lock (vendor: no lock) */
	if (cmd == T32_IOC_SNAP_RAW || cmd == T32_IOC_SNAP_CTL) {
		ret = t32_snap_ioctl(cmd, arg);
		if (t32_trace || ret)
			isp_printf(ret ? 1 : 0, "tx-isp-t32: ioctl %#x -> %ld\n", cmd, ret);
		return ret;
	}
#ifdef TX_ISP_T33
	/* custom AE / AWB library (openimp #805): the get waits for a statistics frame, outside the core lock */
	ret = t33_calgo_ioctl(file, cmd, arg);
	if (ret != -ENOIOCTLCMD) {
		if (t32_trace || (ret && ret != -EAGAIN))
			isp_printf(ret ? 1 : 0, "tx-isp-t32: ioctl %#x -> %ld\n", cmd, ret);
		return ret;
	}
	ret = 0;
#endif
	mutex_lock(&core.lock);
	core.ioctls++;
	switch (cmd) {
#ifdef TX_ISP_T33
	case T32_IOC_DRIVER_VERSION:	/* tx_isp_driver_version: the vendor's 96-byte block */
		ret = t33_driver_version((void *)arg, driver_soc, driver_version, t33_put_user_block);
		break;
#else
	case T32_IOC_DRIVER_VERSION: {
		char ver[64];

		memset(ver, 0, sizeof(ver));
		strlcpy(ver, driver_soc, 32);
		strlcpy(ver + 32, driver_version, 32);
		ret = T32_PUT(arg, ver);
		break;
	}
#endif
	case T32_IOC_REGISTER_SENSOR: {
		struct t32_sensor_reginfo info;

		ret = T32_GET(arg, info);
		if (!ret)
			ret = t32_register_sensor(vi, &info);
		break;
	}
	case T32_IOC_RELEASE_SENSOR:
		if (vi->streaming)
			t32_sensor_stream(vi, 0);
		t32_vi_release(vi);
		break;
	case T32_IOC_DEFAULT_BIN: {
		char *buf = kzalloc(T32_DEFAULT_BIN_SIZE, GFP_KERNEL);
		u8 n;

		if (!buf) {
			ret = -ENOMEM;
			break;
		}
		if (copy_from_user(buf, (void __user *)arg, T32_DEFAULT_BIN_SIZE)) {
			kfree(buf);
			ret = -EFAULT;
			break;
		}
		n = buf[T32_DEFAULT_BIN_VI];
		buf[T32_DEFAULT_BIN_FIRST] = 0;
		*(u32 *)(buf + T32_DEFAULT_BIN_WDR) = n < T32_BIN_VIS ? core.bin_wdr[n] : 0;
		*(u32 *)(buf + T32_DEFAULT_BIN_DN) = n < T32_BIN_VIS ? core.bin_night[n] : 0;
		strlcpy(buf + T32_DEFAULT_BIN_VER, bin_version,
			T32_DEFAULT_BIN_SOC - T32_DEFAULT_BIN_VER);
		strlcpy(buf + T32_DEFAULT_BIN_SOC, driver_soc,
			T32_DEFAULT_BIN_SIZE - T32_DEFAULT_BIN_SOC);
		ret = copy_to_user((void __user *)arg, buf, T32_DEFAULT_BIN_SIZE) ? -EFAULT : 0;
		kfree(buf);
		core.default_bin++;
		break;
	}
	case T32_IOC_MEMORY_MALLOC:
		ret = T32_GET(arg, bi);
		if (!ret)
			ret = t32_malloc(&bi);
		if (!ret)
			ret = T32_PUT(arg, bi);
		break;
	case T32_IOC_MEMORY_FREE:
		ret = T32_GET(arg, bi);
		if (!ret)
			ret = t32_free(&bi);
		break;
	case T32_IOC_SET_TIZIANO_PAR: {
		struct t32_tiziano_par par;

		ret = T32_GET(arg, par);
		if (!ret)
			ret = t32_set_tiziano_par(&par);
		break;
	}
	case T32_IOC_ENUMINPUT: {
		/* {index, name[32]}: libimp walks the inputs and matches the name */
		struct { u32 index; char name[32]; } in;

		ret = T32_GET(arg, v);
		if (ret)
			break;
		if (v >= T32_MAX_VI || !core.vi[v].registered) {
			ret = -EINVAL;
			break;
		}
		memset(&in, 0, sizeof(in));
		in.index = v;
		strlcpy(in.name, core.vi[v].info.name, sizeof(in.name));
		ret = T32_PUT(arg, in);
		break;
	}
	case T32_IOC_S_INPUT:
		ret = T32_GET(arg, ia);
		if (ret)
			break;
		if (ia.enable) {
			if (!vi->registered) {
				ret = -ENODEV;
				break;
			}
			vi->width = T32_AT(vi->sd, T32_SENSOR_VIDEO + T32_VIDEO_MBUS_WIDTH, u32);
			vi->height = T32_AT(vi->sd, T32_SENSOR_VIDEO + T32_VIDEO_MBUS_HEIGHT, u32);
			if (!vi->input_on) {
				void *attr = T32_AT(vi->sd, T32_SENSOR_VIDEO + T32_VIDEO_ATTR, void *);

#ifdef TX_ISP_T33
				t32_ldc_input_latch(core.ldc_mode);	/* ispcore init: 0x5000007 -> stIspInitPar +0x2c */
#else
				t32_ldc_input_latch(vi->width, vi->height);	/* 0x5000007 -> stIspInitPar +0x28, tisp_ldc_init (#751) */
#endif

				ret = attr ? t32_hw_input_setup(attr, vi->width, vi->height) : -ENODEV;
				if (ret)
					break;
			}
			vi->input_on = 1;
			ia.vinum = (s32)((vi->width << 16) | vi->height);
		} else {
#ifndef TX_ISP_T33
			if (vi->input_on)
				t32_ldc_input_off();	/* lib_tisp_deinit -> tisp_ldc_deinit */
#endif
			vi->input_on = 0;
			ia.vinum = 0;
		}
		ret = T32_PUT(arg, ia);
		break;
	case T32_IOC_GET_ISP_IPC_MEM_INFO:
		ret = T32_GET(arg, bi);
		if (!ret) {
			bi.size = 0;		/* no IPC (RISC-V) memory in S1 */
#ifndef TX_ISP_T33
			/* tisp_top_ipc_get_mem_size with raw = isp_cache_line (tx_isp_get_isp_ipc_mem, #759) */
			bi.size = t32_top_ipc_get_mem_size(vi->width, (u32)isp_cache_line, &vi->top_ipc[13]);
#endif
			bi.size += t32_ldc_ipc_size(vi->width, vi->height);	/* online LDC (T33 #491, T32 #751) */
			ret = T32_PUT(arg, bi);
		}
		break;
	case T32_IOC_SET_ISP_IPC_MEM_INFO:
		ret = T32_GET(arg, bi);
		if (!ret) {
			vi->ipc_mem_pa = bi.word1;
			vi->ipc_mem_size = bi.size;
#ifndef TX_ISP_T33
			{
				/* lib_tisp_set_isp_ipc_memory_space: tisp_top_ipc_set_mem_cfg, the LDC part after it (#759) */
				struct t32_front_write tw[6];
				unsigned int i, n = t32_top_ipc_set_mem_cfg(vi->top_ipc, bi.word1, tw);

				for (i = 0; i < n; i++)
					t32_hw_wr(tw[i].reg, tw[i].val);
				t32_ldc_ipc_set(bi.word1 + vi->top_ipc[13], vi->width, vi->height);
			}
#else
			t32_ldc_ipc_set(bi.word1, vi->width, vi->height);
#endif
		}
		break;
	case T32_IOC_GET_ISP_MEM_INFO:
		ret = T32_GET(arg, bi);
		if (ret)
			break;
		if (!vi->width || !vi->height) {
			ret = -ENODEV;
			break;
		}
#ifdef TX_ISP_T33
		/* lib_tisp_get_isp_memory_space: the T33 always takes tisp_mdns_t33_get_mem_size; with our ratio
		 * memopt the vendor's memopt bits 4..7 / 8..15 (second plane off / extra MiB) are 0, as its default 5 */
		bi.size = t33_mdns_get_mem_size(isp_memopt, vi->width, vi->height, 0, 0);
		if (bi.size == T33_MDNS_E_MEMOPT)
			bi.size = 0;
#else
		bi.size = t32_isp_mem_size(vi->width, vi->height, isp_memopt, 0, 0);
#endif
		ret = bi.size ? T32_PUT(arg, bi) : -EINVAL;
		break;
	case T32_IOC_SET_ISP_MEM_INFO:
		ret = T32_GET(arg, bi);
		if (!ret) {
			vi->isp_mem_pa = bi.word1;
			vi->isp_mem_size = bi.size;
		}
		break;
	case T32_IOC_G_INPUT:
	case T32_IOC_CREATE_SUBDEV_LINKS:
	case T32_IOC_DESTROY_SUBDEV_LINKS:
		ret = T32_GET(arg, ia);
		break;
	case T32_IOC_LINKS_STREAMON:
	case T32_IOC_LINKS_STREAMOFF:
		/* the pipeline links come with the frame path (S2) */
		ret = T32_GET(arg, ia);
		if (!ret)
			ret = T32_PUT(arg, ia);
		break;
	case T32_IOC_STREAMON:
		ret = T32_GET(arg, ia);
		if (!ret)
			ret = t32_sensor_stream(vi, 1);
		break;
	case T32_IOC_STREAMOFF:
		ret = T32_GET(arg, ia);
		if (!ret && vi->streaming)
			ret = t32_sensor_stream(vi, 0);
		break;
	case T32_IOC_SET_AE_START_EV:
	case T32_IOC_GET_AE_START_EV:
		ret = t32_tunctl_ae_start(cmd, arg);
		break;
	case T32_IOC_SET_SENSOR_REGISTER:
		ret = t32_sensor_s_register(arg);
		break;
	case T32_IOC_GET_SENSOR_REGISTER:
		ret = t32_sensor_g_register(arg);
		break;
	case T32_IOC_BYPASS_SENSOR: {
		/*
		 * BYPASS_SENSOR 0x80085418 (vendor tx_isp_bypass_all, recovered in
		 * tx_isp_t32_txuioctl.h): {enable, input} stored in the core
		 * device's per-input bypass word, then subdevice 0 is notified with
		 * 0x200001d, which the vendor VIC answers by reading its request
		 * from address 0 (tx_isp_t32_vicops.h).  Only the flag is kept
		 * here (openimp #446); no register is written.
		 */
		u32 b[2];

		ret = copy_from_user(b, (void __user *)arg, sizeof(b)) ? -EFAULT : 0;
		if (ret)
			isp_printf(2, "[ %s:%d ] copy from user error\n", "tx_isp_bypass_all", 1395);
		else if (b[1] < T32_BIN_VIS)
			core.bypass[b[1]] = b[0];
		break;
	}
	case T32_IOC_SET_FRAME_DROP:
		ret = t32_frame_drop_set(arg);
		break;
	case T32_IOC_GET_FRAME_DROP:
		ret = t32_frame_drop_get(arg);
		break;
	case T32_IOC_GET_RUN_MODE:
		/* tx_isp_get_isp_driver_running_mode: the running-mode word */
		ret = T32_PUT(arg, core.run_mode);
		break;
	case T32_IOC_RUN_SWITCH:
		/* the value itself is the argument: 0 normal, 1 one-shot, 2 / 3 one
		 * frame in one-shot mode (refused otherwise, as the vendor); the
		 * frame trigger of the low-power path is not modelled: frames keep
		 * flowing (QEMU proof only; AOV effect: T32/T33 camera) */
		if (arg == 0 || arg == 1) {
#ifdef TX_ISP_T33
			t33_tmo_oneshot(arg);	/* core ioctl 0x100000e / 0x100000f */
#endif
			core.run_mode = arg;
		} else if (arg == 2 || arg == 3) {
			ret = core.run_mode == 1 ? 0 : -EINVAL;
#ifdef TX_ISP_T33
			if (!ret)
				t33_tmo_oneshot(2);	/* core ioctl 0x1000010 */
#endif
		} else {
			ret = -EINVAL;
		}
		break;
	case T32_IOC_LDC_MODE:
		/* broadcast to the subdevices, sum of the results (0): the mode is kept */
		ret = T32_GET(arg, v);
		if (!ret)
			core.ldc_mode = v;
		if (!ret)
			t32_ldc_mode_set(v);
		break;
	case T32_IOC_LDC_ONLINE_PROBE:
		t32_ldc_probe_count();
		ret = t32_ldc_online_probe(arg);
		break;
	case T32_IOC_LDC_ONLINE_INIT:
	case T32_IOC_LDC_ONLINE_PAR:
		ret = t32_ldc_online_ioctl(cmd, arg, core.ldc_mode);
		break;
#ifdef TX_ISP_T33
	case T32_IOC_SET_BIN_PATH:
	case T32_IOC_GET_BIN_PATH:
		ret = t33_bin_path_ioctl(cmd, arg);
		break;
	case T32_IOC_LDC_BYPASS_STATE:
	case T32_IOC_LDC_REFLESH_LUT:
		ret = t32_ldc_ctl_ioctl(cmd, arg, core.ldc_mode);
		break;
#endif
	case T32_IOC_SET_PM_CTRL:
	case T32_IOC_GET_PM_CTRL:
		/* vendor tx-isp-t33.ko (QEMU, openimp #54): 0, the user block {mode,
		 * vinum} comes back unchanged */
		break;
	case T32_IOC_SET_INTERNAL_CHN_ATTR:
	case T32_IOC_GET_INTERNAL_CHN_ATTR:
		/* no handler in the vendor dispatcher: it falls out of the
		 * command tree with 0 and leaves the user block alone */
		break;
	default:
		ret = -ENOTTY;
		break;
	}
	if (t32_trace || ret)
		isp_printf(ret ? 1 : 0, "tx-isp-t32: ioctl %#x -> %ld\n", cmd, ret);
	mutex_unlock(&core.lock);
	return ret;
}


/* ------------------------------------------------------------------ */
/* clocks: the vendor module's platform tables (ISP core, IVDC, CSI)   */
/* ------------------------------------------------------------------ */

/*
 * Probe sets the platform rate (isp_subdev_init_clks), the first open sets
 * the parents and the working rates and enables everything
 * (ispcore/ivdc/csi activate), the last release disables it again.  Rate
 * 0: none set.  Without these the ISP clock gate (CLKGR0 bit 19) stays
 * closed; the kernel gates unused clocks after boot.
 */
struct t32_clk {
	const char *name;
	unsigned long probe_rate;	/* platform table */
	unsigned long rate;		/* activate: isp_clk, isp_clks, isp_clkl, isp_clka, isp_clkv */
	int *rate_param;		/* module parameter overriding rate (0: default) */
	struct clk *clk;
};

/*
 * Vendor glue module parameters (same names and types as the vendor
 * module; the thingino glue passes them, unknown ones make insmod fail).
 * *_name: parent of the clock, isp_clk*: working rate; 0 / NULL keeps the
 * defaults below.  isp_clks / clks_name: scaler clock (T32 div_isps; the
 * T33 has none, accepted and ignored).
 */
static char *clk_name, *clks_name, *clka_name, *clkv_name, *clkl_name;
static int isp_clk, isp_clks, isp_clka, isp_clkv, isp_clkl;
module_param(clk_name, charp, 0444);
MODULE_PARM_DESC(clk_name, "chose parent clk");
module_param(clks_name, charp, 0444);
MODULE_PARM_DESC(clks_name, "chose scaler parent clk");
module_param(clka_name, charp, 0444);
MODULE_PARM_DESC(clka_name, "chose axi parent clk");
module_param(clkv_name, charp, 0444);
MODULE_PARM_DESC(clkv_name, "chose vpu parent clk");
module_param(clkl_name, charp, 0444);
MODULE_PARM_DESC(clkl_name, "chose ldc parent clk");
module_param(isp_clk, int, 0444);
MODULE_PARM_DESC(isp_clk, "isp clock freq");
module_param(isp_clks, int, 0444);
MODULE_PARM_DESC(isp_clks, "scaler clock freq");
module_param(isp_clka, int, 0444);
MODULE_PARM_DESC(isp_clka, "axi clock freq");
module_param(isp_clkv, int, 0444);
MODULE_PARM_DESC(isp_clkv, "vpu clock freq");
module_param(isp_clkl, int, 0444);
MODULE_PARM_DESC(isp_clkl, "ldc clock freq");

/*
 * Further vendor glue parameters.  direct_mode / ivdc_*: IVDC direct mode
 * (ISP channel 0 straight into the encoder) live in tx_isp_t32_ivdcdrv.c
 * (#349, T32 and T33 modules).
 * The others are accepted for insmod compatibility and only stored.
 */
static int isp_ch0_pre_dequeue_time, isp_cache_line, atlas_open, zeratul_open;
module_param(isp_ch0_pre_dequeue_time, int, 0444);
MODULE_PARM_DESC(isp_ch0_pre_dequeue_time, "isp pre dequeue time, unit ms (accepted, unused)");
module_param(isp_cache_line, int, 0444);
#ifdef TX_ISP_T33
MODULE_PARM_DESC(isp_cache_line, "isp cache line (accepted, unused)");
#else
MODULE_PARM_DESC(isp_cache_line, "isp cache line (top IPC memory lines, #759)");
#endif
module_param(atlas_open, int, 0444);
MODULE_PARM_DESC(atlas_open, "atlas open (accepted, unused)");
module_param(zeratul_open, int, 0444);
MODULE_PARM_DESC(zeratul_open, "zeratul open (accepted, unused)");

struct t32_clk_parent {
	const char *mux;
	const char *parent;
	char **param;			/* *_name module parameter */
};

#ifdef TX_ISP_T33
/*
 * T33 3.10.14 clock tree (vendor module strings and /proc/jz/clock/clocks
 * in QEMU): no div_isps and no mux_* clocks; the parents are set on the
 * dividers themselves.  Vendor defaults measured: div_isp 325 MHz (mpll,
 * 340 asked), div_ldc 325 (mpll, 357 asked), div_vpu 316.7 (sclka, 340
 * asked), div_ispa 475 (sclka); gate_ldc and gate_vpu are opened too.
 */
static struct t32_clk t32_clks[] = {
	{ "div_isp", T32_CLK_ISP_PROBE, T32_CLK_ISP, &isp_clk },
	{ "div_ldc", T32_CLK_LDC_PROBE, T32_CLK_LDC, &isp_clkl },
	{ "gate_isp" },
	{ "div_ispa", 500000000, 500000000, &isp_clka },
	{ "div_vpu", T32_CLK_VPU_PROBE, T32_CLK_VPU, &isp_clkv },
	{ "gate_ivdc" },
	{ "gate_csi" },
	{ "gate_ldc" },
	{ "gate_vpu" },
};

static const struct t32_clk_parent t32_clk_parents[] = {
	{ "div_isp", "mpll", &clk_name }, { "div_ldc", "mpll", &clkl_name },
	{ "div_ispa", "sclka", &clka_name }, { "div_vpu", "sclka", &clkv_name },
};
#else
static struct t32_clk t32_clks[] = {
	{ "div_isp", T32_CLK_ISP_PROBE, T32_CLK_ISP, &isp_clk },
	{ "div_isps", 476000000, 476000000, &isp_clks },
	{ "div_ldc", T32_CLK_LDC_PROBE, T32_CLK_LDC, &isp_clkl },
	{ "gate_isp" },
	{ "div_ispa", 500000000, 500000000, &isp_clka },
	{ "div_vpu", T32_CLK_VPU_PROBE, T32_CLK_VPU, &isp_clkv },
	{ "gate_ivdc" },
	{ "gate_csi" },
};

static const struct t32_clk_parent t32_clk_parents[] = {
	{ "mux_isp", "mpll", &clk_name }, { "mux_isps", "mpll", &clks_name },
	{ "mux_ldc", "mpll", &clkl_name }, { "mux_ispa", "sclka", &clka_name },
	{ "mux_vpu", "sclka", &clkv_name },
};
#endif

static unsigned int t32_clk_users;

static void t32_clks_probe(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(t32_clks); i++) {
		struct t32_clk *c = &t32_clks[i];

		c->clk = clk_get(NULL, c->name);
		if (IS_ERR(c->clk)) {
			pr_warn("tx-isp-t32: clock %s not found\n", c->name);
			c->clk = NULL;
			continue;
		}
		if (c->probe_rate && clk_set_rate(c->clk, c->probe_rate))
			pr_warn("tx-isp-t32: clock %s rate %lu not set\n", c->name, c->probe_rate);
	}
}

static void t32_clks_put(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(t32_clks); i++) {
		if (t32_clks[i].clk)
			clk_put(t32_clks[i].clk);
		t32_clks[i].clk = NULL;
	}
}

#ifdef TX_ISP_T33
/* hooks of tisp's ispcore_set_clk_parent / ivdc_set_clk_parent (tx_isp_t33_clkp.h) */
static void *t33_clkp_get(void *dev, const char *name)
{
	return clk_get(NULL, name);
}

static void t33_clkp_put(void *dev, void *clk)
{
	if (!IS_ERR_OR_NULL(clk))	/* the vendor puts error pointers too (devm: a warning) */
		clk_put(clk);
}

static void *t33_clkp_pget(void *dev, const char *name)
{
	return clk_get(NULL, name);
}

static int t33_clkp_set(void *clk, void *parent)
{
	return IS_ERR(parent) ? PTR_ERR(parent) : clk_set_parent(clk, parent);
}

static void t33_clkp_pput(void *clk)
{
	if (!IS_ERR_OR_NULL(clk))
		clk_put(clk);
}

static void t33_clkp_log(const char *fmt, const char *arg)
{
	isp_printf(1, fmt, arg);
}

static const struct t33_clkp_ops t33_clkp = {
	.devm_clk_get = t33_clkp_get, .devm_clk_put = t33_clkp_put, .clk_get = t33_clkp_pget,
	.clk_set_parent = t33_clkp_set, .clk_put = t33_clkp_pput, .log = t33_clkp_log,
};
#endif

static void t32_clks_activate(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(t32_clk_parents); i++) {
		const struct t32_clk_parent *p = &t32_clk_parents[i];
		const char *pname = *p->param && **p->param ? *p->param : p->parent;
#ifdef TX_ISP_T33
		/*
		 * ispcore_activate_module (div_isp / div_ldc) and ivdc_activate_module
		 * (div_ispa / div_vpu): the parent must be "sclka" or "mpll", else it is
		 * only logged and not set (vendor sclk_name, no "vpll" on the T33).
		 */
		void *dev = NULL;

		t33_set_clk_parent(&t33_clkp, &dev, pname, p->mux);
#else
		struct clk *mux = clk_get(NULL, p->mux);
		struct clk *par = clk_get(NULL, pname);

		if (!IS_ERR(mux) && !IS_ERR(par) && clk_set_parent(mux, par))
			pr_warn("tx-isp-t32: %s parent %s not set\n", p->mux, pname);
		if (!IS_ERR(par))
			clk_put(par);
		if (!IS_ERR(mux))
			clk_put(mux);
#endif
	}
	for (i = 0; i < ARRAY_SIZE(t32_clks); i++) {
		struct t32_clk *c = &t32_clks[i];

		unsigned long rate = c->rate_param && *c->rate_param > 0 ? *c->rate_param : c->rate;

		if (!c->clk)
			continue;
		if (rate && clk_get_rate(c->clk) != rate && clk_set_rate(c->clk, rate))
			pr_warn("tx-isp-t32: clock %s rate %lu not set\n", c->name, rate);
		if (clk_prepare_enable(c->clk))
			pr_warn("tx-isp-t32: clock %s not enabled\n", c->name);
	}
}

static void t32_clks_deactivate(void)
{
	unsigned int i;

	for (i = ARRAY_SIZE(t32_clks); i-- > 0;) {
		if (t32_clks[i].clk)
			clk_disable_unprepare(t32_clks[i].clk);
	}
}

/* ISP clocks on (an open /dev/tx-isp); rmmod runs with them off */
int t32_core_clk_active(void)
{
	return t32_clk_users != 0;
}

/*
 * private_reset_tx_isp_module: CPM SRBC (0x100000c4) bit 21 stop request,
 * wait for bit 20 (stopped), then pulse bit 22 (ISP soft reset).  The vendor
 * module does it right after the clocks are on, before the first ISP
 * register write (QEMU trace: clock on, soft reset, 0x22028).  Without it the
 * T33 silicon never ran a register list (openimp #393).
 */
/*
 * openimp #625 diagnostics: srbc_debug=1 logs the CPM state (whole
 * 0x10000000-0x100000fc block, read only) at module load and around the ISP
 * reset of the first open, plus the poll count until the stop ack.  Off by
 * default; nothing is written.
 */
static int srbc_debug;
module_param(srbc_debug, int, 0644);
MODULE_PARM_DESC(srbc_debug, "log CPM/SRBC state around the ISP reset (openimp #625)");

static void t32_srbc_dump(const char *tag)
{
	char buf[8 * 9 + 1];
	unsigned int r, i;

	if (!srbc_debug)
		return;
	for (r = 0; r < 0x100; r += 0x20) {
		for (i = 0; i < 8; i++)
			snprintf(buf + i * 9, 10, " %08x",
				 readl((void __iomem *)CKSEG1ADDR(0x10000000 + r + i * 4)));
		pr_info("tx-isp-t32: #625 %s cpm+%02x:%s\n", tag, r, buf);
	}
}

static void t32_isp_soft_reset(void)
{
	void __iomem *srbc = (void __iomem *)CKSEG1ADDR(0x100000c4);
	int tries;

	t32_srbc_dump("pre-reset");
	writel(readl(srbc) | (1u << 21), srbc);
	for (tries = 500; tries; tries--) {
		if (readl(srbc) & (1u << 20)) {
			if (srbc_debug)
				pr_info("tx-isp-t32: #625 stop ack after %d polls, srbc %08x\n",
					500 - tries, readl(srbc));
			writel((readl(srbc) & ~(1u << 21)) | (1u << 22), srbc);
			writel(readl(srbc) & ~(1u << 22), srbc);
			return;
		}
		msleep(2);
	}
	t32_srbc_dump("no-ack");
#ifdef TX_ISP_T33
	/*
	 * openimp #544 (beyond vendor): on the T33L the ISP sometimes never sets
	 * bit 20 on the first reset after a warm reboot.  openimp #625: this
	 * follows a boot in which the ISP streamed (5 of 7 such boots, 1 of 12
	 * after a boot that only opened the ISP) and never happens on a reopen
	 * within a boot; CPM 0x10000000-0xfc is identical in hit and miss boots
	 * (SRBC 0 before the request).  So it is ISP state that the SoC reset
	 * does not clear; the vendor's sequence is the same and would fail
	 * there.  A soft reset pulse with the stop
	 * request still held (bit 22 for 2 ms, then bit 21 dropped) brings it
	 * back; the second stop request is then acknowledged at once and the
	 * normal reset follows.  The vendor returns -1 here and never streams.
	 */
	writel(readl(srbc) | (1u << 22), srbc);
	msleep(2);
	writel(readl(srbc) & ~(1u << 22), srbc);
	msleep(2);
	writel(readl(srbc) & ~(1u << 21), srbc);
	msleep(2);
	writel(readl(srbc) | (1u << 21), srbc);
	for (tries = 500; tries; tries--) {
		if (readl(srbc) & (1u << 20)) {
			writel((readl(srbc) & ~(1u << 21)) | (1u << 22), srbc);
			writel(readl(srbc) & ~(1u << 22), srbc);
			pr_warn("tx-isp-t32: ISP stop acknowledged after a held reset pulse (#544)\n");
			return;
		}
		msleep(2);
	}
#endif
	writel(readl(srbc) & ~(1u << 21), srbc);
	pr_warn("tx-isp-t32: ISP stop (SRBC bit 20) not acknowledged\n");
}

static int t32_isp_open(struct inode *inode, struct file *file)
{
	mutex_lock(&core.lock);
	if (!t32_clk_users++) {
		t32_srbc_dump("pre-clk");
		t32_clks_activate();
		t32_isp_soft_reset();
	}
	mutex_unlock(&core.lock);
	return 0;
}

static int t32_isp_release(struct inode *inode, struct file *file)
{
#ifdef TX_ISP_T33
	t33_calgo_release(file);	/* #805: a library its dead client left open */
#endif
	mutex_lock(&core.lock);
	if (t32_clk_users && !--t32_clk_users) {
		t33_hw_flip_stop();	/* #779: no VIC hold left over, before the clocks stop */
#ifdef TX_ISP_T33
		t33_hw_lib_deinit();	/* lib_tisp_deinit: tisp_all_reset, LDC mask */
#endif
		t32_clks_deactivate();
		t32_ivdc_unlive();	/* #828: last close without STREAMOFF, no stream runs any more */
		/*
		 * vendor tx_isp_release (tx-isp-t33.ko and tx-isp-t32.ko): the last close kfrees every MEMORY_MALLOC
		 * slot libimp left (16 slots at core + 0x170 / + 0x168); here the whole list.  Before, a killed
		 * streamer's buffers stayed until rmmod (T33 #690, T32 #694).  T32: the IQ bin is read in place only
		 * by the init paths under core.lock, which a later open runs again after the next SET_TIZIANO_PAR.
		 */
		t32_free_all();
	}
	mutex_unlock(&core.lock);
	return 0;
}

static const struct file_operations t32_isp_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = t32_isp_ioctl,
	.open = t32_isp_open,
	.release = t32_isp_release,
};

static struct miscdevice t32_isp_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "tx-isp",
	.fops = &t32_isp_fops,
};

/* ------------------------------------------------------------------ */
/* /dev/isp-m0 (tuning), /dev/misc-ivdc, /dev/framechanN              */

/*
 * Sensor frame rate (isp-m0 tuning id 0x08000070, value num << 16 | den), as
 * the vendor tisp does it: GET answers the sensor's current rate, SET hands
 * the rate to the sensor driver (TX_ISP_EVENT_SENSOR_FPS), which reprograms
 * its frame length over I2C and updates video.fps.
 */
static long t32_sensor_fps_set(struct t32_vi *vi, u32 fps)
{
	struct { s32 vinum, value; } sv = { 0, (s32)fps };
	int (*ioctl)(void *sd, unsigned int cmd, void *arg);
	void *ops, *grp;
	long ret;

	if (!vi->registered || !vi->sd)
		return -ENODEV;
	if (!(fps >> 16) || !(fps & 0xffff))
		return -EINVAL;
	ops = T32_AT(vi->sd, T32_SD_OPS, void *);
	grp = ops ? T32_AT(ops, T32_OPS_SENSOR, void *) : NULL;
	ioctl = grp ? T32_AT(grp, T32_SENSOR_OPS_IOCTL, void *) : NULL;
	if (!ioctl)
		return -ENOIOCTLCMD;
	ret = ioctl(vi->sd, T32_EVENT_SENSOR_FPS, &sv);
	isp_printf(ret ? 2 : 1, "tx-isp-t32: sensor fps %u/%u -> %ld (now %#x)\n",
		   fps >> 16, fps & 0xffff, ret,
		   T32_AT(vi->sd, T32_SENSOR_VIDEO + T32_VIDEO_FPS, u32));
	return ret ? -EINVAL : 0;
}

/* sensor exposure (TX_ISP_EVENT_SENSOR_EXPO, T32 0x02000017): value =
 * analog gain index << 16 | integration time in lines, as tisp hands it */
int t32_core_sensor_expo(u32 packed)
{
	struct t32_vi *vi = &core.vi[0];
	struct { s32 vinum, value; } sv = { 0, (s32)packed };
	int (*ioctl)(void *sd, unsigned int cmd, void *arg);
	void *ops, *grp;

	if (!vi->registered || !vi->sd)
		return -ENODEV;
	ops = T32_AT(vi->sd, T32_SD_OPS, void *);
	grp = ops ? T32_AT(ops, T32_OPS_SENSOR, void *) : NULL;
	ioctl = grp ? T32_AT(grp, T32_SENSOR_OPS_IOCTL, void *) : NULL;
	if (!ioctl)
		return -ENOIOCTLCMD;
	return ioctl(vi->sd, T32_EVENT_SENSOR_EXPO, &sv);
}

/*
 * sensor mirror / flip (TX_ISP_EVENT_SENSOR_FLIP 0x2000010, vendor tx_isp_core_shvflip through
 * ispcore_sensor_ops_ioctl): arg { ch, &msg }, msg[0] = mode (0 normal, 1 mirror, 2 flip,
 * 3 both), the sensor answers msg[1] (frames to hold, cv2005: 2) / msg[2]; -ENOIOCTLCMD counts as 0 (openimp #669)
 */
int t32_core_sensor_flip(u32 ch, u32 mode, u32 *frames)
{
	struct t32_vi *vi = &core.vi[0];
	u32 msg[3] = { mode, 0, 0 };
	struct { u32 vinum; u32 *msg; } sv = { ch, msg };
	int (*ioctl)(void *sd, unsigned int cmd, void *arg);
	void *ops, *grp;
	int r;

	if (!vi->registered || !vi->sd)
		return -ENODEV;
	ops = T32_AT(vi->sd, T32_SD_OPS, void *);
	grp = ops ? T32_AT(ops, T32_OPS_SENSOR, void *) : NULL;
	ioctl = grp ? T32_AT(grp, T32_SENSOR_OPS_IOCTL, void *) : NULL;
	if (!ioctl)
		return 0;
	r = ioctl(vi->sd, T32_EVENT_SENSOR_FLIP, &sv);
	*frames = msg[1];
	return r == -ENOIOCTLCMD ? 0 : r;
}

/*
 * #779 (beyond vendor: the vendor ispcore_irq_fd_work0, tx-isp-t33.ko 0x102f40, runs on system_wq without a
 * lock and is never flushed or cancelled): the flip work runs its VIC + sensor part under core.lock, which
 * stream-off, RELEASE_SENSOR, the sensor module removal and the last close (clock gate) hold, and rechecks
 * here that the sensor still streams with the clocks on.  No path cancels the work synchronously under
 * core.lock (the work may wait for it); rmmod cancels it after core.lock is dropped.
 */
void t32_core_lock(void)
{
	mutex_lock(&core.lock);
}

void t32_core_unlock(void)
{
	mutex_unlock(&core.lock);
}

/* under core.lock: sensor 0 registered and streaming, ISP clocks on */
int t32_core_flip_ready(void)
{
	struct t32_vi *vi = &core.vi[0];

	return vi->registered && vi->sd && vi->streaming && t32_clk_users;
}

#ifndef TX_ISP_T33
/* the sensor attribute's data type (+0x94, linear / WDR): the T32 VIC release value of a flip (#689) */
u32 t32_core_sensor_data_type(void)
{
	struct t32_vi *vi = &core.vi[0];
	void *attr;

	if (!vi->registered || !vi->sd)
		return 0;
	attr = T32_AT(vi->sd, T32_SENSOR_VIDEO + T32_VIDEO_ATTR, void *);
	return attr ? T32_AT(attr, T32_ATTR_DATA_TYPE, u32) : 0;
}
#endif

/* the sensor's frame length (VTS) from its attributes, 0 when unknown */
u32 t32_core_sensor_vts(void)
{
	struct t32_vi *vi = &core.vi[0];
	void *attr;

	if (!vi->registered || !vi->sd)
		return 0;
	attr = T32_AT(vi->sd, T32_SENSOR_VIDEO + T32_VIDEO_ATTR, void *);
	return attr ? T32_AT(attr, T32_ATTR_TOTAL_HEIGHT, u16) : 0;
}

/*
 * The sensor's alloc_again callback (attribute +0x11c, u32 (*)(u32 isp_gain,
 * u8 shift, u32 *sensor_again)) as the vendor sensor_alloc_analog_gain calls
 * it: log2 gain Q16 in, the sensor's rounded log2 gain back, its register
 * value in *reg.  Without a callback the gain stays 1x (0).
 */
u32 t32_core_sensor_alloc_again(u32 log2_q16, u32 *reg)
{
	u32 (*cb)(u32 v, u32 shift, u32 *out);
	const u8 *vin, *attr;

	*reg = 0;
	if (t32_core_sensor_ae_src(&vin, &attr))
		return 0;
	cb = T32_AT(attr, 0x11c, void *);
	return cb ? cb(log2_q16, 16, reg) : 0;
}

/* tisp_set_ae_sensor_fps -> the sensor entry +212 (TX_ISP_EVENT_SENSOR_FPS) */
int t32_core_sensor_set_fps(u32 fps)
{
	return (int)t32_sensor_fps_set(&core.vi[0], fps);
}

/* the sensor's struct tx_isp_video_in (104 B) and its attribute block, the inputs of ispcore_sync_sensor_attr */
int t32_core_sensor_ae_src(const u8 **video_in, const u8 **attr)
{
	struct t32_vi *vi = &core.vi[0];

	if (!vi->registered || !vi->sd)
		return -ENODEV;
	*video_in = (const u8 *)vi->sd + T32_SENSOR_VIDEO;
	*attr = T32_AT(vi->sd, T32_SENSOR_VIDEO + T32_VIDEO_ATTR, const u8 *);
	return *attr ? 0 : -ENODEV;
}

static long t32_m0_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct t32_vi *vi = &core.vi[0];
	u32 w[4];
	long ret = -ENOTTY;

	if (cmd == T32_IOC_TUNING_DEFAULT) {
		if (copy_from_user(w, (void __user *)arg, sizeof(w)))
			return -EFAULT;
		ret = t32_tunctl_ctrl(w, arg);	/* the vendor tuning controls (tx_isp_t32_tunctl.c) */
		if (ret != -ENOIOCTLCMD)
			goto out;
		ret = -ENOTTY;
		mutex_lock(&core.lock);
		if (w[2] == T32_TUNING_ID_SENSOR_FPS && w[1] == T32_TUNING_DIR_GET) {
			if (vi->registered && vi->sd) {
				w[3] = T32_AT(vi->sd, T32_SENSOR_VIDEO + T32_VIDEO_FPS, u32);
				ret = copy_to_user((void __user *)arg, w, sizeof(w)) ? -EFAULT : 0;
			} else {
				ret = -ENODEV;
			}
		} else if (w[2] == T32_TUNING_ID_SENSOR_FPS && w[1] == T32_TUNING_DIR_SET) {
			ret = t32_sensor_fps_set(vi, w[3]);
			/* the vendor's 0x70 then runs tisp_set_fps (T32 tx_isp_t32_ae.c #716, T33 tx_isp_t33_aec.c #725) */
#ifndef TX_ISP_T33
			if (!ret)
				t32_aec_tisp_set_fps(w[0], w[3]);
#else
			if (!ret)
				t33_aec_tisp_set_fps(w[0], w[3]);
#endif
		} else {
			/* ISP OSD: mask / draw / OSD attribute / OSD block (#336) */
			ret = t32_isposd_ctrl(w[0], w[1], w[2], w[3]);
			if (ret == -ENOIOCTLCMD)
				ret = -ENOTTY;
		}
		mutex_unlock(&core.lock);
out:
		if (t32_trace || ret)
			isp_printf(ret ? 1 : 0, "tx-isp-t32: isp-m0 tuning %s id %#x %#x -> %ld\n",
				   w[1] ? "get" : "set", w[2], w[3], ret);
		return ret;
	}
	isp_printf(1, "tx-isp-t32: isp-m0 ioctl %#x not handled\n", cmd);
	return ret;
}

static const struct file_operations t32_m0_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = t32_m0_ioctl,
};

static struct miscdevice t32_m0_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "isp-m0",
	.fops = &t32_m0_fops,
};

static long t32_ivdc_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return t32_ivdc_misc_ioctl(cmd, arg);	/* tx_isp_t32_ivdcdrv.c (#349) */
}

static const struct file_operations t32_ivdc_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = t32_ivdc_ioctl,
};

static struct miscdevice t32_ivdc_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "misc-ivdc",
	.fops = &t32_ivdc_fops,
};

/* ------------------------------------------------------------------ */
/* /proc/jz/isp                                                       */

extern struct proc_dir_entry *jz_proc_mkdir(char *s);

#ifdef TX_ISP_T33
/*
 * openimp #659: the vendor "ISP INFO" block of /proc/jz/isp/isp-m0 (vendor tx-isp-t33.ko isp_info_show 0xfc8b4
 * with its default item word 0xfffffff7: isp_info_system 0xfbf24, isp_info_sensor 0xfb4e4, isp_info_base 0xfbb6c,
 * isp_info_ae / _awb (tx_isp_t32_tunblk.c), isp_info_gib 0xfab68, then the debug parameters), labels, order
 * and units as the vendor prints them, after the driver status text.  Values come from the driver state and
 * the getters the tuning API answers from.  Not printed: "ISP Cache Line" (the vendor prints it only with a
 * cache line set), the antiflicker step list (tisp_get_antiflicker_step: no open state) and "ISP OSD DEBUG"
 * (printed by the vendor only when non-zero).  The vendor's debug counters (chans done / dq / buff, S0 / S1
 * channel state) count only in its debug mode and print 0, as here; "msca done S0" and "ISP debug CHn Done"
 * are the frame counts.  "ISP Memopt" is this driver's isp_memopt parameter (MDNS ratio in 1/1000); the
 * vendor prints its own memopt word (0x5 on the T33L).
 */
static const char *t33_info_bayer(u32 code)
{
	static const char *const bayer[4] = { "BGGR", "GBRG", "GRBG", "RGGB" };

	if (code >= 0x5200 && code <= 0x520b)
		return bayer[(code - 0x5200) & 3];
	return "The format of isp input is RGB or others!";
}

static void t33_info_show(struct seq_file *m)
{
	struct t32_vi *vi = &core.vi[0];
	const u8 *vin = vi->registered && vi->sd ? (const u8 *)vi->sd + T32_SENSOR_VIDEO : NULL;
	const u8 *attr = vin ? *(const u8 *const *)(vin + T32_VIDEO_ATTR) : NULL;
	const u8 *sinfo = vin ? *(const u8 *const *)(vin + T32_VIDEO_INFO) : NULL;
	const char *name = attr ? *(const char *const *)attr : NULL;
	const u8 *tun;
	u32 fps, vts, af[4] = { 0 }, i, fm;
	u16 blc[6];

	seq_printf(m, "********************* ISP INFO Start *********************\n");
	seq_printf(m, "**************** ISP Info ****************\n");
	seq_printf(m, "Software Version : %s\n", "open-tx-isp");
	seq_printf(m, "ISP Memopt: 0x%08x\n", isp_memopt);
	seq_printf(m, "Sensor Number : %d\n", vi->registered ? 1 : 0);
	if (vi->registered)
		seq_printf(m, "Sensor%d Name : %s\n", 0, vi->info.name);
	seq_printf(m, "**************** ISP( %d ) ****************\n", 0);
	if (!vi->streaming || !vin) {
		seq_printf(m, "This ISP( %d ) Don't Wrok\n", 0);	/* vendor text */
		goto params;
	}
	fps = *(const u32 *)(vin + T32_VIDEO_FPS);
	vts = attr ? *(const u16 *)(attr + T32_ATTR_TOTAL_HEIGHT) : 0;
	seq_printf(m, "********* Sensor Info *********\n");
	seq_printf(m, "Sensor Name : %s\n", name ? name : vi->info.name);
	seq_printf(m, "Sensor OUTPUT Width : %d\n", *(const u32 *)(vin + T32_VIDEO_MBUS_WIDTH));
	seq_printf(m, "Sensor OUTPUT Height : %d\n", *(const u32 *)(vin + T32_VIDEO_MBUS_HEIGHT));
	seq_printf(m, "Sensor OUTPUT FPS : %d / %d\n", fps >> 16, fps & 0xffff);
	seq_printf(m, "Sensor OUTPUT Bayer : %s\n", t33_info_bayer(*(const u32 *)(vin + T32_VIDEO_MBUS_CODE)));
	seq_printf(m, "Sensor DFBOOT : %d\n", sinfo ? *(const u32 *)(sinfo + 0x68) : 0);
	seq_printf(m, "Sensor HTS : %d\n", attr ? *(const u16 *)(attr + T32_ATTR_TOTAL_WIDTH) : 0);
	seq_printf(m, "Sensor VTS : %d\n", vts);
	/* vendor: (1000000 / num * den) / VTS, no zero check; here 0 for a zero rate or VTS */
	seq_printf(m, "Sensor one line expo time : %dus\n",
		   (fps >> 16) && vts ? (1000000 / (fps >> 16)) * (fps & 0xffff) / vts : 0);
	seq_printf(m, "Expo Fs : %d\n", attr ? *(const u32 *)(attr + 0x100) : 0);

	seq_printf(m, "**************** Base Info ****************\n");
	seq_printf(m, "ISP Runing Mode : %s\n", core.bin_night[0] == 0 ? "day" : core.bin_night[0] == 1 ? "night" : "custom");
	tun = t33_info_tuning();
	seq_printf(m, "Saturation : %d\n", tun ? tun[0] : 0);
	seq_printf(m, "Sharpness : %d\n", tun ? tun[8] : 0);
	seq_printf(m, "Contrast : %d\n", tun ? tun[4] : 0);
	seq_printf(m, "Brightness : %d\n", tun ? tun[12] : 0);
	seq_printf(m, "Hue : %d\n", tun ? tun[16] : 0);
	seq_printf(m, "Wdr En : %d\n", core.bin_wdr[0]);
	seq_printf(m, "Bypass : %d\n", 0);
	fm = t33_hw_flip_mode();
	for (i = 0; i < 3; i++)	/* vendor: the flip state of the same ISP three times */
		seq_printf(m, "ISP FLIP MODE CH%d : %s\n", i,
			   (fm & 3) == 3 ? "mirror and flip" : (fm & 1) ? "mirror" : (fm & 2) ? "flip" : "nomal");
	t33_aec_api_i9j(0, 1, af);	/* tisp_g_antiflick: mode, frequency */
	seq_printf(m, "Antiflicker : %s freq: %d\n", af[0] == 0 ? "disable" : af[0] == 2 ? "auto" : "normal",
		   af[0] ? af[1] : 0);
	for (i = 0; i < 3; i++)
		seq_printf(m, "ISP debug CH%d Done: %d\n", i, t32_fs_frames(i));

	t33_info_ae_awb(m, 0);

	seq_printf(m, "********* GIB Info *********\n");
	memset(blc, 0, sizeof(blc));
	t33_hw_info_gib(blc);
	seq_printf(m, "GIB R  : %d\n", blc[1]);
	seq_printf(m, "GIB GR : %d\n", blc[2]);
	seq_printf(m, "GIB GB : %d\n", blc[3]);
	seq_printf(m, "GIB B  : %d\n", blc[4]);
	seq_printf(m, "GIB IR : %d\n", blc[5]);
params:
	seq_printf(m, "********* Debug Parameters *********\n");
	seq_printf(m, "Vinum:%d\n", 0);
	seq_printf(m, "Debug Type:%d\n", 0);
	seq_printf(m, "Items:0x%08x\n", 0xfffffff7);
	seq_printf(m, "Interval:%d\n", 0);
	{
		static const char *const rows[5] = { "chans done cnt", "chans dq cnt", "chans buff cfg",
						     "chans buff cnt", "chans buff null cnt" };
		u32 r;

		for (r = 0; r < 5; r++) {
			seq_printf(m, "%s%s\n", r ? "\n" : "", rows[r]);
			for (i = 0; i < 12; i++)
				seq_printf(m, "%d ", 0);
		}
		seq_printf(m, "\nmsca done:S0, S1, S2, S3\n%d %d %d %d ", t32_hw_stats.frame_ends, 0, 0, 0);
		for (r = 0; r < 2; r++) {
			seq_printf(m, "\nS%d:en, c0y, c1y, c2y, c0uv, c1uv, c2uv\n", r);
			for (i = 0; i < 7; i++)
				seq_printf(m, "%d ", 0);
		}
	}
	seq_printf(m, "\n********************* ISP INFO End *********************\n");
}
#endif

static int t32_proc_show(struct seq_file *m, void *v)
{
	struct t32_vi *vi = &core.vi[0];

	seq_printf(m, T32_SOC_DRV " (open, S3: frames on 3 channels, sensor fps, no tisp)\n");
	seq_printf(m, "sensor: %s registered %d input %d streaming %d %ux%u\n",
		   vi->registered ? vi->info.name : "-", vi->registered,
		   vi->input_on, vi->streaming, vi->width, vi->height);
	seq_printf(m, "isp mem pa %#x size %#x, iq pa %#x, ioctls %u, memopt %u\n",
		   vi->isp_mem_pa, vi->isp_mem_size, core.tiziano_pa, core.ioctls,
		   isp_memopt);
#ifdef TX_ISP_T33
	{
		struct t32_alloc *al;
		u32 n = 0, kb = 0;

		T32_ALLOC_LOCK();
		list_for_each_entry(al, &core.allocs, node) {
			n++;
			kb += (PAGE_SIZE << al->order) >> 10;
		}
		T32_ALLOC_UNLOCK();
		seq_printf(m, "memory malloc: live %u (%u KB), iq bin copy %u bytes, copies %u\n",
			   n, kb, t33_bin_cur_bytes, t33_bin_copies);
	}
#endif
	seq_printf(m, "irqs isp %u vic %u frame ends %u stream ons %u\n",
		   t32_hw_stats.isp_irqs, t32_hw_stats.vic_irqs,
		   t32_hw_stats.frame_ends, t32_hw_stats.stream_ons);
	seq_printf(m, "statistics: inits %u, irqs ae %u ae-hist %u awb %u\n",
		   t32_hw_stats.stats_inits, t32_hw_stats.ae_irqs, t32_hw_stats.ae_hist_irqs,
		   t32_hw_stats.awb_irqs);
	seq_printf(m, "bring-up controller (not the vendor AE): steps %u, mean %u/16, it %u lines\n",
		   t32_hw_stats.bringup_steps, t32_hw_stats.bringup_mean16, t32_hw_stats.bringup_it);
	seq_printf(m, "per-frame ported paths: runs %u, writes %u\n",
		   t32_pf_stats.runs, t32_pf_stats.writes);
	seq_printf(m, "block irqs: lce attaches %u, irqs lce %u lsf %u, events %u (queue full %u)\n",
		   t32_blk_stats.attaches, t32_blk_stats.lce_irqs, t32_blk_stats.lsf_irqs, t32_blk_stats.events,
		   t32_blk_stats.ev_full);
	seq_printf(m, "tmo: attaches %u, irqs tmo %u fbf %u, tgain updates %u\n",
		   t32_tmo_stats.attaches, t32_tmo_stats.tmo_irqs, t32_tmo_stats.fbf_irqs, t32_tmo_stats.tgains);
	seq_printf(m, "af / gsm: attaches %u / %u, irqs %u / %u, events %u / %u (queue full %u)\n",
		   t32_afg_stats.af_attaches, t32_afg_stats.gsm_attaches, t32_afg_stats.af_irqs, t32_afg_stats.gsm_irqs,
		   t32_afg_stats.af_events, t32_afg_stats.gsm_events, t32_afg_stats.ev_full);
	seq_printf(m, "vendor AE: frames %u events %u writes %u expo %u fps %u; mean %u target %u it %u again %u dgain %u tgain %#x\n",
		   t32_aec_stats.frames, t32_aec_stats.events, t32_aec_stats.writes, t32_aec_stats.expo_sets,
		   t32_aec_stats.fps_sets, t32_aec_stats.mean, t32_aec_stats.target, t32_aec_stats.it,
		   t32_aec_stats.again, t32_aec_stats.dgain, t32_aec_stats.tgain);
#ifndef TX_ISP_T33
	seq_printf(m, "vendor AE face: sets %u en %u zones h %u..%u v %u..%u target %u on %u\n",
		   t32_aec_stats.face_sets, t32_aec_stats.face_en, t32_aec_stats.face_zone & 0xff,
		   t32_aec_stats.face_zone >> 8 & 0xff, t32_aec_stats.face_zone >> 16 & 0xff,
		   t32_aec_stats.face_zone >> 24, t32_aec_stats.face_target & 0xff, t32_aec_stats.face_target >> 8);
	seq_printf(m, "vendor AE sensor sync: syncs %u fps word %#x\n", t32_aec_stats.syncs, t32_aec_stats.sync_fps);
#endif
	seq_printf(m, "vendor AWB: frames %u writes %u refused %u; ct %u gain r %u b %u; ev %u tgain %u updates\n",
		   t32_awbc_stats.frames, t32_awbc_stats.writes, t32_awbc_stats.refused, t32_awbc_stats.ct,
		   t32_awbc_stats.gain_r, t32_awbc_stats.gain_b, t32_awbc_stats.ev_updates,
		   t32_awbc_stats.tgain_updates);
	seq_printf(m, "ct fan-out (CCM, BCSH, CLM): runs %u writes %u last ct %u; ev fan-out runs %u\n",
		   t32_ctup_stats.runs, t32_ctup_stats.writes, t32_ctup_stats.ct, t32_ctup_stats.ev_runs);
	seq_printf(m, "LSC ct path: attaches %u ct calls %u mesh writes %u last ct %u tgains %u; frame done %u list runs %u; waits %u timeouts %u\n",
		   t32_lsc_drv_stats.attaches, t32_lsc_drv_stats.ct_calls, t32_lsc_drv_stats.mesh_writes,
		   t32_lsc_drv_stats.ct, t32_lsc_drv_stats.tgains, t32_lsc_drv_stats.frames,
		   t32_lsc_drv_stats.list_runs, t32_lsc_drv_stats.waits, t32_lsc_drv_stats.timeouts);
#ifndef TX_ISP_T33
	seq_printf(m, "LSC hvflip: flips %u mirrors %u mf %u\n", t32_lsc_drv_stats.flips, t32_lsc_drv_stats.mirrors,
		   t32_lsc_drv_stats.mf);
#endif
	seq_printf(m, "sensor fps %#x, stream-on list runs %u (last %u words) timeouts %u\n",
		   t32_core_sensor_fps(), t32_rcfg_runs, t32_rcfg_last_len, t32_rcfg_timeouts);
#ifdef TX_ISP_T33
	t33_stats_show(m);
#else
	t32_flip_show(m);	/* #689 */
#endif
	t32_fs_show(m);
	t32_snapldc_show(m);
	t32_ivdc_show(m);
#ifdef TX_ISP_T33
	t33_info_show(m);	/* #659 */
#endif
	return 0;
}

u32 t32_core_sensor_fps(void)
{
	struct t32_vi *vi = &core.vi[0];

	if (!vi->registered || !vi->sd)
		return 0;
	return T32_AT(vi->sd, T32_SENSOR_VIDEO + T32_VIDEO_FPS, u32);
}

/* the IQ bin libimp loaded (SET_TIZIANO_PAR): uncached view and the bytes
 * up to the end of its buffer; -ENOENT when there is none */
int t32_core_iq_bin(const u8 **bin, u32 *bytes)
{
#ifdef TX_ISP_T33
	int r = -ENOENT;

	/* the driver's copy of the last SET_TIZIANO_PAR bin (#684); kept over libimp's FREE and the release */
	T32_ALLOC_LOCK();
	if (t33_bin_cur) {
		*bin = t33_bin_cur;
		*bytes = t33_bin_cur_bytes;
		r = 0;
	}
	T32_ALLOC_UNLOCK();
	return r;
#else
	struct t32_alloc *a;

	if (!core.tiziano_pa)
		return -ENOENT;
	list_for_each_entry(a, &core.allocs, node)
		if (core.tiziano_pa >= a->pa && core.tiziano_pa < a->pa + a->size) {
			*bin = (const u8 *)CKSEG1ADDR(core.tiziano_pa);
			*bytes = a->pa + a->size - core.tiziano_pa;
			return 0;
		}
	return -ENOENT;
#endif
}

/* the sensor's media bus code (input_mbus_fmt) of the running input */
int t32_core_sensor_mbus(u32 *code)
{
	struct t32_vi *vi = &core.vi[0];

	if (!vi->registered || !vi->input_on)
		return -ENODEV;
	*code = T32_AT(vi->sd, T32_SENSOR_VIDEO + T32_VIDEO_MBUS_CODE, u32);
	return 0;
}

#ifdef TX_ISP_T33
/* the sensor bus (attribute dbus_type: 1 MIPI, 2 DVP), 0 when unknown: /proc/tisp VIC info (#589) */
u32 t33_core_sensor_dbus(void)
{
	struct t32_vi *vi = &core.vi[0];
	void *attr;

	if (!vi->registered || !vi->sd)
		return 0;
	attr = T32_AT(vi->sd, T32_SENSOR_VIDEO + T32_VIDEO_ATTR, void *);
	return attr ? T32_AT(attr, T32_ATTR_DBUS_TYPE, u32) : 0;
}
#endif

/* the MSCA input: the sensor picture of the running input */
int t32_core_sensor_size(u32 *w, u32 *h)
{
	struct t32_vi *vi = &core.vi[0];

	if (!vi->registered || !vi->input_on || !vi->width || !vi->height)
		return -ENODEV;
	*w = vi->width;
	*h = vi->height;
	return 0;
}

/* the MDNS compression setting (1/1000) */
unsigned int t32_core_memopt(void)
{
	return isp_memopt;
}

/* the ISP working memory libimp handed over (SET_ISP_MEM_INFO) */
int t32_core_isp_mem(u32 *pa)
{
	struct t32_vi *vi = &core.vi[0];

	if (!vi->isp_mem_pa || !vi->isp_mem_size)
		return -ENODEV;
	*pa = vi->isp_mem_pa;
	return 0;
}

static int t32_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, t32_proc_show, NULL);
}

static const struct file_operations t32_proc_fops = {
	.owner = THIS_MODULE,
	.open = t32_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

/* ------------------------------------------------------------------ */

int tx_isp_t32_core_init(void)
{
	int ret, i;

	mutex_init(&core.lock);
	INIT_LIST_HEAD(&core.allocs);
	t32_srbc_dump("load");

	t32_clks_probe();
	ret = misc_register(&t32_isp_misc);
	if (ret) {
		t32_clks_put();
		return ret;
	}
	ret = misc_register(&t32_m0_misc);
	if (ret)
		goto err_m0;
	ret = misc_register(&t32_ivdc_misc);
	if (ret)
		goto err_ivdc;
	ret = t32_fs_init();
	if (ret)
		goto err_fc;
	ret = t32_hw_init();
	if (ret)
		goto err_hw;
	ret = t32_ivdc_init();
	if (ret) {
		t32_hw_exit();
		goto err_hw;
	}
	ret = t32_snapldc_init();
	if (ret) {
		t32_ivdc_exit();
		t32_hw_exit();
		goto err_hw;
	}
	core.proc = jz_proc_mkdir("isp");
	if (core.proc) {
		proc_create("isp-m0", 0444, core.proc, &t32_proc_fops);
		proc_create("isp-w02", 0444, core.proc, &t32_proc_fops);
	}
#ifdef TX_ISP_T33
	if (t33_tisp_proc_init())	/* the vendor tisp debug view (#589) */
		pr_warn(T32_SOC_DRV ": /proc/tisp not created\n");
#endif
	pr_info(T32_SOC_DRV ": open driver loaded (S3: 3 channels, sensor fps, no tisp) (memopt %u)\n", isp_memopt);
	return 0;

err_hw:
	t32_fs_exit();
err_fc:
	misc_deregister(&t32_ivdc_misc);
err_ivdc:
	misc_deregister(&t32_m0_misc);
err_m0:
	misc_deregister(&t32_isp_misc);
	t32_clks_put();
	return ret;
}

void tx_isp_t32_core_exit(void)
{
	int i;

	t32_fs_stop_all();
	mutex_lock(&core.lock);
	for (i = 0; i < T32_MAX_VI; i++) {
		if (core.vi[i].streaming)
			t32_sensor_stream(&core.vi[i], 0);
		t32_vi_release(&core.vi[i]);
	}
	t32_free_all();
	mutex_unlock(&core.lock);
#ifdef TX_ISP_T33
	t33_tisp_proc_exit();
#endif
	if (core.proc) {
		remove_proc_entry("isp-m0", core.proc);
		remove_proc_entry("isp-w02", core.proc);
		proc_remove(core.proc);
	}
	t32_snapldc_exit();
	t32_ivdc_exit();
	t32_hw_exit();
#ifdef TX_ISP_T33
	vfree(t33_bin_prev);	/* after the block works are gone (t32_hw_exit) */
	vfree(t33_bin_cur);
	t33_bin_prev = t33_bin_cur = NULL;
#endif
	t32_fs_exit();
	misc_deregister(&t32_ivdc_misc);
	misc_deregister(&t32_m0_misc);
	misc_deregister(&t32_isp_misc);
	t32_clks_put();
}
