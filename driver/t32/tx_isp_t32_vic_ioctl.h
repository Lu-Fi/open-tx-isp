/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 VIC core ioctl from the vendor module (Matt Davis' recovered C,
 * PR #15): vic_core_ops_ioctl (0x302c), commands 0x1000000..0x1000010.
 *
 * Subdev: VIC private +280, ops +264 (two op tables at +0 / +12, handler
 * +4 of each), notify +152.  VIC: VIC count +1096, per-VIC init blocks
 * from +288 (104 bytes: words +0 / +4 / +8, +52), streaming +820, state
 * lock +804, one-shot state +824 (48 bytes: +828 flag, +832 completion,
 * +844 counters, +848 / +856 masks), per-VIC state words +768, sensor
 * block +340 (+148 interface), raw snap +2388..+2408 and +2412..+2448
 * (completion +2448), GPIO slots per VIC index: used bytes +2460 / +2461,
 * freed bytes +2464 / +2465, slot records +2468 + 32 * index + 16 * slot
 * (16 bytes: the registration record, GPIO +4, active level +8).
 *
 *  0 / 1   op table 0 / 1 handler (-19 without subdev; -515 -> 0);
 *  3       PM stream on: stop / deinit / init per VIC, stream on;
 *  7       raw snap addresses for every VIC;
 *  8       reset (ISR state) and stream on (-515 -> 0);
 *  10      raw snap: parameters, wait up to 1000 ms for the frame;
 *  12      GPIO slot registration (two per VIC index);
 *  13      GPIO state switch through the RISC-V core, free both slots;
 *  14      one-shot leave: clear the state, notify 2 -> 1, reset, stream on;
 *  15      one-shot prepare: stop the RISC-V VIC, wait up to 200 ms, notify 2;
 *  16      one-shot start: VIC restart (interface 2: control 17424, else
 *          17472), arm the per-VIC masks, stop the RISC-V VIC.
 * Others: 0.
 *
 * Vendor quirks kept: the stop loop of command 3 stops once when a VIC is
 * present; command 7 writes the same address for every VIC; command 13
 * passes slot 0's GPIO / level as 0 when only slot 1 is used (the vendor
 * passes stale registers there, the recovered C 0).
 * Vendor UB (crashes, not reachable from a registered subdev): commands
 * 7 / 12..16 read through a NULL / error subdev or VIC pointer (the later
 * NULL checks of 14 / 15 are dead); command 7 writes past +2404 for more
 * than 4 VICs; command 13 / 12 index past the slot bytes for index > 1.
 *
 * Checked bit-exact against the vendor flow by tests/t32_oracle
 * (t32_vicio_oracle).
 */
#ifndef TX_ISP_T32_VIC_IOCTL_H
#define TX_ISP_T32_VIC_IOCTL_H

#include "tx_isp_t32_glue.h"

int isp_printf(unsigned int level, const char *fmt, ...);

#define T32_VIO_CMD_BASE	0x1000000U
#define T32_VIO_SD_OPS		264U
#define T32_VIO_SD_NOTIFY	152U
#define T32_VIO_SD_PRIV		280U
#define T32_VIO_COUNT		1096U
#define T32_VIO_INIT		288U
#define T32_VIO_INIT_BYTES	104U
#define T32_VIO_STREAM		820U
#define T32_VIO_LOCK		804U
#define T32_VIO_OS		824U	/* one-shot state, 48 bytes */
#define T32_VIO_OS_BYTES	48U
#define T32_VIO_OS_DONE		832U
#define T32_VIO_VSTATE		768U
#define T32_VIO_SNAP_DONE	2448U
#define T32_VIO_GPIO_USED	2460U
#define T32_VIO_GPIO_SLOT	2468U
#define T32_VIO_EV_ONESHOT	0x1000006U
#define T32_VIO_CTRL_MIPI	17424U
#define T32_VIO_CTRL_OTHER	17472U
#define T32_VIO_EBUSY_REMOTE	(-515)

typedef int (*t32_vio_handler)(u8 *sd, u8 *arg);
typedef int (*t32_vio_notify)(u8 *sd, u32 event, u32 value);

struct t32_vicio_os {
	void (*vic_stop)(void);				/* lib_tisp_vic_stop */
	void (*vic_deinit)(void);
	u32 (*index_cal)(u8 *vic, u32 n);		/* tx_isp_vic_index_cal */
	int (*vic_init)(u32 index, u32 mode, const u8 *p);
	void (*vic_stream_on)(void);
	int (*reset_isr)(u8 *sd);			/* tx_isp_vic_reset_isra_5 */
	void (*init_completion)(u8 *c);
	int (*wait_timeout)(u8 *c, u32 ms);		/* private_wait_for_completion_timeout */
	void (*gpio_request)(u32 gpio, const char *name);
	u32 (*gpio_to_desc)(u32 gpio);
	u32 (*gpiod_get_raw_value)(u32 desc);
	void (*gpio_direction_output)(u32 gpio, u32 value);
	void (*riscv_switch_gpio)(u32 index, u32 gpio0, u32 active0, u32 gpio1, u32 active1);
	void (*gpio_free)(u32 gpio);
	void (*lock)(u8 *l, u32 *flags);
	void (*unlock)(u8 *l, u32 flags);
	void (*riscv_stop_vic)(u32 n);
	void (*vic_reset)(void);
	void (*enable_irq)(u32 on);			/* tx_vic_enable_irq */
	void (*vic_start)(u8 *vic, u32 on);		/* tx_isp_vic_start */
	void (*vic_ctrl)(u32 on, u32 value);
};

static inline int t32_vio_valid(uintptr_t p)
{
	return p && p < (uintptr_t)(u32)-4095;
}

static inline u8 *t32_vio_vic(u8 *sd)
{
	u8 *vic;

	if (!t32_vio_valid((uintptr_t)sd))
		return NULL;
	vic = T32_PTR(t32_g_rd32(sd, T32_VIO_SD_PRIV));
	return t32_vio_valid((uintptr_t)vic) ? vic : NULL;
}

static inline int t32_vio_handler_call(u8 *sd, u32 table, u8 *arg)
{
	u32 ops, fn;
	int ret;

	if (!sd)
		return -19;
	ops = t32_g_rd32(T32_PTR(t32_g_rd32(sd, T32_VIO_SD_OPS)), table);
	if (!ops)
		return 0;
	fn = t32_g_rd32(T32_PTR(ops), 4);
	if (!fn)
		return 0;
	ret = ((t32_vio_handler)T32_PTR(fn))(sd, arg);
	return ret == T32_VIO_EBUSY_REMOTE ? 0 : ret;
}

static inline int t32_vio_pm_streamon(const struct t32_vicio_os *os, u8 *sd)
{
	static const char fn[] = "tx_isp_vic_pm_streamon";
	u8 *vic = t32_vio_vic(sd), *blk, p[16];
	u32 n, i, idx;
	int ret = 0;

	if (!vic) {
		isp_printf(2, "\n [%s %d] VIC is null \n", fn, 829);
		return -22;
	}
	n = t32_g_rd32(vic, T32_VIO_COUNT) < 2 ? 1 : 2;
	for (i = 0; i < n; i++)
		if (t32_g_rd32(vic, T32_VIO_COUNT) == 1 || i == 1) {
			os->vic_stop();
			t32_g_wr32(vic, T32_VIO_STREAM, 0);
		}
	os->vic_deinit();
	blk = vic + T32_VIO_INIT;
	for (i = 0; i < n; i++, blk += T32_VIO_INIT_BYTES) {
		idx = os->index_cal(vic, i);
		memcpy(p, blk, 12);
		memcpy(p + 12, vic + T32_VIO_COUNT, 4);
		ret = os->vic_init(idx & 0xff, t32_g_rd32(blk, 52), p);
	}
	os->vic_stream_on();
	if (ret) {
		isp_printf(2, "\n [%s %d] Fail to vic streamon\n", fn, 857);
		return -22;
	}
	isp_printf(1, "[%s %d] vic is streamon !!!\n", fn, 861);
	return 0;
}

static inline int t32_vio_snap_raw(const struct t32_vicio_os *os, u8 *sd, const u8 *arg)
{
	static const char fn[] = "tx_isp_vic_snap_raw";
	u8 *vic = t32_vio_vic(sd);
	u32 p[6];
	int r, ret;

	if (!vic) {
		isp_printf(2, "\n [%s %d] VIC is null \n", fn, 920);
		return -22;
	}
	memcpy(p, arg, sizeof(p));
	isp_printf(1, "Driver vinum = %d, raw_type = %d, wdr_rawmode = %d, paddr = 0x%08x, snap_cnt = %d, frame_size = %d\n",
		   p[0], p[2], p[3], p[5], p[1], p[4]);
	t32_g_wr32(vic, 2412, 0);
	t32_g_wr32(vic, 2420, 0);
	t32_g_wr32(vic, 2424, 0);
	t32_g_wr32(vic, 2416, p[0]);
	t32_g_wr32(vic, 2432, p[2]);
	t32_g_wr32(vic, 2428, p[1]);
	t32_g_wr32(vic, 2436, p[3]);
	t32_g_wr32(vic, 2440, p[4]);
	t32_g_wr32(vic, 2444, p[5]);
	os->init_completion(vic + T32_VIO_SNAP_DONE);
	t32_g_wr32(vic, 2412, 1);
	r = os->wait_timeout(vic + T32_VIO_SNAP_DONE, 1000);
	if (r == 0) {
		isp_printf(1, "Wait for completion timeout after %d ms\n", 10000);
		ret = -1;
	} else if (r < 0) {
		isp_printf(1, "Wait interrupted by signal\n");
		ret = -1;
	} else {
		isp_printf(1, "snap %d raw successed! \n", p[1]);
		ret = 0;
	}
	t32_g_wr32(vic, 2412, 0);
	return ret;
}

static inline int t32_vio_gpio_register(const struct t32_vicio_os *os, u8 *sd, const u8 *arg)
{
	static const char fn[] = "tx_isp_vic_gpio_state_switch";
	u8 *vic = T32_PTR(t32_g_rd32(sd, T32_VIO_SD_PRIV));
	u32 idx = os->index_cal(vic, t32_g_rd32(arg, 0)), slot = 0;

	if (vic[T32_VIO_GPIO_USED + 2 * idx]) {
		if (vic[T32_VIO_GPIO_USED + 2 * idx + 1]) {
			isp_printf(1, "[%s %d] Sensor%d belongs to VIC%d. VIC can operate a maximum of 2 GPIOs..",
				   fn, 976, t32_g_rd32(arg, 0), idx);
			return 0;
		}
		slot = 1;
	}
	memcpy(vic + T32_VIO_GPIO_SLOT + 32 * idx + 16 * slot, arg, 16);
	vic[T32_VIO_GPIO_USED + 2 * idx + slot] = 1;
	return 0;
}

/* one GPIO slot: claim it as output at its current level; returns its "active level is 5" flag */
static inline u32 t32_vio_gpio_claim(const struct t32_vicio_os *os, const u8 *rec, const char *name)
{
	u32 gpio = t32_g_rd32(rec, 4);

	os->gpio_request(gpio, name);
	os->gpio_direction_output(gpio, os->gpiod_get_raw_value(os->gpio_to_desc(gpio)));
	return t32_g_rd32(rec, 8) == 5;
}

static inline int t32_vio_gpio_switch(const struct t32_vicio_os *os, u8 *sd, const u8 *arg)
{
	u8 *vic = T32_PTR(t32_g_rd32(sd, T32_VIO_SD_PRIV)), *used, *rec;
	u32 idx = os->index_cal(vic, t32_g_rd32(arg, 4)), g0 = 0, a0 = 0, g1 = 0, a1 = 0;

	used = vic + T32_VIO_GPIO_USED + 2 * idx;
	rec = vic + T32_VIO_GPIO_SLOT + 32 * idx;
	if (used[0]) {
		a0 = t32_vio_gpio_claim(os, rec, "switch0");
		g0 = t32_g_rd32(rec, 4);
	}
	if (used[1]) {
		a1 = t32_vio_gpio_claim(os, rec + 16, "switch1");
		g1 = t32_g_rd32(rec + 16, 4);
	} else {
		a1 = a0;
		g1 = g0;
	}
	if (used[0] || used[1])
		os->riscv_switch_gpio(idx, g0, a0, g1, a1);
	if (used[0]) {
		os->gpio_free(t32_g_rd32(rec, 4));
		used[0] = 0;
		used[4] = 1;
	}
	if (used[1]) {
		os->gpio_free(t32_g_rd32(rec + 16, 4));
		used[1] = 0;
		used[5] = 1;
	}
	return 0;
}

static inline void t32_vio_notify_call(u8 *sd, u32 value)
{
	u32 fn = t32_g_rd32(sd, T32_VIO_SD_NOTIFY);

	if (fn)
		((t32_vio_notify)T32_PTR(fn))(sd, T32_VIO_EV_ONESHOT, value);
}

static inline int t32_vio_oneshot_leave(const struct t32_vicio_os *os, u8 *sd)
{
	u8 *vic = T32_PTR(t32_g_rd32(sd, T32_VIO_SD_PRIV));
	u32 flags = 0, i, n;

	os->lock(vic + T32_VIO_LOCK, &flags);
	if (t32_g_rd32(vic, T32_VIO_OS) == 1) {
		memset(vic + T32_VIO_OS, 0, T32_VIO_OS_BYTES);
		t32_g_wr32(vic, T32_VIO_OS, 0);
	}
	n = t32_g_rd32(vic, T32_VIO_COUNT);
	for (i = 0; i < n; i++)
		t32_g_wr32(vic, T32_VIO_VSTATE + 4 * i, 4);
	os->unlock(vic + T32_VIO_LOCK, flags);
	t32_vio_notify_call(sd, 1);
	os->reset_isr(sd);
	os->vic_stream_on();
	return 0;
}

static inline int t32_vio_oneshot_prepare(const struct t32_vicio_os *os, u8 *sd)
{
	u8 *vic = T32_PTR(t32_g_rd32(sd, T32_VIO_SD_PRIV));
	u32 flags = 0;

	os->lock(vic + T32_VIO_LOCK, &flags);
	if (!t32_g_rd32(vic, T32_VIO_OS)) {
		memset(vic + T32_VIO_OS, 0, T32_VIO_OS_BYTES);
		t32_g_wr32(vic, T32_VIO_OS, 1);
		if (t32_g_rd32(vic, T32_VIO_STREAM)) {
			os->riscv_stop_vic(t32_g_rd32(vic, T32_VIO_COUNT));
			t32_g_wr32(vic, 828, 1);
			os->init_completion(vic + T32_VIO_OS_DONE);
		}
	}
	os->unlock(vic + T32_VIO_LOCK, flags);
	if (t32_g_rd32(vic, T32_VIO_STREAM)) {
		if (!os->wait_timeout(vic + T32_VIO_OS_DONE, 200))
			isp_printf(1, "oneshot wait for prepare timeout\n");
		t32_g_wr32(vic, T32_VIO_STREAM, 0);
	}
	t32_vio_notify_call(sd, 2);
	return 0;
}

static inline u32 t32_vio_ctrl_value(const u8 *vic)
{
	return t32_g_rd32(T32_PTR(t32_g_rd32(vic, 340)), 148) == 2 ? T32_VIO_CTRL_MIPI : T32_VIO_CTRL_OTHER;
}

static inline int t32_vio_oneshot_start(const struct t32_vicio_os *os, u8 *sd)
{
	u8 *vic = T32_PTR(t32_g_rd32(sd, T32_VIO_SD_PRIV));
	u32 flags = 0, i, n, bit, k;

	os->lock(vic + T32_VIO_LOCK, &flags);
	if (t32_g_rd32(vic, T32_VIO_OS) == 1) {
		if (!t32_g_rd32(vic, T32_VIO_STREAM)) {
			os->vic_reset();
			os->enable_irq(0);
			os->vic_start(vic, 0);
			os->vic_ctrl(0, t32_vio_ctrl_value(vic));
			os->enable_irq(1);
			os->vic_start(vic, 1);
			os->vic_ctrl(1, t32_vio_ctrl_value(vic));
		}
		n = t32_g_rd32(vic, T32_VIO_COUNT);
		for (i = 0; i < n; i++) {
			bit = 1U << (i & 31);
			k = (i & 1) * 4;
			vic[844 + i]++;
			t32_g_wr32(vic, 848 + k, t32_g_rd32(vic, 848 + k) | bit);
			t32_g_wr32(vic, 856 + k, t32_g_rd32(vic, 856 + k) | bit);
			t32_g_wr32(vic, T32_VIO_VSTATE + 4 * i, 4);
		}
		os->riscv_stop_vic(n);
	}
	os->unlock(vic + T32_VIO_LOCK, flags);
	return 0;
}

static inline int t32_vic_core_ops_ioctl(const struct t32_vicio_os *os, u8 *sd, u32 cmd, u8 *arg)
{
	u8 *vic;
	u32 i, n;
	int ret;

	switch (cmd - T32_VIO_CMD_BASE) {
	case 0:
		return t32_vio_handler_call(sd, 0, arg);
	case 1:
		return t32_vio_handler_call(sd, 12, arg);
	case 3:
		return t32_vio_pm_streamon(os, sd);
	case 7:
		vic = T32_PTR(t32_g_rd32(sd, T32_VIO_SD_PRIV));
		n = t32_g_rd32(vic, T32_VIO_COUNT);
		for (i = 0; i < n; i++)
			t32_g_wr32(vic, 2388 + 4 * i, t32_g_rd32(arg, 0));
		t32_g_wr32(vic, 2404, 1);
		t32_g_wr32(vic, 2408, t32_g_rd32(arg, 4));
		return 0;
	case 8:
		ret = os->reset_isr(sd);
		os->vic_stream_on();
		return ret == T32_VIO_EBUSY_REMOTE ? 0 : ret;
	case 10:
		return t32_vio_snap_raw(os, sd, arg);
	case 12:
		return t32_vio_gpio_register(os, sd, arg);
	case 13:
		return t32_vio_gpio_switch(os, sd, arg);
	case 14:
		return t32_vio_oneshot_leave(os, sd);
	case 15:
		return t32_vio_oneshot_prepare(os, sd);
	case 16:
		return t32_vio_oneshot_start(os, sd);
	}
	return 0;
}

#endif
