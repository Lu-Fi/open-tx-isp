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
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/gfp.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/platform_device.h>
#include <linux/clk.h>

#include "tx_isp_t32.h"
#include "tx_isp_t32_abi.h"
#include "tx_isp_t32_regs.h"
#include "tx_isp_t32_mem.h"
#include "tx_isp_t32_msca.h"
#include "tx_isp_t32_soc.h"

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
	u32 bypass[T32_MAX_VI];		/* BYPASS_SENSOR {enable, input} */
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
	int i;

	mutex_lock(&core.lock);
	for (i = 0; i < T32_MAX_VI; i++) {
		struct t32_vi *vi = &core.vi[i];

		if (vi->client && vi->client->dev.driver == &drv->driver) {
			vi->sd = NULL;
			vi->registered = 0;
			vi->streaming = 0;
		}
	}
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
		if (init)
			ret = init(vi->sd, &arg);
		if (!ret && stream)
			ret = stream(vi->sd, &arg);
		if (!ret) {
			t32_ivdc_stream(1);
			t32_hw_stream_on();
		}
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
	p = __get_free_pages(GFP_KERNEL | __GFP_ZERO, a->order);
	if (!p) {
		kfree(a);
		return -ENOMEM;
	}
	a->kva = (void *)p;
	a->pa = virt_to_phys(a->kva);
	a->size = bi->size;
	list_add_tail(&a->node, &core.allocs);
	bi->word0 = (u32)p;
	bi->word1 = a->pa;
	return 0;
}

static int t32_free(const struct t32_buf_info *bi)
{
	struct t32_alloc *a = t32_alloc_find_kva(bi->word0);

	if (!a)
		return -EINVAL;
	if (core.tiziano_pa >= a->pa && core.tiziano_pa < a->pa + a->size)
		core.tiziano_pa = 0;
	list_del(&a->node);
	free_pages((unsigned long)a->kva, a->order);
	kfree(a);
	return 0;
}

static void t32_free_all(void)
{
	struct t32_alloc *a, *n;

	list_for_each_entry_safe(a, n, &core.allocs, node) {
		list_del(&a->node);
		free_pages((unsigned long)a->kva, a->order);
		kfree(a);
	}
	core.tiziano_pa = 0;
}

static int t32_set_tiziano_par(const struct t32_tiziano_par *par)
{
	u32 pa = par->pa;
	struct t32_alloc *a = t32_alloc_find_pa(pa);
	const u32 *w;

	if (!a)
		return -EINVAL;
	core.tiziano_pa = pa;
	if (par->vinum < T32_BIN_VIS) {
		core.bin_wdr[par->vinum] = par->wdr;
		core.bin_night[par->vinum] = par->night;
	}
	/* libimp wrote the bin through an uncached /dev/mem mapping */
	w = (const u32 *)CKSEG1ADDR(pa);
	isp_printf(0, "tx-isp-t32: IQ parameters at pa %#x: %08x %08x %08x %08x \n",
		   pa, w[0], w[1], w[2], w[3]);
	return 0;
}

/* ------------------------------------------------------------------ */
/* /dev/tx-isp                                                        */

#define T32_GET(arg, var) (copy_from_user(&(var), (void __user *)(arg), sizeof(var)) ? -EFAULT : 0)
#define T32_PUT(arg, var) (copy_to_user((void __user *)(arg), &(var), sizeof(var)) ? -EFAULT : 0)

/*
 * Vendor /dev/tx-isp commands the vendor libimp 2.0.2.1 issues (openimp #429):
 *
 * G_REGISTER 0x8040540e (tx_isp_sensor_g_register, T33 vendor module
 * 0x267a0 branch at 0x8040540e): 64 B block copied in, sensor ioctl event
 * 0x02000012, block copied out; a sensor without a handler (-ENOIOCTLCMD
 * -515) is skipped, any other error is returned, no sensor answers 0.
 */
/* the sensor picture size of the running input (T33 OSD bounds) */
void t32_core_in_size(u32 *w, u32 *h)
{
	*w = core.vi[0].width;
	*h = core.vi[0].height;
}

static long t32_sensor_g_register(struct t32_vi *vi, unsigned long arg)
{
	u8 b[64];
	int (*ioctl)(void *sd, unsigned int cmd, void *arg);
	void *ops, *grp;

	if (copy_from_user(b, (void __user *)arg, sizeof(b))) {
		isp_printf(2, "[ %s:%d ] copy from user error\n", "tx_isp_sensor_g_register", 0x3c4);
		return -EFAULT;
	}
	if (vi->registered && vi->sd) {
		ops = T32_AT(vi->sd, T32_SD_OPS, void *);
		grp = ops ? T32_AT(ops, T32_OPS_SENSOR, void *) : NULL;
		ioctl = grp ? T32_AT(grp, T32_SENSOR_OPS_IOCTL, void *) : NULL;
		if (ioctl) {
			int r = ioctl(vi->sd, T32_EVENT_SENSOR_GET_REG, b);

			if (r && r != -ENOIOCTLCMD)
				return r;
		}
	}
	if (copy_to_user((void __user *)arg, b, sizeof(b))) {
		isp_printf(2, "[ %s:%d ] copy from user error\n", "tx_isp_sensor_g_register", 0x3d2);
		return -EFAULT;
	}
	return 0;
}

/*
 * S_REGISTER 0xc040540d (tx_isp_sensor_s_register, T33 vendor module 0x267a0,
 * line 0x3a9): 64 B block in, sensor event 0x02000011, ENOIOCTLCMD skipped,
 * other errors returned, nothing copied back, no sensor answers 0.
 */
static long t32_sensor_s_register(struct t32_vi *vi, unsigned long arg)
{
	u8 b[64];
	int (*ioctl)(void *sd, unsigned int cmd, void *arg);
	void *ops, *grp;

	if (copy_from_user(b, (void __user *)arg, sizeof(b))) {
		isp_printf(2, "[ %s:%d ] copy from user error\n", "tx_isp_sensor_s_register", 0x3a9);
		return -EFAULT;
	}
	if (vi->registered && vi->sd) {
		ops = T32_AT(vi->sd, T32_SD_OPS, void *);
		grp = ops ? T32_AT(ops, T32_OPS_SENSOR, void *) : NULL;
		ioctl = grp ? T32_AT(grp, T32_SENSOR_OPS_IOCTL, void *) : NULL;
		if (ioctl) {
			int r = ioctl(vi->sd, T32_EVENT_SENSOR_SET_REG, b);

			if (r && r != -ENOIOCTLCMD)
				return r;
		}
	}
	return 0;
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

static long t32_frame_drop_get(unsigned long arg)
{
	u32 b[T32_FD_BLOCK / 4];
	int k;

	if (copy_from_user(b, (void __user *)arg, sizeof(b))) {
		isp_printf(2, "[ %s:%d ] copy from user error\n", "tx_isp_get_frame_drop", 0x7a6);
		return -EFAULT;
	}
	for (k = 0; k < T32_FD_OUTPUTS; k++) {
		u32 *e = &b[1 + 3 * k];

		*(u8 *)(e + 1) = (u8)t32_hw_rd(T32_MSCA_FDROP_SKIP(k));
		e[2] = t32_hw_rd(T32_MSCA_FDROP_MASK(k));
		e[0] = 1;
	}
	if (copy_to_user((void __user *)arg, b, sizeof(b))) {
		isp_printf(2, "[ %s:%d ] copy to user error\n", "tx_isp_get_frame_drop", 0x7b5);
		return -EFAULT;
	}
	return 0;
}

static long t32_isp_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct t32_vi *vi = &core.vi[0];
	struct t32_buf_info bi;
	struct t32_initarg ia;
	u32 v;
	long ret = 0;

	mutex_lock(&core.lock);
	core.ioctls++;
	switch (cmd) {
	case T32_IOC_DRIVER_VERSION: {
		char ver[64];

		memset(ver, 0, sizeof(ver));
		strlcpy(ver, driver_soc, 32);
		strlcpy(ver + 32, driver_version, 32);
		ret = T32_PUT(arg, ver);
		break;
	}
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

				ret = attr ? t32_hw_input_setup(attr, vi->width, vi->height) : -ENODEV;
				if (ret)
					break;
			}
			vi->input_on = 1;
			ia.vinum = (s32)((vi->width << 16) | vi->height);
		} else {
			vi->input_on = 0;
			ia.vinum = 0;
		}
		ret = T32_PUT(arg, ia);
		break;
	case T32_IOC_GET_ISP_IPC_MEM_INFO:
		ret = T32_GET(arg, bi);
		if (!ret) {
			bi.size = 0;		/* no IPC (RISC-V) memory in S1 */
			ret = T32_PUT(arg, bi);
		}
		break;
	case T32_IOC_SET_ISP_IPC_MEM_INFO:
		ret = T32_GET(arg, bi);
		if (!ret) {
			vi->ipc_mem_pa = bi.word1;
			vi->ipc_mem_size = bi.size;
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
		bi.size = t32_isp_mem_size(vi->width, vi->height, isp_memopt, 0, 0);
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
		ret = t32_sensor_s_register(vi, arg);
		break;
	case T32_IOC_GET_SENSOR_REGISTER:
		ret = t32_sensor_g_register(vi, arg);
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
		if (arg == 0 || arg == 1)
			core.run_mode = arg;
		else if (arg == 2 || arg == 3)
			ret = core.run_mode == 1 ? 0 : -EINVAL;
		else
			ret = -EINVAL;
		break;
	case T32_IOC_LDC_MODE:
		/* broadcast to the subdevices, sum of the results (0): the mode is kept */
		ret = T32_GET(arg, v);
		if (!ret)
			core.ldc_mode = v;
		break;
	case T32_IOC_BYPASS_SENSOR: {
		/* tx_isp_bypass_all: {enable, input} kept per input (libimp
		 * IMP_ISP_SetISPBypass); the sensor-side switch of the vendor's
		 * notify is not modelled (QEMU proof only, picture: camera) */
		u32 b[2];

		if (copy_from_user(b, (void __user *)arg, sizeof(b))) {
			ret = -EFAULT;
			break;
		}
		if (b[1] < T32_MAX_VI)
			core.bypass[b[1]] = b[0];
		break;
	}
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
MODULE_PARM_DESC(isp_cache_line, "isp cache line (accepted, unused)");
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

static void t32_clks_activate(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(t32_clk_parents); i++) {
		const struct t32_clk_parent *p = &t32_clk_parents[i];
		const char *pname = *p->param && **p->param ? *p->param : p->parent;
		struct clk *mux = clk_get(NULL, p->mux);
		struct clk *par = clk_get(NULL, pname);

		if (!IS_ERR(mux) && !IS_ERR(par) && clk_set_parent(mux, par))
			pr_warn("tx-isp-t32: %s parent %s not set\n", p->mux, pname);
		if (!IS_ERR(par))
			clk_put(par);
		if (!IS_ERR(mux))
			clk_put(mux);
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

static int t32_isp_open(struct inode *inode, struct file *file)
{
	mutex_lock(&core.lock);
	if (!t32_clk_users++)
		t32_clks_activate();
	mutex_unlock(&core.lock);
	return 0;
}

static int t32_isp_release(struct inode *inode, struct file *file)
{
	mutex_lock(&core.lock);
	if (t32_clk_users && !--t32_clk_users)
		t32_clks_deactivate();
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
	seq_printf(m, "sensor fps %#x, stream-on list runs %u (last %u words) timeouts %u\n",
		   t32_core_sensor_fps(), t32_rcfg_runs, t32_rcfg_last_len, t32_rcfg_timeouts);
#ifdef TX_ISP_T33
	t33_stats_show(m);
#endif
	t32_fs_show(m);
	t32_ivdc_show(m);
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
	core.proc = jz_proc_mkdir("isp");
	if (core.proc) {
		proc_create("isp-m0", 0444, core.proc, &t32_proc_fops);
		proc_create("isp-w02", 0444, core.proc, &t32_proc_fops);
	}
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
	if (core.proc) {
		remove_proc_entry("isp-m0", core.proc);
		remove_proc_entry("isp-w02", core.proc);
		proc_remove(core.proc);
	}
	t32_ivdc_exit();
	t32_hw_exit();
	t32_fs_exit();
	misc_deregister(&t32_ivdc_misc);
	misc_deregister(&t32_m0_misc);
	misc_deregister(&t32_isp_misc);
	t32_clks_put();
}
