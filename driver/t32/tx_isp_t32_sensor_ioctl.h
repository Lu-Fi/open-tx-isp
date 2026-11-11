/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 sensor subdev ioctl from the vendor module (Matt Davis' recovered C,
 * PR #15): subdev_sensor_ops_ioctl (0x66b8), commands 0x2000000..0x2000020
 * through a 33-entry jump table, everything else forwarded to the sensor.
 *
 * Subdev (VIN): sensor list head +288 / +292 (lock +312), active sensor
 * per index +296 + 4 * i, per-index lock +324 + 12 * i, per-index state
 * +372 + 4 * i, attribute block +392 (108 bytes, first word = GPIO mode),
 * +148 (copied to the sensor), notify +152.
 * Sensor subdev: ops +264 (core table +0: init +0, g_register +16,
 * s_register +28; sensor table +12: ioctl +8), I2C client +280, private
 * +284, +148, init argument +192.  Private: +300, list node +304 / +308,
 * info copy +312 (116 bytes; bus type at +344), +880 (word +16), info
 * pointer +884, client +280 (release).  Info: name +0, bus type +32 (1 =
 * I2C, 2 = SPI), +56, GPIO function +84, index u16 +92.
 *
 *  0     register sensor (I2C, one retry after a failed core init),
 *        GPIO function by mode, list add, frame channel by index;
 *  1     release sensor by name (list del, I2C client put / unregister);
 *  2 / 4 enum / set input;  3  get input (index of the active sensor);
 *  17/18 s_register / g_register through the core ops (arg index +32);
 *  19    sensor ioctl when the index state is 4;  26 / 27 sensor ioctl
 *        (-19 without sensor);  28  copy the attribute block;
 *  32    stream (two words, swapped);  12, others  sensor ioctl.
 * Returns -515 of the callees as 0.
 *
 * Vendor quirks kept: with the module flag d9008 set, the per-index
 * registration count d9450 counts up and the second registration of the
 * same name returns 0 at once; locks are released with the index read
 * again after the callee (arg / info may change it); the unknown-bus error of the retry path
 * returns with the per-index lock held; stream passes { arg[1], arg[0] }.
 * Vendor UB (crashes, not reachable from a registered subdev): bus type 2 (SPI)
 * reads the private pointer of a NULL sensor; the forwarded commands and
 * 28 read through a NULL subdev / arg; indexes are not range checked.
 *
 * Checked bit-exact against the vendor flow by tests/t32_oracle
 * (t32_ssio_oracle: commands 2..32 and others; t32_ssior_oracle: register,
 * release and all commands end to end).
 */
#ifndef TX_ISP_T32_SENSOR_IOCTL_H
#define TX_ISP_T32_SENSOR_IOCTL_H

#include "tx_isp_t32_glue.h"

int isp_printf(unsigned int level, const char *fmt, ...);

#define T32_SSIO_CMD_BASE	0x2000000U
#define T32_SSIO_EBUSY		(-515)
#define T32_SSIO_HEAD		288U
#define T32_SSIO_LIST_LOCK	312U
#define T32_SSIO_SLOT(i)	(296U + 4U * (i))
#define T32_SSIO_LOCK(i)	(324U + 12U * (i))
#define T32_SSIO_STATE(i)	(372U + 4U * (i))
#define T32_SSIO_ATTR		392U
#define T32_SSIO_ATTR_BYTES	108U
#define T32_SSIO_NOTIFY		152U
#define T32_SSIO_S_OPS		264U
#define T32_SSIO_S_CLIENT	280U
#define T32_SSIO_S_PRIV		284U
#define T32_SSIO_P_NODE		304U
#define T32_SSIO_P_INFO		312U
#define T32_SSIO_INFO_BYTES	116U
#define T32_SSIO_I_BUS		32U
#define T32_SSIO_I_GPIO		84U
#define T32_SSIO_I_INDEX	92U
#define T32_SSIO_EV_CHAN	0x1000002U

typedef int (*t32_ssio_core_fn)(u8 *s, u8 *arg);
typedef int (*t32_ssio_ioctl_fn)(u8 *s, u32 cmd, u8 *arg);
typedef int (*t32_ssio_notify_fn)(u8 *sd, u32 event, u32 *arg);

struct t32_ssio_os {
	int (*enum_input)(u8 *sd, u8 *arg);
	int (*set_input)(u8 *sd, u8 *arg);
	int (*s_stream)(u8 *sd, u32 *v);		/* vin_s_stream */
	u8 *(*register_i2c)(u8 *info);			/* subdev_sensor_register_i2c_device */
	void (*lock)(u8 *m);
	void (*unlock)(u8 *m);
	void (*put_adapter)(u32 adapter);
	void (*unregister_client)(u32 client);
	void (*subdev_deinit)(u8 *s);
	void (*gpio_set_func)(u32 port, u32 func, u32 pins);
	u32 *dup_check;					/* t32_data_d9008 */
	u32 *reg_count;					/* t32_data_d9450[index] */
	const char *names;				/* t32_data_f384c, 40 bytes per index */
};

static const char t32_ssio_no_sensor[] = "[ %s:%d ] Don't have active sensor!\n";
static const char t32_ssio_no_open[] = "[ %s:%d ] the devnode does't have been opened.\n";
static const char t32_ssio_no_bus[] = "[ %s:%d ] the type of sensor SBUS hasn't been defined.\n";

static inline int t32_ssio_ret(int ret)
{
	return ret == T32_SSIO_EBUSY ? 0 : ret;
}

static inline u8 *t32_ssio_slot(u8 *sd, u32 idx)
{
	return T32_PTR(t32_g_rd32(sd, T32_SSIO_SLOT(idx)));
}

static inline u32 t32_ssio_index(const u8 *info)
{
	return t32_g_rd16(info, T32_SSIO_I_INDEX);
}

/* sensor table +12, ioctl +8, under the per-index lock taken by the caller */
static inline int t32_ssio_locked_ioctl(const struct t32_ssio_os *os, u8 *sd, u8 *s, u32 cmd, u8 *arg)
{
	int ret = -19;
	u32 tbl, fn;

	if (s) {
		ret = T32_SSIO_EBUSY;
		tbl = t32_g_rd32(T32_PTR(t32_g_rd32(s, T32_SSIO_S_OPS)), 12);
		if (tbl) {
			fn = t32_g_rd32(T32_PTR(tbl), 8);
			if (fn)
				ret = ((t32_ssio_ioctl_fn)T32_PTR(fn))(s, cmd, arg);
		}
	}
	os->unlock(sd + T32_SSIO_LOCK(t32_g_rd32(arg, 0)));
	return t32_ssio_ret(ret);
}

static inline int t32_ssio_forward(const struct t32_ssio_os *os, u8 *sd, u32 cmd, u8 *arg, u32 line)
{
	u32 idx = t32_g_rd32(arg, 0);
	u8 *s = t32_ssio_slot(sd, idx);

	if (!s) {
		isp_printf(1, t32_ssio_no_sensor, "subdev_sensor_ops_ioctl", line);
		return -1;
	}
	os->lock(sd + T32_SSIO_LOCK(idx));
	return t32_ssio_locked_ioctl(os, sd, s, cmd, arg);
}

/* s_register (core +28) / g_register (core +16), sensor index at arg +32 */
static inline int t32_ssio_reg_op(const struct t32_ssio_os *os, u8 *sd, u8 *arg, u32 op,
				  const char *name, u32 line)
{
	u32 idx = t32_g_rd32(arg, 32), core, fn;
	u8 *s = t32_ssio_slot(sd, idx);
	int ret = T32_SSIO_EBUSY;

	if (!s) {
		isp_printf(1, t32_ssio_no_sensor, name, line);
		return -1;
	}
	os->lock(sd + T32_SSIO_LOCK(idx));
	core = t32_g_rd32(T32_PTR(t32_g_rd32(s, T32_SSIO_S_OPS)), 0);
	if (core) {
		fn = t32_g_rd32(T32_PTR(core), op);
		if (fn)
			ret = ((t32_ssio_core_fn)T32_PTR(fn))(s, arg);
	}
	os->unlock(sd + T32_SSIO_LOCK(t32_g_rd32(arg, 32)));
	return t32_ssio_ret(ret);
}

/* command 3: arg +4 = index in, sensor index (+292) or -1 out */
static inline int t32_ssio_get_input(u8 *sd, u8 *arg)
{
	u32 s, v = (u32)-1;

	if (!arg)
		return -22;
	s = t32_g_rd32(sd, T32_SSIO_SLOT(t32_g_rd32(arg, 4)));
	if (s && s < (u32)-4095)
		v = t32_g_rd32(T32_PTR(s), 292);
	t32_g_wr32(arg, 4, v);
	return 0;
}

static inline int t32_ssio_valid(const u8 *p)
{
	return p && T32_WORD(p) < (u32)-4095;
}

static inline void t32_ssio_release_client(const struct t32_ssio_os *os, u32 client)
{
	u32 adapter = t32_g_rd32(T32_PTR(client), 24);

	if (adapter)
		os->put_adapter(adapter);
	os->unregister_client(client);
}

/* core init (+0 of the core table) with the sensor's +192; 1 = no init */
static inline int t32_ssio_core_init(u8 *s)
{
	u32 core = t32_g_rd32(T32_PTR(t32_g_rd32(s, T32_SSIO_S_OPS)), 0), fn;

	if (!core)
		return 1;
	fn = t32_g_rd32(T32_PTR(core), 0);
	if (!fn)
		return 1;
	return ((t32_ssio_core_fn)T32_PTR(fn))(s, s + 192);
}

static inline u8 *t32_ssio_copy_info(u8 *s, u8 *info)
{
	u8 *priv = T32_PTR(t32_g_rd32(s, T32_SSIO_S_PRIV));

	memcpy(priv + T32_SSIO_P_INFO, info, T32_SSIO_INFO_BYTES);
	t32_g_wr32(priv, 884, T32_WORD(priv + T32_SSIO_P_INFO));
	return priv;
}

static inline void t32_ssio_gpio(const struct t32_ssio_os *os, u8 *sd, u32 func)
{
	if (t32_g_rd32(sd, T32_SSIO_ATTR) >= 3) {
		switch (func) {
		case 25: os->gpio_set_func(0, 17, 0x2000000); break;
		case 38: os->gpio_set_func(1, 17, 64); break;
		case 51: os->gpio_set_func(1, 17, 0x80000); break;
		case 60: os->gpio_set_func(1, 19, 0x10000000); break;
		case 66: os->gpio_set_func(2, 19, 4); break;
		}
	}
	if (t32_g_rd32(sd, T32_SSIO_ATTR) == 4) {
		switch (func) {
		case 26: os->gpio_set_func(0, 17, 0x4000000); break;
		case 39: os->gpio_set_func(1, 17, 128); break;
		case 62: os->gpio_set_func(1, 19, 0x20000000); break;
		case 68: os->gpio_set_func(2, 19, 16); break;
		}
	}
}

static inline int t32_ssio_register(const struct t32_ssio_os *os, u8 *sd, u8 *info)
{
	static const char fn[] = "subdev_sensor_ops_register_sensor";
	u32 idx, state, bus, prev, chan = 0, notify;
	u8 *s, *priv;

	if (!sd || !info)
		return -22;
	idx = t32_ssio_index(info);
	if (*os->dup_check && ++os->reg_count[idx] == 2 && !strcmp((const char *)info, os->names + 40 * idx))
		return 0;
	state = t32_g_rd32(sd, T32_SSIO_STATE(idx));
	if (state == 1) {
		isp_printf(2, "[ %s:%d ] sensor id is %d,state is %d, the devnode does't have been opened.\n",
			   fn, 148, idx, state);
		return -1;
	}
	bus = t32_g_rd32(info, T32_SSIO_I_BUS);
	if (bus == 1) {
		os->lock(sd + T32_SSIO_LOCK(idx));
		s = os->register_i2c(info);
		os->unlock(sd + T32_SSIO_LOCK(t32_ssio_index(info)));
		if (!t32_ssio_valid(s))
			return -22;
		os->lock(sd + T32_SSIO_LOCK(t32_ssio_index(info)));
		priv = t32_ssio_copy_info(s, info);
		t32_g_wr32(priv, 300, 0);
		if (!t32_ssio_core_init(s))
			goto ok;
	} else if (bus == 2) {
		os->lock(sd + T32_SSIO_LOCK(idx));
		s = NULL;			/* vendor: no SPI device, NULL private read (UB) */
		priv = t32_ssio_copy_info(s, info);
		t32_g_wr32(priv, 300, 0);
	} else {
		isp_printf(1, t32_ssio_no_bus, fn, 162);
		return -22;
	}
	/* retry once */
	t32_g_wr32(info, 56, t32_g_rd32(T32_PTR(t32_g_rd32(priv, 880)), 16));
	bus = t32_g_rd32(info, T32_SSIO_I_BUS);
	if (bus == 1) {
		t32_ssio_release_client(os, t32_g_rd32(s, T32_SSIO_S_CLIENT));
		s = os->register_i2c(info);
		if (!t32_ssio_valid(s))
			goto unlock;
		priv = t32_ssio_copy_info(s, info);
	} else if (bus == 2) {
		priv = t32_ssio_copy_info(s, info);
		if (!s)
			goto deinit;
	} else {
		isp_printf(1, t32_ssio_no_bus, fn, 184);
		return -22;		/* vendor: per-index lock stays held */
	}
	if (!t32_ssio_core_init(s))
		goto ok;
deinit:
	if (t32_g_rd32(info, T32_SSIO_I_BUS) == 1)
		t32_ssio_release_client(os, t32_g_rd32(s, T32_SSIO_S_CLIENT));
	os->subdev_deinit(s);
unlock:
	os->unlock(sd + T32_SSIO_LOCK(t32_ssio_index(info)));
	return -22;
ok:
	os->unlock(sd + T32_SSIO_LOCK(t32_ssio_index(info)));
	if (t32_g_rd32(info, T32_SSIO_I_GPIO))
		t32_ssio_gpio(os, sd, t32_g_rd32(info, T32_SSIO_I_GPIO));
	os->lock(sd + T32_SSIO_LIST_LOCK);
	prev = t32_g_rd32(sd, T32_SSIO_HEAD + 4);
	t32_g_wr32(sd, T32_SSIO_HEAD + 4, T32_WORD(priv + T32_SSIO_P_NODE));
	t32_g_wr32(priv, T32_SSIO_P_NODE, T32_WORD(sd + T32_SSIO_HEAD));
	t32_g_wr32(priv, T32_SSIO_P_NODE + 4, prev);
	t32_g_wr32(T32_PTR(prev), 0, T32_WORD(priv + T32_SSIO_P_NODE));
	os->unlock(sd + T32_SSIO_LIST_LOCK);
	t32_g_wr32(s, 148, t32_g_rd32(sd, 148));
	idx = t32_ssio_index(info);
	if (idx < 4)
		chan = idx + 1;
	notify = t32_g_rd32(sd, T32_SSIO_NOTIFY);
	if (notify && !((t32_ssio_notify_fn)T32_PTR(notify))(sd, T32_SSIO_EV_CHAN, &chan))
		return 0;
	isp_printf(2, "[ %s:%d ] Failed to create frame chan ,chan_flag = %d\n", fn, 271, chan);
	return -1;
}

static inline int t32_ssio_release(const struct t32_ssio_os *os, u8 *sd, u8 *info)
{
	static const char fn[] = "subdev_sensor_ops_release_sensor";
	u32 head = T32_WORD(sd + T32_SSIO_HEAD), node, next, prev, client;
	u8 *priv = NULL;

	if (!info)
		return -22;
	if (t32_g_rd32(sd, T32_SSIO_STATE(t32_ssio_index(info))) == 1) {
		isp_printf(2, t32_ssio_no_open, fn, 289);
		return -1;
	}
	os->lock(sd + T32_SSIO_LIST_LOCK);
	for (node = t32_g_rd32(sd, T32_SSIO_HEAD); node != head; node = t32_g_rd32(T32_PTR(node), 0)) {
		if (!strcmp((const char *)T32_PTR(node - T32_SSIO_P_NODE + T32_SSIO_P_INFO), (const char *)info)) {
			priv = T32_PTR(node - T32_SSIO_P_NODE);
			break;
		}
	}
	if (!priv) {
		os->unlock(sd + T32_SSIO_LIST_LOCK);
		return 0;
	}
	if (T32_WORD(priv) == t32_g_rd32(sd, T32_SSIO_SLOT(t32_ssio_index(info)))) {
		os->unlock(sd + T32_SSIO_LIST_LOCK);
		isp_printf(1, "[ %s:%d ] the sensor is active, please stop it firstly.\n", fn, 308);
		return -22;
	}
	next = t32_g_rd32(priv, T32_SSIO_P_NODE);
	prev = t32_g_rd32(priv, T32_SSIO_P_NODE + 4);
	t32_g_wr32(T32_PTR(next), 4, prev);
	t32_g_wr32(T32_PTR(prev), 0, next);
	t32_g_wr32(priv, T32_SSIO_P_NODE, 256);		/* LIST_POISON1 / 2 */
	t32_g_wr32(priv, T32_SSIO_P_NODE + 4, 512);
	os->unlock(sd + T32_SSIO_LIST_LOCK);
	switch (t32_g_rd32(priv, 344)) {
	case 1:
		client = t32_g_rd32(priv, T32_SSIO_S_CLIENT);
		os->lock(sd + T32_SSIO_LOCK(t32_ssio_index(info)));
		t32_ssio_release_client(os, client);
		os->unlock(sd + T32_SSIO_LOCK(t32_ssio_index(info)));
		return 0;
	case 2:
		return 0;
	}
	isp_printf(1, t32_ssio_no_bus, fn, 326);
	return -22;
}

static inline int t32_subdev_sensor_ops_ioctl(const struct t32_ssio_os *os, u8 *sd, u32 cmd, u8 *arg)
{
	u32 v[2], idx;
	u8 *s;

	switch (cmd - T32_SSIO_CMD_BASE) {
	case 0:
		return t32_ssio_register(os, sd, arg);
	case 1:
		return sd ? t32_ssio_release(os, sd, arg) : -22;
	case 2:
		return t32_ssio_ret(os->enum_input(sd, arg));
	case 3:
		return sd ? t32_ssio_get_input(sd, arg) : -22;
	case 4:
		return t32_ssio_ret(os->set_input(sd, arg));
	case 12:
		return t32_ssio_forward(os, sd, cmd, arg, 1063);
	case 17:
		return t32_ssio_reg_op(os, sd, arg, 28, "tx_isp_vin_s_register", 640);
	case 18:
		return t32_ssio_reg_op(os, sd, arg, 16, "tx_isp_vin_g_register", 622);
	case 19:
		idx = t32_g_rd32(arg, 0);
		s = t32_ssio_slot(sd, idx);
		if (!s || t32_g_rd32(sd, T32_SSIO_STATE(idx)) != 4)
			return 0;
		os->lock(sd + T32_SSIO_LOCK(idx));
		return t32_ssio_locked_ioctl(os, sd, s, cmd, arg);
	case 26:
	case 27:
		idx = t32_g_rd32(arg, 0);
		s = t32_ssio_slot(sd, idx);
		os->lock(sd + T32_SSIO_LOCK(idx));
		return t32_ssio_locked_ioctl(os, sd, s, cmd, arg);
	case 28:
		memcpy(sd + T32_SSIO_ATTR, arg, T32_SSIO_ATTR_BYTES);
		return 0;
	case 32:
		v[0] = t32_g_rd32(arg, 4);
		v[1] = t32_g_rd32(arg, 0);
		return t32_ssio_ret(os->s_stream(sd, v));
	}
	return t32_ssio_forward(os, sd, cmd, arg, 1081);
}

#endif /* TX_ISP_T32_SENSOR_IOCTL_H */
