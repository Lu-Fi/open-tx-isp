/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 tx-isp device ioctl (vendor tx_isp_unlocked_ioctl, the /dev/tx-isp
 * VIDIOC_* dispatcher of the ISP device).
 *
 * file == 0: kernel call, the ISP device is the vendor global da784 and +60
 * (kernel caller flag) is set to 1; else the device is file +120 (private
 * data) - 12 and +60 is cleared.  Unknown commands return 0.
 *
 * Command groups (each proven by tests/t32_oracle/t32_txuio_oracle against
 * the vendor flow, Matt Davis' recovered C, PR #15):
 *   helpers  driver version, default bin path set / get, sensor register /
 *            release, multi-sensor mode, AE algorithm open, video s_stream /
 *            link stream (tx_isp_t32_txioctl.h);
 *   core     core ioctl (subdevice ops +0, +32) broadcast to the 16
 *            subdevices +84 .. +144 in three vendor flavours;
 *   sensor   bypass all, sensor enum / get / set input, sensor s / g
 *            register: the sensor ioctl (ops +12, +8) of every subdevice;
 *   memory   ISP memory space set / get, WDR buffer set / get;
 *   links    video link setup (vendor tx_isp_video_link_setup with
 *            subdev_video_setup_link inlined) and destroy;
 *   algo     AE algorithm close, AWB algorithm open / close and core
 *            command 0x100001b over the core ioctl broadcast;
 *   drop     frame drop set / get and set / get once (core device +1132
 *            channel table, 3 channels per input; the tisp frame drop
 *            functions when core +1196 is 1);
 *   misc     night mode start, PM control / all done, events 0x100000a /
 *            0x100000b and tiziano parameters (subdevice 0 notify), running
 *            mode read;
 *   ae / gpio AE start EV set / get (AE scene attribute), GPIO state switch
 *            (+ trigger) via subdevice 0's core ioctl, default bin, WDR enable;
 *   ipc / mem ISP IPC memory space set / get, managed memory malloc / free
 *            (16 slots { ptr, phys, size } at +372).
 */
#ifndef TX_ISP_T32_TXUIOCTL_H
#define TX_ISP_T32_TXUIOCTL_H

#include "tx_isp_t32_txos.h"
#include "tx_isp_t32_txcore.h"
#include "tx_isp_t32_txioctl.h"
#include "tx_isp_t32_txlink.h"
#include "tx_isp_t32_txbin.h"

#define T32_FILE_PRIV		120U	/* file->private_data: ISP device + 12 */
#define T32_FILE_PRIV_ADJ	12U

/* VIDIOC_* command words of the ISP device */
#define T32_UIO_VERSION		0x80045401U
#define T32_UIO_SENSOR_REG	0x80745405U
#define T32_UIO_SENSOR_REL	0x80745406U
#define T32_UIO_S_STREAM	0x80085407U
#define T32_UIO_S_STREAM2	0x80085408U
#define T32_UIO_LINK_STREAM	0xc008540bU
#define T32_UIO_LINK_STREAM2	0xc008540cU
#define T32_UIO_MULTISENSOR	0xc0045415U
#define T32_UIO_CORE_16		0xc004541bU
#define T32_UIO_CORE_17		0xc004541cU
#define T32_UIO_AE_ALGO		0xc004541dU
#define T32_UIO_CORE_1A		0xc0045423U
#define T32_UIO_SET_BINPATH	0xc004542aU
#define T32_UIO_GET_BINPATH	0xc004542bU
#define T32_UIO_CORE_SUM	0xc00454b8U
#define T32_UIO_BYPASS		0x80085418U
#define T32_UIO_ENUM_INPUT	0xc0045402U
#define T32_UIO_GET_INPUT	0x80085403U
#define T32_UIO_SET_INPUT	0xc0085404U
#define T32_UIO_S_REGISTER	0xc040540dU
#define T32_UIO_G_REGISTER	0x8040540eU
#define T32_UIO_SET_ISP_MEM	0x800c540fU
#define T32_UIO_GET_ISP_MEM	0x800c5410U
#define T32_UIO_WDR_SET_BUF	0x800c5411U
#define T32_UIO_WDR_GET_BUF	0x800c5412U
#define T32_UIO_LINK_SETUP	0x80085409U
#define T32_UIO_LINK_DESTROY	0x8008540aU
#define T32_UIO_AE_CLOSE	0xc004541eU
#define T32_UIO_CORE_1B		0xc0045424U
#define T32_UIO_AWB_OPEN	0xc0045425U
#define T32_UIO_AWB_CLOSE	0xc0045426U
#define T32_UIO_SET_FDROP	0xc004542cU
#define T32_UIO_GET_FDROP	0xc004542dU
#define T32_UIO_SET_FDROP1	0xc00454cdU
#define T32_UIO_GET_FDROP1	0xc00454ceU
#define T32_UIO_NIGHT_MODE	0xc004542eU
#define T32_UIO_GET_PM_CTRL	0xc0045432U
#define T32_UIO_PM_ALL_DONE	0xc0045433U
#define T32_UIO_SD_EV_0A	0xc004548eU
#define T32_UIO_SD_EV_0B	0xc004548fU
#define T32_UIO_RUN_MODE	0xc004543dU
#define T32_UIO_TIZIANO_PAR	0xc00454c8U
#define T32_UIO_SET_AE_EV	0xc0045436U
#define T32_UIO_GET_AE_EV	0xc0045437U
#define T32_UIO_GPIO_SWITCH	0xc00454cbU
#define T32_UIO_GPIO_TRIGGER	0xc00454ccU
#define T32_UIO_DEFAULT_BIN	0xc00454c9U
#define T32_UIO_WDR_ENABLE	0xc00454caU
#define T32_UIO_SET_IPC_MEM	0x800c5438U
#define T32_UIO_GET_IPC_MEM	0x800c5439U
#define T32_UIO_MEM_MALLOC	0x800c543aU
#define T32_UIO_MEM_FREE	0x800c543bU

#define T32_NOTIFY_IPC		0x5000007U
#define T32_CORE_IPC_FIX	1200U	/* core +1200 == 1: +1204 is the IPC line count */
#define T32_CORE_IPC_LINES	1204U
#define T32_SENSOR_IPC_VAL	316U
#define T32_ISP_MEM_SLOT	372U
#define T32_ISP_MEM_SLOTS	16U
#define T32_MEM_SLOT_BYTES	12U
#define T32_GFP_MEM		0x24000c0U
#define T32_PHYS_OFF		0x80000000U

u32 t32_tx_os_get_isp_cache_line(void);
void t32_tx_os_lib_tisp_set_isp_ipc_memory_space(u32 sel, u32 v);
u32 t32_tx_os_lib_tisp_get_isp_ipc_memory_space(u32 sel, void *opt);
u32 t32_tx_os_kmalloc(u32 n, u32 flags);

#define T32_CORE_GPIO		0x100000cU
#define T32_CORE_GPIO_TRIG	0x100000dU
#define T32_ENODEV		19U
#define T32_AE_SCENE_BYTES	80U
#define T32_AE_SCENE_EV		6U	/* scene words 6, 7: start EV */
#define T32_CORE_WDR_MODE	1180U
#define T32_WDR_MODE_ON		16U

int t32_tx_os_tisp_g_ae_scence_attr(u32 ch, void *b);
int t32_tx_os_tisp_s_ae_scence_attr(u32 ch, void *b);

#define T32_NOTIFY_PM_DONE	0x1000005U
#define T32_NOTIFY_0A		0x100000aU
#define T32_NOTIFY_0B		0x100000bU
#define T32_NOTIFY_TIZIANO	0x100001eU
#define T32_CORE_NIGHT		1164U	/* core +1164 + 4 input: night mode request (vendor 4 * (input + 290) + 4) */
#define T32_NIGHT_START		10U
#define T32_CORE_PM_CTRL	4020U
#define T32_ISP_RUN_MODE	64U
#define T32_ENOENT		2U
#define T32_ENOMEM		12U

/* core ioctl command words the broadcasts hand on */
#define T32_CORE_CMD_16		0x1000016U
#define T32_CORE_CMD_17		0x1000017U
#define T32_CORE_CMD_1A		0x100001aU
#define T32_CORE_CMD_SUM	0x5000005U
#define T32_CORE_AE_CLOSE	0x1000015U
#define T32_CORE_AWB_OPEN	0x1000018U
#define T32_CORE_AWB_CLOSE	0x1000019U
#define T32_CORE_CMD_1B		0x100001bU
#define T32_AWB_ALGO_BUF	28U

/* sensor ioctl command words */
#define T32_SENS_ENUM_INPUT	0x2000002U
#define T32_SENS_GET_INPUT	0x2000003U
#define T32_SENS_SET_INPUT	0x2000004U
#define T32_SENS_S_REGISTER	0x2000011U
#define T32_SENS_G_REGISTER	0x2000012U
#define T32_NOTIFY_BYPASS	0x200001dU

#define T32_CORE_BYPASS		1148U	/* core device +1148 + 4 input: bypass flag (vendor 4 * (input + 286) + 4) */
#define T32_UIO_REG_BUF		64U
#define T32_UIO_ENUM_BUF	80U

#define T32_UIO_CFU_ERR		"[ %s:%d ] copy from user error\n"
#define T32_UIO_CTU_ERR		"[ %s:%d ] copy to user error\n"

/* core device + 104 * input: +376 sensor (+148 WDR mode), +384 / +388 width / height */
#define T32_CORE_IN		376U
#define T32_CORE_IN_BYTES	104U
#define T32_CORE_IN_W		384U
#define T32_CORE_IN_H		388U
#define T32_SENSOR_WDR_MODE	148U
#define T32_CORE_WDR_BUF	1180U	/* +1176 + 4 input: WDR buffer ok (vendor 4 * (input + 294) + 4) */

/* pad (find_subdev_link_pad): +6 type mask, +7 state, +8 link { self, peer pad, peer link, flags } */
#define T32_PAD_TYPE		6U
#define T32_PAD_STATE		7U
#define T32_PAD_PEER		12U
#define T32_PAD_PEER_LINK	16U
#define T32_PAD_FLAGS		20U
#define T32_PAD_LINKED		3U
#define T32_PAD_STREAMING	4U
#define T32_LINK_FLAGS		16U	/* link descriptor +16: type mask / flags */

/*
 * Frame drop: user block { u8 input, 3 x { en, u8 lsize, fmark } } (40
 * bytes); core device +1132 channel table (284 bytes per channel, channel
 * input * 3 + k): +256 once, +260 enable, +264 lsize, +268 fmark, +272 count.
 */
#define T32_FD_BUF		40U
#define T32_FD_CHN		3U
#define T32_CORE_FD_TAB		1132U
#define T32_CORE_FD_TISP	1196U
#define T32_FD_BYTES		284U
#define T32_FD_ONCE		256U
#define T32_FD_EN		260U
#define T32_FD_LSIZE		264U
#define T32_FD_FMARK		268U
#define T32_FD_COUNT		272U
#define T32_FD_LSIZE_MAX	32U
#define T32_FD_FPS_ERR		"FPS over 32 in the numerator or denominator!!! Setup failed!!!\n"
#define T32_FD_SET_MSG		"set frame drop chn:%d lsize:%d fmark:0x%x\n"
#define T32_FD_OFF_MSG		"set frame drop chn:%d disable\n"

/* provided by the driver glue (kernel) or by the oracle (host) */
int t32_tx_os_tisp_set_frame_drop(u32 in, u32 k, void *e);
int t32_tx_os_tisp_get_frame_drop(u32 in, u32 k, void *e);
void t32_tx_os_lib_tisp_set_isp_memory_space(u32 sel, u32 v);
u32 t32_tx_os_lib_tisp_get_isp_memory_space(u32 sel, void *opt);
u32 t32_tx_os_get_isp_memopt(void);
void t32_tx_os_tisp_s_wdr_en(u32 in, u32 en, u32 why);

typedef int (*t32_uio_core_fn)(u32 sd, u32 cmd, u32 arg);
typedef int (*t32_uio_notify_fn)(u32 sd, u32 ev, void *msg);

/* subdevice k's core ioctl (0 = none); the ops table itself is not checked (vendor) */
static inline u32 t32_uio_core_ioctl(u32 sd)
{
	u32 core = t32_tx_ld(t32_tx_ld(sd, T32_SD_OPS), T32_OPS_CORE);

	return core ? t32_tx_ld(core, T32_CORE_IOCTL) : 0;
}

/* broadcast: an error other than -ENOIOCTLCMD stops and is returned, else 0 */
static inline int t32_uio_core_each(u32 isp, u32 cmd, u32 arg)
{
	u32 k;

	for (k = 0; k < T32_DEV_SD_N; k++) {
		u32 sd = t32_tx_ld(isp, T32_DEV_SD + 4 * k), fn;
		int r;

		if (!sd || !(fn = t32_uio_core_ioctl(sd)))
			continue;
		r = ((t32_uio_core_fn)T32_TX_P(fn))(sd, cmd, arg);
		if (r && r != -(int)T32_ENOIOCTLCMD)
			return r;
	}
	return 0;
}

/* broadcast: like _each, but returns the last subdevice's result (-ENOIOCTLCMD for no ioctl) */
static inline int t32_uio_core_last(u32 isp, u32 cmd, u32 arg)
{
	int r = 0;
	u32 k;

	for (k = 0; k < T32_DEV_SD_N; k++) {
		u32 sd = t32_tx_ld(isp, T32_DEV_SD + 4 * k), fn;
		if (!sd)
			continue;
		fn = t32_uio_core_ioctl(sd);
		if (!fn) {
			r = -(int)T32_ENOIOCTLCMD;
			continue;
		}
		r = ((t32_uio_core_fn)T32_TX_P(fn))(sd, cmd, arg);
		if (r == -(int)T32_ENOIOCTLCMD)
			continue;
		if (r)
			return r;
	}
	return r;
}

/* broadcast: the sum of all results (-ENOIOCTLCMD for no ioctl) */
static inline int t32_uio_core_sum(u32 isp, u32 cmd, u32 arg)
{
	u32 k, sum = 0;

	for (k = 0; k < T32_DEV_SD_N; k++) {
		u32 sd = t32_tx_ld(isp, T32_DEV_SD + 4 * k), fn;
		if (!sd)
			continue;
		fn = t32_uio_core_ioctl(sd);
		sum += fn ? (u32)((t32_uio_core_fn)T32_TX_P(fn))(sd, cmd, arg) : (u32)-(int)T32_ENOIOCTLCMD;
	}
	return (int)sum;
}

/* tx_isp_bypass_all: { enable, input } into the core device, then the sensor notify of subdevice 0 */
static inline int t32_uio_bypass_all(u32 isp, u32 user)
{
	u32 sd = t32_tx_ld(isp, T32_DEV_SD), core = t32_tx_ld(sd, T32_SD_PRIV_CORE), b[2], fn;

	if (t32_tx_os_copy_from_user(b, user, sizeof(b))) {
		t32_tx_os_isp_printf(2, T32_UIO_CFU_ERR, "tx_isp_bypass_all", 1395);
		return -(int)T32_EFAULT;
	}
	t32_tx_st(core, T32_CORE_BYPASS + 4 * b[1], b[0]);
	if ((fn = t32_tx_ld(sd, T32_SD_NOTIFY)))	/* vendor's sd != 0 test: dead after the core load above */
		((t32_uio_notify_fn)T32_TX_P(fn))(sd, T32_NOTIFY_BYPASS, b);
	return 0;
}

/* tx_isp_sensor_{enum,get,set}_input: block in, sensor ioctl on every subdevice, block back */
static inline int t32_uio_sensor_input(u32 isp, u32 arg, u32 n, u32 cmd, const char *fn, u32 lin, u32 lout)
{
	u32 b[T32_UIO_ENUM_BUF / 4];
	int r;

	if (t32_tx_arg_in(isp, b, arg, n)) {
		t32_tx_os_isp_printf(2, T32_UIO_CFU_ERR, fn, lin);
		return -(int)T32_EFAULT;
	}
	r = t32_tx_sensor_cmd_all(isp, cmd, b, 1);
	if (r)
		return r;
	if (t32_tx_ld(isp, T32_ISP_KCALL)) {
		memcpy(T32_TX_P(arg), b, n);
		return 0;
	}
	if (t32_tx_os_copy_to_user(arg, b, n)) {
		t32_tx_os_isp_printf(2, T32_UIO_CTU_ERR, fn, lout);
		return -(int)T32_EFAULT;
	}
	return 0;
}

/* tx_isp_sensor_s_register: always a user block */
static inline int t32_uio_s_register(u32 isp, u32 user)
{
	u32 b[T32_UIO_REG_BUF / 4];

	if (t32_tx_os_copy_from_user(b, user, T32_UIO_REG_BUF)) {
		t32_tx_os_isp_printf(2, T32_UIO_CFU_ERR, "tx_isp_sensor_s_register", 926);
		return -(int)T32_EFAULT;
	}
	return t32_tx_sensor_cmd_all(isp, T32_SENS_S_REGISTER, b, 1);
}

/* tx_isp_sensor_g_register.  Vendor: the copy-back failure prints the copy-from-user message */
static inline int t32_uio_g_register(u32 isp, u32 user)
{
	u32 b[T32_UIO_REG_BUF / 4];
	int r;

	if (t32_tx_os_copy_from_user(b, user, T32_UIO_REG_BUF)) {
		t32_tx_os_isp_printf(2, T32_UIO_CFU_ERR, "tx_isp_sensor_g_register", 953);
		return -(int)T32_EFAULT;
	}
	r = t32_tx_sensor_cmd_all(isp, T32_SENS_G_REGISTER, b, 1);
	if (r)
		return r;
	if (t32_tx_os_copy_to_user(user, b, T32_UIO_REG_BUF)) {
		t32_tx_os_isp_printf(2, T32_UIO_CFU_ERR, "tx_isp_sensor_g_register", 967);
		return -(int)T32_EFAULT;
	}
	return 0;
}

/* user or kernel block out (+60) */
static inline u32 t32_uio_arg_out(u32 isp, u32 arg, const void *src, u32 n)
{
	if (t32_tx_ld(isp, T32_ISP_KCALL)) {
		memcpy(T32_TX_P(arg), src, n);
		return 0;
	}
	return t32_tx_os_copy_to_user(arg, src, n);
}

/* tx_isp_set_isp_mem: { u8 sel, u32 value, ... } */
static inline int t32_uio_set_isp_mem(u32 isp, u32 arg)
{
	u32 b[3];

	if (t32_tx_arg_in(isp, b, arg, sizeof(b))) {
		t32_tx_os_isp_printf(2, T32_UIO_CFU_ERR, "tx_isp_set_isp_mem", 986);
		return -(int)T32_EFAULT;
	}
	t32_tx_os_lib_tisp_set_isp_memory_space((u8)b[0], b[1]);
	return 0;
}

/*
 * tx_isp_get_isp_mem: { u8 sel, -, size out }.  The option block handed to
 * the lib holds the memory option nibble at +4; the vendor leaves its other
 * bytes as stale stack (zero here).  A failed copy back prints the
 * copy-from-user message (vendor).
 */
static inline int t32_uio_get_isp_mem(u32 isp, u32 arg)
{
	u32 m = t32_tx_os_get_isp_memopt(), b[3];
	u8 opt[8] = { 0 };

	if (t32_tx_arg_in(isp, b, arg, sizeof(b))) {
		t32_tx_os_isp_printf(2, T32_UIO_CFU_ERR, "tx_isp_get_isp_mem", 1032);
		return -(int)T32_EFAULT;
	}
	opt[4] = (m >> 4) & 15;
	b[2] = t32_tx_os_lib_tisp_get_isp_memory_space((u8)b[0], opt);
	if (t32_uio_arg_out(isp, arg, b, sizeof(b))) {
		t32_tx_os_isp_printf(2, T32_UIO_CFU_ERR, "tx_isp_get_isp_mem", 1044);
		return -(int)T32_EFAULT;
	}
	return 0;
}

/* the input's WDR mode is 1 or 2; *tsize = width * height * 4 */
static inline int t32_uio_wdr_size(u32 in, u32 *tsize)
{
	u32 mode = t32_tx_ld(t32_tx_ld(in, T32_CORE_IN), T32_SENSOR_WDR_MODE);

	if (mode != 1 && mode != 2)
		return 0;
	*tsize = t32_tx_ld(in, T32_CORE_IN_W) * t32_tx_ld(in, T32_CORE_IN_H) << 2;
	return 1;
}

/*
 * tx_isp_wdr_set_buf: { input, paddr, size }.  The vendor copies the block
 * in twice and keeps the input of the first copy; the input is not checked.
 */
static inline int t32_uio_wdr_set_buf(u32 isp, u32 arg)
{
	u32 core = t32_tx_ld(t32_tx_ld(isp, T32_DEV_SD), T32_SD_PRIV_CORE), b[3], ch, tsize;

	if (t32_tx_arg_in(isp, b, arg, sizeof(b))) {
		t32_tx_os_isp_printf(2, T32_UIO_CFU_ERR, "tx_isp_wdr_set_buf", 1121);
		return -(int)T32_EFAULT;
	}
	ch = b[0];
	if (t32_tx_arg_in(isp, b, arg, sizeof(b))) {
		t32_tx_os_isp_printf(2, T32_UIO_CFU_ERR, "tx_isp_wdr_set_buf", 1137);
		return -(int)T32_EFAULT;
	}
	if (!t32_uio_wdr_size(core + T32_CORE_IN_BYTES * ch, &tsize)) {
		t32_tx_os_isp_printf(2, "[ %s:%d ] Not the wdr mode, do not need to alloc buf!!!\n", "tx_isp_wdr_set_buf", 1147);
		return -1;
	}
	t32_tx_os_isp_printf(0, "[ %s:%d ] tsize is %d, buf info size is %d, paddr is 0x%x\n", "tx_isp_wdr_set_buf", 1151,
			     tsize, b[2], b[1]);
	if (b[2] < tsize) {
		t32_tx_os_isp_printf(0, "[ %s:%d ] buf size too small\n", "tx_isp_wdr_set_buf", 1154);
		t32_tx_st(core, T32_CORE_WDR_BUF + 4 * ch, 0);
		t32_tx_os_tisp_s_wdr_en(ch, 0, 2);
		return -(int)T32_EFAULT;
	}
	t32_tx_st(core, T32_CORE_WDR_BUF + 4 * ch, 1);
	return 0;
}

/* tx_isp_wdr_get_buf: { input, 0, tsize } back.  A failed copy back prints the copy-from-user message (vendor) */
static inline int t32_uio_wdr_get_buf(u32 isp, u32 arg)
{
	u32 b[3], in;

	if (t32_tx_arg_in(isp, b, arg, sizeof(b))) {
		t32_tx_os_isp_printf(2, T32_UIO_CFU_ERR, "tx_isp_wdr_get_buf", 1179);
		return -(int)T32_EFAULT;
	}
	in = t32_tx_ld(t32_tx_ld(isp, T32_DEV_SD), T32_SD_PRIV_CORE) + T32_CORE_IN_BYTES * b[0];
	b[1] = 0;
	b[2] = 0;
	if (!t32_uio_wdr_size(in, &b[2]))
		t32_tx_os_isp_printf(1, "[ %s:%d ] Not the wdr mode, do not need to alloc buf!!!\n", "tx_isp_wdr_get_buf", 1199);
	t32_tx_os_isp_printf(0, "[ %s:%d ] tsize is %d\n", "tx_isp_wdr_get_buf", 1202, b[2]);
	if (t32_uio_arg_out(isp, arg, b, sizeof(b))) {
		t32_tx_os_isp_printf(2, T32_UIO_CFU_ERR, "tx_isp_wdr_get_buf", 1207);
		return -(int)T32_EFAULT;
	}
	return 0;
}

/* link set of an input: +8 with a sensor, +4 in the multi-sensor modes (as tx_isp_video_link_destroy) */
static inline u32 t32_uio_link_set(u32 isp, u32 in)
{
	u32 m = t32_tx_link_mode;

	if ((m == 1 && in == 0) || (m == 2 && in < 2) || (m == 3 && in < 3) || (m == 4 && in < 4))
		return in + 4;
	return t32_tx_ld(isp, T32_LK_SENSOR(in)) ? in + 8 : in;
}

/* a pad already linked to another pad drops that link first; 1 = go on, 0 = skip this link, else error */
static inline int t32_uio_unlink_other(u32 pad, u32 peer, int *r)
{
	if (t32_tx_b(pad + T32_PAD_STATE) != T32_PAD_LINKED || t32_tx_ld(pad, T32_PAD_PEER) == peer)
		return 1;
	*r = t32_tx_os_subdev_video_destroy_link(pad + T32_PAD_LINK);
	return !*r;
}

static inline void t32_uio_link_pad(u32 pad, u32 peer, u32 flags)
{
	t32_tx_st(pad, T32_PAD_LINK, pad);
	t32_tx_st(pad, T32_PAD_PEER, peer);
	t32_tx_st(pad, T32_PAD_PEER_LINK, peer + T32_PAD_LINK);
	t32_tx_st(pad, T32_PAD_FLAGS, flags);
	t32_tx_wb(pad + T32_PAD_STATE, T32_PAD_LINKED);
}

/* subdev_video_setup_link (inlined): 0 = linked or skipped, else the error */
static inline int t32_uio_setup_link(u32 isp, u32 d)
{
	u32 src = t32_tx_os_find_subdev_link_pad(isp, d), snk = t32_tx_os_find_subdev_link_pad(isp, d + T32_LINK_SINK);
	u32 flags = t32_tx_ld(d, T32_LINK_FLAGS);
	int r = 0;

	if (!src || !snk)
		return 0;
	if (!(t32_tx_b(src + T32_PAD_TYPE) & t32_tx_b(snk + T32_PAD_TYPE) & flags)) {
		t32_tx_os_isp_printf(2, "[ %s:%d ] The link type is mismatch!\n", "subdev_video_setup_link", 513);
		return -1;
	}
	if (t32_tx_b(src + T32_PAD_STATE) == T32_PAD_STREAMING || t32_tx_b(snk + T32_PAD_STATE) == T32_PAD_STREAMING) {
		t32_tx_os_isp_printf(2, "[ %s:%d ] Please stop active links firstly!\n", "subdev_video_setup_link", 519);
		return -1;
	}
	if (!t32_uio_unlink_other(src, snk, &r) || !t32_uio_unlink_other(snk, src, &r))
		return r == -(int)T32_ENOIOCTLCMD ? 0 : r;
	t32_uio_link_pad(src, snk, flags | 1);
	t32_uio_link_pad(snk, src, flags | 1);
	return 0;
}

/* tx_isp_video_link_setup: { -, input } */
static inline int t32_uio_link_setup(u32 isp, u32 arg)
{
	u32 b[2], set, d, k;

	if (t32_tx_arg_in(isp, b, arg, sizeof(b))) {
		t32_tx_os_isp_printf(2, T32_UIO_CFU_ERR, "tx_isp_video_link_setup", 570);
		return -(int)T32_EFAULT;
	}
	set = t32_uio_link_set(isp, b[1]);
	if (set >= T32_LINK_SETS) {
		t32_tx_os_isp_printf(2, "[ %s:%d ] link(%d) is invalid!\n", "tx_isp_video_link_setup", 589, set);
		return -22;
	}
	if (t32_tx_ld(isp, T32_LK_COUNT(set)))
		return 0;
	d = t32_tx_link_sets[set].links;
	for (k = 0; (int)k < (int)t32_tx_link_sets[set].n; k++, d += T32_LINK_BYTES) {
		int r = t32_uio_setup_link(isp, d);

		if (r)
			return r;
	}
	t32_tx_st(isp, T32_LK_COUNT(set), 1);
	return 0;
}

/* link destroy: { -, input }, always a user block */
static inline int t32_uio_link_destroy(u32 isp, u32 user)
{
	u32 b[2];

	if (t32_tx_os_copy_from_user(b, user, sizeof(b))) {
		t32_tx_os_isp_printf(2, T32_UIO_CFU_ERR, "tx_isp_unlocked_ioctl", 2487);
		return -(int)T32_EFAULT;
	}
	return t32_tx_video_link_destroy(isp, b[1]);
}

/* tx_isp_set_{ae,awb}_algo_close: an 8-byte user block to the core ioctl of every subdevice */
static inline int t32_uio_algo_close(u32 isp, u32 user, u32 cmd, const char *fn, u32 line)
{
	u32 b[2];

	if (t32_tx_os_copy_from_user(b, user, sizeof(b))) {
		t32_tx_os_isp_printf(2, T32_UIO_CFU_ERR, fn, line);
		return -(int)T32_EFAULT;
	}
	return t32_uio_core_last(isp, cmd, T32_TX_A(b));
}

/*
 * tx_isp_set_awb_algo_open: { version, ... } in (8 bytes), the core ioctls
 * fill the 28-byte block, which goes back.  Vendor: bytes 8..27 are stale
 * stack unless a subdevice fills them (zero here).
 */
static inline int t32_uio_awb_algo_open(u32 isp, u32 user)
{
	u32 b[T32_AWB_ALGO_BUF / 4] = { 0 };
	int r;

	if (t32_tx_os_copy_from_user(b, user, 8)) {
		t32_tx_os_isp_printf(2, T32_UIO_CFU_ERR, "tx_isp_set_awb_algo_open", 1581);
		return -(int)T32_EFAULT;
	}
	if (b[0] != T32_AE_ALGO_VERSION) {
		t32_tx_os_isp_printf(2, "[ %s:%d ] Ae Algo Function register version error\n", "tx_isp_set_awb_algo_open", 1586);
		return -1;
	}
	r = t32_uio_core_last(isp, T32_CORE_AWB_OPEN, T32_TX_A(b));
	if (r && r != -(int)T32_ENOIOCTLCMD)
		return r;
	if (t32_tx_os_copy_to_user(user, b, T32_AWB_ALGO_BUF)) {
		t32_tx_os_isp_printf(2, T32_UIO_CTU_ERR, "tx_isp_set_awb_algo_open", 1600);
		return -(int)T32_EFAULT;
	}
	return r;
}

/* the vendor reloads the table pointer for every store */
static inline void t32_uio_fd_st(u32 core, u32 chn, u32 off, u32 v)
{
	t32_tx_st(t32_tx_ld(core, T32_CORE_FD_TAB) + T32_FD_BYTES * chn, off, v);
}

static inline void t32_uio_fd_off(u32 core, u32 chn, u32 en)
{
	t32_tx_os_isp_printf(0, T32_FD_OFF_MSG, chn);
	t32_uio_fd_st(core, chn, T32_FD_EN, en);
	t32_uio_fd_st(core, chn, T32_FD_LSIZE, 0);
	t32_uio_fd_st(core, chn, T32_FD_FMARK, (u32)-1);
	t32_uio_fd_st(core, chn, T32_FD_COUNT, 0);
}

/* tx_isp_set_frame_drop */
static inline int t32_uio_set_frame_drop(u32 isp, u32 arg)
{
	u32 core = t32_tx_ld(t32_tx_ld(isp, T32_DEV_SD), T32_SD_PRIV_CORE), b[T32_FD_BUF / 4], k;
	u8 *b8 = (u8 *)b;
	int r = 0;

	if (t32_tx_arg_in(isp, b, arg, T32_FD_BUF)) {
		t32_tx_os_isp_printf(2, T32_UIO_CFU_ERR, "tx_isp_set_frame_drop", 1726);
		return -(int)T32_EFAULT;
	}
	if (t32_tx_ld(core, T32_CORE_FD_TISP) == 1) {
		for (k = 0; k < T32_FD_CHN; k++)
			r = t32_tx_os_tisp_set_frame_drop(b8[0], k, &b[1 + 3 * k]);
		return r;
	}
	for (k = 0; k < T32_FD_CHN; k++) {
		u32 *e = &b[1 + 3 * k], chn = b8[0] * T32_FD_CHN + k, ls = (u8)e[1];

		if (e[0] != 1) {
			t32_uio_fd_off(core, chn, e[0]);
			continue;
		}
		if (ls >= T32_FD_LSIZE_MAX) {
			t32_tx_os_isp_printf(1, T32_FD_FPS_ERR);
			return -1;
		}
		t32_uio_fd_st(core, chn, T32_FD_EN, e[0]);
		t32_uio_fd_st(core, chn, T32_FD_LSIZE, ls);
		t32_uio_fd_st(core, chn, T32_FD_FMARK, e[2]);
		t32_uio_fd_st(core, chn, T32_FD_COUNT, 0);
		t32_uio_fd_st(core, chn, T32_FD_ONCE, 0);
		t32_tx_os_isp_printf(0, T32_FD_SET_MSG, chn, ls, e[2]);
	}
	return 0;
}

/* tx_isp_set_frame_drop_once: lsize 31 and the frames above lsize marked; then the tisp setter if core +1196 is 1 */
static inline int t32_uio_set_frame_drop_once(u32 isp, u32 arg)
{
	u32 core = t32_tx_ld(t32_tx_ld(isp, T32_DEV_SD), T32_SD_PRIV_CORE), b[T32_FD_BUF / 4], k;
	u8 *b8 = (u8 *)b;
	int r = 0;

	if (t32_tx_arg_in(isp, b, arg, T32_FD_BUF)) {
		t32_tx_os_isp_printf(2, T32_UIO_CFU_ERR, "tx_isp_set_frame_drop_once", 1815);
		return -(int)T32_EFAULT;
	}
	for (k = 0; k < T32_FD_CHN; k++) {
		u32 *e = &b[1 + 3 * k], chn = b8[0] * T32_FD_CHN + k, ls = (u8)e[1], mask = 0, i;

		if (e[0] != 1) {
			t32_uio_fd_off(core, chn, e[0]);
		} else {
			if (ls >= T32_FD_LSIZE_MAX) {
				t32_tx_os_isp_printf(1, T32_FD_FPS_ERR);
				return -1;
			}
			for (i = ls + 1; i < 32; i++)
				mask |= 1u << i;
			t32_uio_fd_st(core, chn, T32_FD_EN, 1);
			t32_uio_fd_st(core, chn, T32_FD_LSIZE, 31);
			t32_uio_fd_st(core, chn, T32_FD_FMARK, mask | e[2]);
			t32_uio_fd_st(core, chn, T32_FD_COUNT, 0);
			t32_uio_fd_st(core, chn, T32_FD_ONCE, 1);
			t32_tx_os_isp_printf(0, T32_FD_SET_MSG, chn, ls, e[2]);
		}
		if (t32_tx_ld(core, T32_CORE_FD_TISP) == 1)
			r = t32_tx_os_tisp_set_frame_drop(b8[0], k, e);
	}
	return r;
}

/* tx_isp_get_frame_drop{,_once}: lsize / fmark of the input's 3 channels back (the tisp getters for get, core +1196 1) */
static inline int t32_uio_get_frame_drop(u32 isp, u32 user, int once, const char *fn, u32 lin, u32 lout)
{
	u32 core = t32_tx_ld(t32_tx_ld(isp, T32_DEV_SD), T32_SD_PRIV_CORE), b[T32_FD_BUF / 4], k;
	u8 *b8 = (u8 *)b;
	int r = 0;

	if (t32_tx_os_copy_from_user(b, user, T32_FD_BUF)) {
		t32_tx_os_isp_printf(2, T32_UIO_CFU_ERR, fn, lin);
		return -(int)T32_EFAULT;
	}
	if (!once && t32_tx_ld(core, T32_CORE_FD_TISP) == 1) {
		for (k = 0; k < T32_FD_CHN; k++)
			r = t32_tx_os_tisp_get_frame_drop(b8[0], k, &b[1 + 3 * k]);
	} else {
		u32 p = t32_tx_ld(core, T32_CORE_FD_TAB) + T32_FD_BYTES * T32_FD_CHN * b8[0];

		for (k = 0; k < T32_FD_CHN; k++, p += T32_FD_BYTES) {
			b8[8 + 12 * k] = (u8)t32_tx_ld(p, T32_FD_LSIZE);
			b[3 + 3 * k] = t32_tx_ld(p, T32_FD_FMARK);
		}
	}
	if (t32_tx_os_copy_to_user(user, b, T32_FD_BUF)) {
		t32_tx_os_isp_printf(2, T32_UIO_CTU_ERR, fn, lout);
		return -(int)T32_EFAULT;
	}
	return r;
}

/* tx_isp_start_night_mode: { input } */
static inline int t32_uio_night_mode(u32 isp, u32 arg)
{
	u32 core = t32_tx_ld(t32_tx_ld(isp, T32_DEV_SD), T32_SD_PRIV_CORE), in = 0;

	if (t32_tx_arg_in(isp, &in, arg, 4)) {
		t32_tx_os_isp_printf(2, T32_UIO_CFU_ERR, "tx_isp_start_night_mode", 1892);
		return -(int)T32_EFAULT;
	}
	t32_tx_st(core, T32_CORE_NIGHT + 4 * in, T32_NIGHT_START);
	return 0;
}

/*
 * tx_isp_get_pm_control: { 1 if core +4020 is 1, 0 if it is 0 } to user.
 * Vendor: other values and the second word leave stale stack (zero here).
 */
static inline int t32_uio_get_pm_control(u32 isp, u32 user)
{
	u32 core = t32_tx_ld(t32_tx_ld(isp, T32_DEV_SD), T32_SD_PRIV_CORE), b[2] = { 0 };

	if (t32_tx_ld(core, T32_CORE_PM_CTRL) == 1)
		b[0] = 1;
	if (t32_tx_os_copy_to_user(user, b, sizeof(b))) {
		t32_tx_os_isp_printf(2, T32_UIO_CTU_ERR, "tx_isp_get_pm_control", 1918);
		return -(int)T32_EFAULT;
	}
	return 0;
}

/* a user block to subdevice 0's notify; nosd / nofn: results without a subdevice 0 / notify, res: pass its result */
static inline int t32_uio_notify_block(u32 isp, u32 user, u32 n, u32 ev, int nosd, int nofn, int res)
{
	u32 sd = t32_tx_ld(isp, T32_DEV_SD), b[6], fn;
	int r;

	if (t32_tx_os_copy_from_user(b, user, n)) {
		t32_tx_os_isp_printf(2, "Failed to copy from user\n");
		return -(int)T32_ENOMEM;
	}
	if (!sd)
		return nosd;
	fn = t32_tx_ld(sd, T32_SD_NOTIFY);
	if (!fn)
		return nofn;
	r = ((t32_uio_notify_fn)T32_TX_P(fn))(sd, ev, b);
	return res ? r : 0;
}

/* tx_isp_get_pm_all_done: an 8-byte user block to subdevice 0's notify, its result */
static inline int t32_uio_pm_all_done(u32 isp, u32 user)
{
	u32 sd = t32_tx_ld(isp, T32_DEV_SD), b[2], fn;

	if (t32_tx_os_copy_from_user(b, user, sizeof(b))) {
		t32_tx_os_isp_printf(2, T32_UIO_CFU_ERR, "tx_isp_get_pm_all_done", 1990);
		return -(int)T32_EFAULT;
	}
	if (!sd)
		return -(int)T32_ENOENT;
	fn = t32_tx_ld(sd, T32_SD_NOTIFY);
	if (!fn)
		return -(int)T32_ENOIOCTLCMD;
	return ((t32_uio_notify_fn)T32_TX_P(fn))(sd, T32_NOTIFY_PM_DONE, b);
}

/* tx_isp_set_tiziano_par: a 20-byte block (kernel or user) to subdevice 0's notify */
static inline int t32_uio_tiziano_par(u32 isp, u32 arg)
{
	u32 sd, b[5], fn;

	if (t32_tx_arg_in(isp, b, arg, sizeof(b))) {
		t32_tx_os_isp_printf(2, T32_UIO_CFU_ERR, "tx_isp_set_tiziano_par", 1229);
		return -(int)T32_EFAULT;
	}
	sd = t32_tx_ld(isp, T32_DEV_SD);
	if (sd && (fn = t32_tx_ld(sd, T32_SD_NOTIFY)))
		((t32_uio_notify_fn)T32_TX_P(fn))(sd, T32_NOTIFY_TIZIANO, b);
	return 0;
}

/* the running mode word (+64) to user; a failed copy returns the copy result */
static inline int t32_uio_run_mode(u32 isp, u32 user)
{
	u32 r = t32_tx_os_copy_to_user(user, T32_TX_P(isp + T32_ISP_RUN_MODE), 4);

	if (r)
		t32_tx_os_isp_printf(2, "Get isp driver running mode failed\n");
	return (int)r;
}

/* tx_isp_set_ae_start_ev: { u8 ch, ev[2] } into the AE scene attribute */
static inline int t32_uio_set_ae_start_ev(u32 user)
{
	u32 u[3], sc[T32_AE_SCENE_BYTES / 4];
	int r;

	if (t32_tx_os_copy_from_user(u, user, sizeof(u))) {
		t32_tx_os_isp_printf(2, T32_UIO_CFU_ERR, "tx_isp_set_ae_start_ev", 2080);
		return -(int)T32_EFAULT;
	}
	r = t32_tx_os_tisp_g_ae_scence_attr((u8)u[0], sc);
	if (r) {
		t32_tx_os_isp_printf(1, "[ %s:%d ] get ae scence failed!!!\n", "tx_isp_set_ae_start_ev", 2086);
		return r;
	}
	sc[T32_AE_SCENE_EV] = u[1];
	sc[T32_AE_SCENE_EV + 1] = u[2];
	r = t32_tx_os_tisp_s_ae_scence_attr((u8)u[0], sc);
	if (r) {
		t32_tx_os_isp_printf(1, "[ %s:%d ] set ae start ev failed!!!\n", "tx_isp_set_ae_start_ev", 2094);
		return r;
	}
	return 0;
}

/* tx_isp_get_ae_start_ev */
static inline int t32_uio_get_ae_start_ev(u32 user)
{
	u32 u[3], sc[T32_AE_SCENE_BYTES / 4];
	int r;

	if (t32_tx_os_copy_from_user(u, user, sizeof(u))) {
		t32_tx_os_isp_printf(2, T32_UIO_CFU_ERR, "tx_isp_get_ae_start_ev", 2110);
		return -(int)T32_EFAULT;
	}
	r = t32_tx_os_tisp_g_ae_scence_attr((u8)u[0], sc);
	if (r) {
		t32_tx_os_isp_printf(1, "[ %s:%d ] get ae scence failed!!!\n", "tx_isp_get_ae_start_ev", 2116);
		return r;
	}
	u[1] = sc[T32_AE_SCENE_EV];
	u[2] = sc[T32_AE_SCENE_EV + 1];
	if (t32_tx_os_copy_to_user(user, u, sizeof(u))) {
		t32_tx_os_isp_printf(2, T32_UIO_CTU_ERR, "tx_isp_get_ae_start_ev", 2123);
		return -(int)T32_EFAULT;
	}
	return 0;
}

/* tx_isp_gpio_state_switch{,_trige}: a user block to subdevice 0's core ioctl */
static inline int t32_uio_gpio(u32 isp, u32 user, u32 n, u32 cmd, const char *fn, u32 lin, const char *err, u32 lerr)
{
	u32 sd = t32_tx_ld(isp, T32_DEV_SD), b[4], core, f;
	int r;

	if (t32_tx_os_copy_from_user(b, user, n)) {
		t32_tx_os_isp_printf(2, T32_UIO_CFU_ERR, fn, lin);
		return -(int)T32_EFAULT;
	}
	if (!sd)
		r = -(int)T32_ENODEV;
	else if (!(core = t32_tx_ld(t32_tx_ld(sd, T32_SD_OPS), T32_OPS_CORE)) || !(f = t32_tx_ld(core, T32_CORE_IOCTL)))
		r = -(int)T32_ENOIOCTLCMD;
	else if (!(r = ((t32_uio_core_fn)T32_TX_P(f))(sd, cmd, T32_TX_A(b))))
		return 0;
	t32_tx_os_isp_printf(1, err, fn, lerr);
	return r;
}

/* tx_isp_set_wdr_enable: { mode, u8 sensor } - mode 2 on a single sensor sets core +1180 to 16 */
static inline int t32_uio_wdr_enable(u32 isp, u32 arg)
{
	u32 core = t32_tx_ld(t32_tx_ld(isp, T32_DEV_SD), T32_SD_PRIV_CORE), b[2];

	if (t32_tx_arg_in(isp, b, arg, sizeof(b))) {
		t32_tx_os_isp_printf(2, T32_UIO_CFU_ERR, "tx_isp_set_wdr_enable", 1313);
		return -(int)T32_EFAULT;
	}
	if (b[0] != 2)
		t32_tx_os_isp_printf(1, "[ISP] Mode(%d) don't support WDR mode !!!\n", b[0]);
	else if ((u8)b[1])
		t32_tx_os_isp_printf(1, "[ISP] sensor id = %d, Multi-sensors don't support WDR mode !!!\n", (u8)b[1]);
	else
		t32_tx_st(core, T32_CORE_WDR_MODE, T32_WDR_MODE_ON);
	return 0;
}

/* tx_isp_set_isp_ipc_mem: { u8 sel, value, - } */
static inline int t32_uio_set_ipc_mem(u32 isp, u32 arg)
{
	u32 b[3];

	if (t32_tx_arg_in(isp, b, arg, sizeof(b))) {
		t32_tx_os_isp_printf(2, T32_UIO_CFU_ERR, "tx_isp_set_isp_ipc_mem", 1004);
		return -(int)T32_EFAULT;
	}
	t32_tx_os_lib_tisp_set_isp_ipc_memory_space((u8)b[0], b[1]);
	return 0;
}

/*
 * tx_isp_get_isp_ipc_mem: { u8 sel, -, size out }.  Option block { u16
 * lines, u8 notify value, 0 }: lines = core +1204 if +1200 is 1, replaced
 * by cache line + (sensor +316 / 2) / width when WDR mode +1180 is 16
 * (which clears it), when that sum is not 0.  Vendor: a zero width is
 * divided by (the recovered C gives ~0); the input indexes unchecked; a
 * failed copy back prints the copy-from-user message.
 */
static inline int t32_uio_get_ipc_mem(u32 isp, u32 arg)
{
	u32 cl = t32_tx_os_get_isp_cache_line(), sd = t32_tx_ld(isp, T32_DEV_SD), core = t32_tx_ld(sd, T32_SD_PRIV_CORE);
	u32 b[3], w = 0, fn, lines = cl;
	u8 opt[4];

	if (t32_tx_arg_in(isp, b, arg, sizeof(b))) {
		t32_tx_os_isp_printf(2, T32_UIO_CFU_ERR, "tx_isp_get_isp_ipc_mem", 1071);
		return -(int)T32_EFAULT;
	}
	if ((fn = t32_tx_ld(sd, T32_SD_NOTIFY)))	/* vendor's sd != 0 test: dead after the core load */
		((t32_uio_notify_fn)T32_TX_P(fn))(sd, T32_NOTIFY_IPC, &w);
	opt[0] = opt[1] = opt[3] = 0;
	opt[2] = (u8)w;
	if (t32_tx_ld(core, T32_CORE_IPC_FIX) == 1)
		t32_tx_wh(T32_TX_A(opt), (u16)t32_tx_ld(core, T32_CORE_IPC_LINES));
	if (t32_tx_ld(core, T32_CORE_WDR_MODE) == T32_WDR_MODE_ON) {
		u32 in = core + T32_CORE_IN_BYTES * b[0], v, wd;

		v = t32_tx_ld(t32_tx_ld(in, T32_CORE_IN), T32_SENSOR_IPC_VAL) >> 1;
		wd = t32_tx_ld(in, T32_CORE_IN_W);
		t32_tx_st(core, T32_CORE_WDR_MODE, 0);
		lines += wd ? v / wd : ~0u;
	}
	if (lines)
		t32_tx_wh(T32_TX_A(opt), (u16)lines);
	b[2] = t32_tx_os_lib_tisp_get_isp_ipc_memory_space((u8)b[0], opt);
	if (t32_uio_arg_out(isp, arg, b, sizeof(b))) {
		t32_tx_os_isp_printf(2, T32_UIO_CFU_ERR, "tx_isp_get_isp_ipc_mem", 1097);
		return -(int)T32_EFAULT;
	}
	return 0;
}

/*
 * tx_isp_memory_malloc: { -, -, size } in, { ptr, phys, size } into a free
 * slot and back.  Vendor: a failed copy back frees the memory but leaves
 * the slot filled.
 */
static inline int t32_uio_memory_malloc(u32 isp, u32 user)
{
	u32 b[3], k, p;

	if (t32_tx_os_copy_from_user(b, user, sizeof(b))) {
		t32_tx_os_isp_printf(2, T32_UIO_CFU_ERR, "tx_isp_memory_malloc", 2219);
		return -(int)T32_EFAULT;
	}
	for (k = 0; k < T32_ISP_MEM_SLOTS; k++)
		if (!t32_tx_ld(isp, T32_ISP_MEM_SLOT + T32_MEM_SLOT_BYTES * k))
			break;
	if (k == T32_ISP_MEM_SLOTS) {
		t32_tx_os_isp_printf(1, "[ %s:%d ] Unable to apply for manageable memory\n", "tx_isp_memory_malloc", 2229);
		return -(int)T32_ENOMEM;
	}
	p = t32_tx_os_kmalloc(b[2], T32_GFP_MEM);
	b[0] = p;
	if (!p) {
		t32_tx_os_isp_printf(1, "[ %s:%d ] Failed to allocate memory\n", "tx_isp_memory_malloc", 2235);
		return -(int)T32_ENOMEM;
	}
	b[1] = p + T32_PHYS_OFF;
	memcpy(T32_TX_P(isp + T32_ISP_MEM_SLOT + T32_MEM_SLOT_BYTES * k), b, sizeof(b));
	if (t32_tx_os_copy_to_user(user, b, sizeof(b))) {
		t32_tx_os_kfree(b[0]);
		t32_tx_os_isp_printf(2, T32_UIO_CTU_ERR, "tx_isp_memory_malloc", 2243);
		return -(int)T32_EFAULT;
	}
	return 0;
}

/* tx_isp_memory_free: { ptr, -, - }: free a managed block and clear its slot */
static inline int t32_uio_memory_free(u32 isp, u32 user)
{
	u32 b[3], k;

	if (t32_tx_os_copy_from_user(b, user, sizeof(b))) {
		t32_tx_os_isp_printf(2, T32_UIO_CFU_ERR, "tx_isp_memory_free", 2258);
		return -(int)T32_EFAULT;
	}
	if (!b[0]) {
		t32_tx_os_isp_printf(2, "[ %s:%d ] The memory cannot be empty\n", "tx_isp_memory_free", 2263);
		return -(int)T32_ENOMEM;
	}
	for (k = 0; k < T32_ISP_MEM_SLOTS; k++) {
		u32 slot = isp + T32_ISP_MEM_SLOT + T32_MEM_SLOT_BYTES * k;

		if (t32_tx_ld(slot, 0) == b[0]) {
			t32_tx_os_kfree(b[0]);
			t32_tx_os_memset(slot, 0, T32_MEM_SLOT_BYTES);
			return 0;
		}
	}
	t32_tx_os_isp_printf(1, "[ %s:%d ] Unmanageable memory\n", "tx_isp_memory_free", 2273);
	return -(int)T32_ENOMEM;
}

/* tx_isp_unlocked_ioctl(file, cmd, arg) */
static inline int t32_tx_isp_unlocked_ioctl(u32 file, u32 cmd, u32 arg)
{
	u32 isp;

	if (file) {
		isp = t32_tx_ld(file, T32_FILE_PRIV) - T32_FILE_PRIV_ADJ;
		t32_tx_st(isp, T32_ISP_KCALL, 0);
	} else {
		isp = t32_tx_isp_dev;
		t32_tx_st(isp, T32_ISP_KCALL, 1);
	}
	switch (cmd) {
	case T32_UIO_VERSION:		return t32_tx_driver_version(arg);
	case T32_UIO_SENSOR_REG:	return t32_tx_sensor_register_sensor(isp, arg);
	case T32_UIO_SENSOR_REL:	return t32_tx_sensor_release_sensor(isp, arg);
	case T32_UIO_S_STREAM:
	case T32_UIO_S_STREAM2:		return t32_tx_video_s_stream(isp, arg);
	case T32_UIO_LINK_STREAM:
	case T32_UIO_LINK_STREAM2:	return t32_tx_video_link_stream(isp, arg);
	case T32_UIO_MULTISENSOR:	return t32_tx_multisensor_mode(isp, arg);
	case T32_UIO_AE_ALGO:		return t32_tx_set_ae_algo_open(isp, arg);
	case T32_UIO_SET_BINPATH:	return t32_tx_set_default_bin_path(isp + T32_DEV_SD, arg);
	case T32_UIO_GET_BINPATH:	return t32_tx_get_default_bin_path(isp + T32_DEV_SD, arg);
	case T32_UIO_CORE_16:		return t32_uio_core_each(isp, T32_CORE_CMD_16, arg);
	case T32_UIO_CORE_17:		return t32_uio_core_each(isp, T32_CORE_CMD_17, arg);
	case T32_UIO_CORE_1A:		return t32_uio_core_last(isp, T32_CORE_CMD_1A, arg);
	case T32_UIO_CORE_SUM:		return t32_uio_core_sum(isp, T32_CORE_CMD_SUM, arg);
	case T32_UIO_BYPASS:		return t32_uio_bypass_all(isp, arg);
	case T32_UIO_ENUM_INPUT:
		return t32_uio_sensor_input(isp, arg, T32_UIO_ENUM_BUF, T32_SENS_ENUM_INPUT, "tx_isp_sensor_enum_input", 671, 689);
	case T32_UIO_GET_INPUT:
		return t32_uio_sensor_input(isp, arg, 8, T32_SENS_GET_INPUT, "tx_isp_sensor_get_input", 709, 726);
	case T32_UIO_SET_INPUT:
		return t32_uio_sensor_input(isp, arg, 8, T32_SENS_SET_INPUT, "tx_isp_sensor_set_input", 746, 763);
	case T32_UIO_S_REGISTER:	return t32_uio_s_register(isp, arg);
	case T32_UIO_G_REGISTER:	return t32_uio_g_register(isp, arg);
	case T32_UIO_SET_ISP_MEM:	return t32_uio_set_isp_mem(isp, arg);
	case T32_UIO_GET_ISP_MEM:	return t32_uio_get_isp_mem(isp, arg);
	case T32_UIO_WDR_SET_BUF:	return t32_uio_wdr_set_buf(isp, arg);
	case T32_UIO_WDR_GET_BUF:	return t32_uio_wdr_get_buf(isp, arg);
	case T32_UIO_LINK_SETUP:	return t32_uio_link_setup(isp, arg);
	case T32_UIO_LINK_DESTROY:	return t32_uio_link_destroy(isp, arg);
	case T32_UIO_AE_CLOSE:
		return t32_uio_algo_close(isp, arg, T32_CORE_AE_CLOSE, "tx_isp_set_ae_algo_close", 1503);
	case T32_UIO_AWB_CLOSE:
		return t32_uio_algo_close(isp, arg, T32_CORE_AWB_CLOSE, "tx_isp_set_awb_algo_close", 1616);
	case T32_UIO_CORE_1B:		return t32_uio_core_last(isp, T32_CORE_CMD_1B, arg);
	case T32_UIO_AWB_OPEN:		return t32_uio_awb_algo_open(isp, arg);
	case T32_UIO_SET_FDROP:		return t32_uio_set_frame_drop(isp, arg);
	case T32_UIO_SET_FDROP1:	return t32_uio_set_frame_drop_once(isp, arg);
	case T32_UIO_GET_FDROP:		return t32_uio_get_frame_drop(isp, arg, 0, "tx_isp_get_frame_drop", 1775, 1790);
	case T32_UIO_GET_FDROP1:	return t32_uio_get_frame_drop(isp, arg, 1, "tx_isp_get_frame_drop_once", 1866, 1876);
	case T32_UIO_NIGHT_MODE:	return t32_uio_night_mode(isp, arg);
	case T32_UIO_GET_PM_CTRL:	return t32_uio_get_pm_control(isp, arg);
	case T32_UIO_PM_ALL_DONE:	return t32_uio_pm_all_done(isp, arg);
	case T32_UIO_SD_EV_0A:
		return t32_uio_notify_block(isp, arg, 24, T32_NOTIFY_0A, -(int)T32_ENOENT, -(int)T32_ENOIOCTLCMD, 1);
	case T32_UIO_SD_EV_0B:	return t32_uio_notify_block(isp, arg, 8, T32_NOTIFY_0B, 0, 0, 0);
	case T32_UIO_TIZIANO_PAR:	return t32_uio_tiziano_par(isp, arg);
	case T32_UIO_RUN_MODE:		return t32_uio_run_mode(isp, arg);
	case T32_UIO_SET_AE_EV:		return t32_uio_set_ae_start_ev(arg);
	case T32_UIO_GET_AE_EV:		return t32_uio_get_ae_start_ev(arg);
	case T32_UIO_GPIO_SWITCH:
		return t32_uio_gpio(isp, arg, 16, T32_CORE_GPIO, "tx_isp_gpio_state_switch", 2176,
				    "[ %s:%d ] set gpio state switch failed!!!\n", 2183);
	case T32_UIO_GPIO_TRIGGER:
		return t32_uio_gpio(isp, arg, 8, T32_CORE_GPIO_TRIG, "tx_isp_gpio_state_switch_trige", 2197,
				    "[ %s:%d ] set gpio state switch trigger failed!!!\n", 2204);
	case T32_UIO_DEFAULT_BIN:	return t32_tx_default_bin(isp + T32_DEV_SD, arg);
	case T32_UIO_WDR_ENABLE:	return t32_uio_wdr_enable(isp, arg);
	case T32_UIO_SET_IPC_MEM:	return t32_uio_set_ipc_mem(isp, arg);
	case T32_UIO_GET_IPC_MEM:	return t32_uio_get_ipc_mem(isp, arg);
	case T32_UIO_MEM_MALLOC:	return t32_uio_memory_malloc(isp, arg);
	case T32_UIO_MEM_FREE:		return t32_uio_memory_free(isp, arg);
	}
	return 0;
}

#endif
