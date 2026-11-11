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
 *            subdev_video_setup_link inlined) and destroy.
 */
#ifndef TX_ISP_T32_TXUIOCTL_H
#define TX_ISP_T32_TXUIOCTL_H

#include "tx_isp_t32_txos.h"
#include "tx_isp_t32_txcore.h"
#include "tx_isp_t32_txioctl.h"
#include "tx_isp_t32_txlink.h"

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

/* core ioctl command words the broadcasts hand on */
#define T32_CORE_CMD_16		0x1000016U
#define T32_CORE_CMD_17		0x1000017U
#define T32_CORE_CMD_1A		0x100001aU
#define T32_CORE_CMD_SUM	0x5000005U

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

/* provided by the driver glue (kernel) or by the oracle (host) */
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
	}
	return 0;
}

#endif
