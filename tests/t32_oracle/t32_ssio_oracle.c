// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our sensor subdev ioctl (driver/t32/tx_isp_t32_sensor_ioctl.h)
 * against the unmodified vendor subdev_sensor_ops_ioctl (Matt Davis'
 * recovered C, PR #15 @ 2e2e06bc).  Phase 1 (t32_ssio_oracle): commands
 * 0x2000002..0x2000020, out-of-table and random commands (input, register,
 * stream, attribute and forwarded sensor ioctls).  Phase 2 (t32_ssior_oracle,
 * same file with SSIO_PHASE 2): register / release (I2C registration with
 * retry, core init results, GPIO functions by mode, sensor list with name
 * matches, frame channel notify, module duplicate check) and all commands
 * end to end.  Random images with sensors / op tables / handlers present or
 * not, scripted hook answers (handlers may rewrite the index words) run
 * through ours, are restored and run through the vendor; compared: every
 * byte of the image, the module tables, the return value and the ordered
 * hook log (every hook with its arguments, isp_printf with all arguments).
 * Exit 0 = all equal.
 */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_sensor_ioctl.h"

#ifndef SSIO_PHASE
#define SSIO_PHASE 1
#define SSIO_NAME "t32_ssio_oracle"
#endif

#define R(f)	((t32o_fn)(void (*)(void))(f))
u32 t32_data_bd870[33] = { 0x673c, 0x6ccc, 0x670c, 0x671c, 0x672c, 0x6fc8, 0x6fc8, 0x6fc8, 0x6fc8, 0x6fc8, 0x6fc8,
			   0x6fc8, 0x6f34, 0x6fc8, 0x6fc8, 0x6fc8, 0x6fc8, 0x6cdc, 0x6d3c, 0x6ec8, 0x6fc8, 0x6fc8,
			   0x6fc8, 0x6fc8, 0x6fc8, 0x6fc8, 0x6e00, 0x6e64, 0x6de0, 0x6fc8, 0x6fc8, 0x6fc8, 0x6f9c };
char t32_data_bd90c[] = "tx_isp_vin_g_register";
char t32_data_bd924[] = "tx_isp_vin_s_register";
char t32_data_bd93c[] = "subdev_sensor_ops_release_sensor";
char t32_data_bd984[] = "subdev_sensor_ops_register_sensor";
char t32_data_bd9a8[] = "subdev_sensor_ops_ioctl";
char t32_data_c40f4[] = "[ %s:%d ] Don't have active sensor!\n";
char t32_data_c4478[] = "[ %s:%d ] the devnode does't have been opened.\n";
char t32_data_c44a8[] = "[ %s:%d ] the type of sensor SBUS hasn't been defined.\n";
char t32_data_c45c0[] = "[ %s:%d ] sensor id is %d,state is %d, the devnode does't have been opened.\n";
char t32_data_c4610[] = "[ %s:%d ] Failed to create frame chan ,chan_flag = %d\n";
char t32_data_c4648[] = "[ %s:%d ] the sensor is active, please stop it firstly.\n";
#define NIDX 8
u32 t32_data_d9008, t32_data_d9450[NIDX];
char t32_data_f384c[40 * NIDX];

u64 subdev_sensor_ops_ioctl(u32, u32, u32, u32);

static u32 rng = 0x2545f491u * SSIO_PHASE;
static u32 rnd(void)
{
	rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
	return rng;
}

#define MAXE 40
static struct { unsigned int n; u32 e[MAXE][10]; } EV[2];
static int side;
static u32 *ev(u32 a, u32 b, u32 c, u32 d)
{
	static u32 dummy[10];
	unsigned int n = EV[side].n++;

	if (n >= MAXE)
		return dummy;
	EV[side].e[n][0] = a; EV[side].e[n][1] = b; EV[side].e[n][2] = c; EV[side].e[n][3] = d;
	return EV[side].e[n];
}
static u32 hash(const u8 *p, u32 n)
{
	u32 h = 2166136261u, i;

	for (i = 0; i < n; i++)
		h = (h ^ p[i]) * 16777619u;
	return h;
}
static u32 hstr(const char *s) { return hash((const u8 *)s, (u32)strlen(s)); }

/* scripted answers */
static u32 r_bus, r_bidx, r_in, r_hnd, r_mod, r_mod_on, r_ntf, r_nval, r_init[2], r_reg[2], ninit, nreg;

/* both sides: image pointers are below 4 GiB, host stack buffers of ours are not */
static u64 c_hnd(u32 s, u32 cmd, u32 arg, u32 a3)
{
	(void)a3;
	ev(20, s, cmd, arg);
	if (r_mod_on)
		t32_g_wr32(T32O_HOST(arg), r_mod_on == 1 ? 0 : 32, r_mod);
	return (u64)r_hnd;
}
static u64 c_core(u32 s, u32 arg, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	ev(21, s, arg, 0);
	if (r_mod_on)
		t32_g_wr32(T32O_HOST(arg), 32, r_mod);
	return (u64)r_hnd;
}
static u64 c_init(u32 s, u32 arg, u32 a2, u32 a3) { (void)a2; (void)a3; ev(22, s, arg, 0); return r_init[ninit++ % 2]; }
static u64 c_ntf(unsigned long sd, unsigned long e, unsigned long p, unsigned long a3)
{
	u32 *v = (u32 *)(uintptr_t)(side ? (u32)p : p);

	(void)a3;
	ev(23, (u32)sd, (u32)e, *v);
	if (r_ntf)
		*v = r_nval;
	return r_ntf;
}
/* the I2C registration may rewrite the bus type / index of the info (re-read by the vendor) */
static u32 c_reg(u8 *info)
{
	u32 r;

	ev(4, T32O_PTR(info), hash(info, 116), 0);
	r = r_reg[nreg++ % 2];
	if (r_bus && nreg == 1)
		t32_g_wr32(info, T32_SSIO_I_BUS, r_bus - 1);
	if (r_bidx && nreg == 1)
		t32_g_wr16(info, T32_SSIO_I_INDEX, (u16)(r_bidx - 1));
	return r;
}
/* ours */
static int h_enum(u8 *sd, u8 *arg) { ev(1, T32O_PTR(sd), T32O_PTR(arg), 0); return (int)r_in; }
static int h_set(u8 *sd, u8 *arg) { ev(2, T32O_PTR(sd), T32O_PTR(arg), 0); return (int)r_in; }
static int h_stream(u8 *sd, u32 *v) { ev(3, T32O_PTR(sd), v[0], v[1]); return (int)r_in; }
static u8 *h_reg(u8 *info) { return T32O_HOST(c_reg(info)); }
static void h_lock(u8 *m) { ev(5, T32O_PTR(m), 0, 0); }
static void h_unlock(u8 *m) { ev(6, T32O_PTR(m), 0, 0); }
static void h_put(u32 a) { ev(7, a, 0, 0); }
static void h_unreg(u32 c) { ev(8, c, 0, 0); }
static void h_deinit(u8 *s) { ev(9, T32O_PTR(s), 0, 0); }
static void h_gpio(u32 p, u32 f, u32 pins) { ev(10, p, f, pins); }
static const struct t32_ssio_os os = { h_enum, h_set, h_stream, h_reg, h_lock, h_unlock, h_put, h_unreg, h_deinit, h_gpio,
				       &t32_data_d9008, t32_data_d9450, t32_data_f384c };
/* vendor (argument registers the vendor leaves unset are not recorded) */
#define V0(name, body) u64 name(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; body; }
V0(subdev_sensor_ops_enum_input, ev(1, a0, a1, 0); return (u64)r_in)
V0(subdev_sensor_ops_set_input, ev(2, a0, a1, 0); return (u64)r_in)
V0(vin_s_stream, ev(3, a0, t32_g_rd32(T32O_HOST(a1), 0), t32_g_rd32(T32O_HOST(a1), 4)); return (u64)r_in)
V0(subdev_sensor_register_i2c_device, return c_reg(T32O_HOST(a0)))
V0(t32o_mutex_lock, ev(5, a0, 0, 0); return 0)
V0(t32o_mutex_unlock, ev(6, a0, 0, 0); return rnd())
V0(private_i2c_put_adapter, ev(7, a0, 0, 0); return 0)
V0(private_i2c_unregister_device, ev(8, a0, 0, 0); return 0)
V0(tx_isp_subdev_deinit, ev(9, a0, 0, 0); return 0)
V0(private_jzgpio_set_func, ev(10, a0, a1, a2); return 0)

/* both sides: level, format text, then each conversion (%s by its text) */
int isp_printf(unsigned int level, const char *fmt, ...)
{
	unsigned int k = 3;
	const char *f;
	va_list ap;
	u32 *e = ev(11, level, hstr(fmt), 0);

	va_start(ap, fmt);
	for (f = fmt; *f && k < 10; f++) {
		if (*f != '%')
			continue;
		while (f[1] && !strchr("sdxucp", f[1]))
			f++;
		f++;
		if (*f == 's') {
			unsigned long p = va_arg(ap, unsigned long);

			e[k++] = hstr((const char *)(uintptr_t)(side ? (u32)p : p));
		} else
			e[k++] = va_arg(ap, unsigned int);
	}
	va_end(ap);
	return 0;
}

#define O_SD	0x0000u		/* 0x200 */
#define O_ARG	0x0200u		/* 0x80 */
#define O_S(i)	(0x0280u + 0x140u * (i))	/* two sensor subdevs */
#define O_P(i)	((i) < 2 ? 0x0500u + 0x380u * (i) : 0x0c00u + 0x160u * ((i) - 2))	/* privates, 3 list entries */
#define O_OPS(i) (0x1040u + 0x20u * (i))
#define O_CORE(i) (0x1080u + 0x20u * (i))
#define O_STB(i) (0x10c0u + 0x20u * (i))
#define O_CL(i)	(0x1100u + 0x20u * (i))
#define O_W880	0x1140u
#define IMGSZ	0x1180u
static u8 *img, save0[IMGSZ], save1[IMGSZ];
static u32 g0[NIDX], g1[NIDX], dbg[4];
static const char *const names[] = { "gc2053", "sc2336", "", "gc2053x", "imx307" };

static int fail(const char *what, unsigned int c)
{
	unsigned int i, k, s;

	printf("%s: FAIL case %u: %s (cmd %08x our %08x ref %08x sd %08x)\n", SSIO_NAME, c, what, dbg[0], dbg[1], dbg[2], dbg[3]);
	if (getenv("T32O_VERBOSE"))
		for (s = 0; s < 2; s++)
			for (i = 0; i < EV[s].n && i < MAXE; i++) {
				printf("%s %2u:", s ? "ref" : "our", i);
				for (k = 0; k < 10; k++)
					printf(" %08x", EV[s].e[i][k]);
				printf("\n");
			}
	return 1;
}

static u32 W(u32 off) { return T32O_PTR(img + off); }
static u32 pick_ret(void)
{
	static const u32 r[] = { 0, 0, (u32)-515, (u32)-22, 7 };

	return rnd() % 8 ? r[rnd() % 5] : rnd();
}
static void name(u8 *p) { memset(p, 0, 16); strcpy((char *)p, names[rnd() % 5]); }

/* sensor list from the subdev head through n of the 5 privates / entries */
static void build_list(void)
{
	u32 head = W(O_SD + T32_SSIO_HEAD), prev = head, n = rnd() % 5, i, used = 0, k, node;

	for (i = 0; i < n; i++) {
		do
			k = rnd() % 5;
		while (used & (1u << k));
		used |= 1u << k;
		node = W(O_P(k) + T32_SSIO_P_NODE);
		t32_g_wr32(T32O_HOST(prev), 0, node);
		t32_g_wr32(img + O_P(k), T32_SSIO_P_NODE + 4, prev);
		prev = node;
	}
	t32_g_wr32(T32O_HOST(prev), 0, head);
	t32_g_wr32(img + O_SD, T32_SSIO_HEAD + 4, prev);
}

int main(void)
{
	unsigned int c, i, n = 60000, hit[4] = { 0 }, nspi = 0, spi;

	img = t32o_alloc(IMGSZ);
	for (c = 0; c < n; c++) {
		u8 *sd = img + O_SD, *arg = img + O_ARG;
		u32 sdw = W(O_SD), argw = W(O_ARG), cmd, ro = 0, rr = 0, k;

		for (i = 0; i < IMGSZ; i++)
			img[i] = (u8)rnd();
		if (SSIO_PHASE == 1 || rnd() % 4 == 0)
			cmd = rnd() % 16 ? T32_SSIO_CMD_BASE - 1 + rnd() % 35 : rnd();
		else
			cmd = T32_SSIO_CMD_BASE + rnd() % 2;
		if (SSIO_PHASE == 1 && cmd - T32_SSIO_CMD_BASE < 2)
			cmd += 2;
		for (i = 0; i < 2; i++) {	/* sensors */
			u8 *s = img + O_S(i), *p = img + O_P(i);

			t32_g_wr32(s, T32_SSIO_S_OPS, W(O_OPS(i)));
			t32_g_wr32(s, T32_SSIO_S_CLIENT, W(O_CL(i)));
			t32_g_wr32(s, T32_SSIO_S_PRIV, W(O_P(i)));
			t32_g_wr32(img + O_OPS(i), 0, rnd() % 5 ? W(O_CORE(i)) : 0);
			t32_g_wr32(img + O_OPS(i), 12, rnd() % 5 ? W(O_STB(i)) : 0);
			t32_g_wr32(img + O_CORE(i), 0, rnd() % 5 ? T32O_PTR(c_init) : 0);
			t32_g_wr32(img + O_CORE(i), 16, rnd() % 5 ? T32O_PTR(c_core) : 0);
			t32_g_wr32(img + O_CORE(i), 28, rnd() % 5 ? T32O_PTR(c_core) : 0);
			t32_g_wr32(img + O_STB(i), 8, rnd() % 5 ? T32O_PTR(c_hnd) : 0);
			t32_g_wr32(img + O_CL(i), 24, rnd() % 2 ? 0 : rnd());
			t32_g_wr32(p, 880, W(O_W880));
		}
		for (i = 0; i < 5; i++) {	/* privates and list entries */
			u8 *p = img + O_P(i);

			name(p + T32_SSIO_P_INFO);
			t32_g_wr32(p, 344, rnd() % 4 ? 1 + rnd() % 2 : rnd() % 3 ? 0 : rnd());
			t32_g_wr32(p, T32_SSIO_S_CLIENT, W(O_CL(rnd() % 2)));
		}
		build_list();
		for (i = 0; i < 6; i++) {	/* active sensors and states per index */
			k = rnd() % 4;
			if (k == 3)	/* a list entry (release) or an error pointer (get input) */
				t32_g_wr32(sd, T32_SSIO_SLOT(i), cmd == T32_SSIO_CMD_BASE + 1 ? W(O_P(rnd() % 5)) :
					   cmd == T32_SSIO_CMD_BASE + 3 ? (u32)-1 - rnd() % 4095 : W(O_S(0)));
			else
				t32_g_wr32(sd, T32_SSIO_SLOT(i), k ? W(O_S(k - 1)) : 0);
			t32_g_wr32(sd, T32_SSIO_STATE(i), rnd() % 3 ? 4 : rnd() % 2 ? 1 : rnd() % 6);
		}
		t32_g_wr32(sd, T32_SSIO_ATTR, rnd() % 2 ? 3 + rnd() % 2 : rnd() % 6);
		t32_g_wr32(sd, T32_SSIO_NOTIFY, rnd() % 6 ? T32O_PTR(c_ntf) : 0);
		/* argument / sensor info */
		name(arg);
		if (rnd() % 2)
			memcpy(arg, img + O_P(rnd() % 5) + T32_SSIO_P_INFO, 16);
		t32_g_wr32(arg, 0, rnd() % 6);
		t32_g_wr32(arg, 4, rnd() % 6);
		t32_g_wr32(arg, 32, rnd() % 4 ? (rnd() % 5 ? 1 : rnd() % 6) : rnd() % 6);
		if (t32_g_rd32(arg, 32) == 2 && (cmd != T32_SSIO_CMD_BASE || rnd() % 3))	/* SPI: vendor NULL read (UB) */
			t32_g_wr32(arg, 32, 3);
		if (cmd == T32_SSIO_CMD_BASE + 17 || cmd == T32_SSIO_CMD_BASE + 18 || cmd == T32_SSIO_CMD_BASE + 1)
			t32_g_wr32(arg, 32, rnd() % 6);
		{
			static const u32 gp[] = { 0, 25, 26, 38, 39, 51, 60, 62, 66, 68, 52, 40, 1 };

			t32_g_wr32(arg, T32_SSIO_I_GPIO, rnd() % 8 ? gp[rnd() % 13] : rnd());
		}
		t32_g_wr16(arg, T32_SSIO_I_INDEX, (u16)(rnd() % 6));
		for (i = 0; i < NIDX; i++) {
			t32_data_d9450[i] = rnd() % 3;
			strcpy(t32_data_f384c + 40 * i, names[rnd() % 5]);
		}
		t32_data_d9008 = rnd() % 2 ? 0 : rnd() % 2 ? 1 : rnd();
		/* hooks */
		r_bus = rnd() % 6 ? 0 : 1 + rnd() % 4;
		r_bidx = rnd() % 6 ? 0 : 1 + rnd() % 6;
		r_in = pick_ret();
		r_hnd = pick_ret();
		r_mod_on = rnd() % 4 ? 0 : 1 + rnd() % 2;
		r_mod = rnd() % 6;
		r_ntf = rnd() % 3 ? 0 : pick_ret();
		r_nval = rnd();
		for (i = 0; i < 2; i++) {
			r_init[i] = rnd() % 2 ? 0 : pick_ret();
			k = rnd() % 8;
			r_reg[i] = k < 5 ? W(O_S(k % 2)) : k == 5 ? 0 : (u32)-1 - rnd() % 4095;
		}
		/* NULL subdev / arg only where the vendor checks them or only passes them on */
		if (rnd() % 16 == 0 && (cmd - T32_SSIO_CMD_BASE < 3 ||
					 cmd - T32_SSIO_CMD_BASE == 3 || cmd - T32_SSIO_CMD_BASE == 4 ||
					 cmd - T32_SSIO_CMD_BASE == 32))
			sdw = 0;
		if (rnd() % 16 == 0 && (cmd - T32_SSIO_CMD_BASE < 5 && sdw))
			argw = 0;
		if (!sdw && cmd == T32_SSIO_CMD_BASE + 3)	/* set_input(NULL, arg) only */
			argw = rnd() % 2 ? argw : 0;
		memcpy(save0, img, IMGSZ);
		memcpy(g0, t32_data_d9450, sizeof(g0));
		/*
		 * BEYOND VENDOR (#281): register with bus type 2 (SPI) that gets past the
		 * duplicate and state checks: the vendor dereferences a NULL sensor, ours returns
		 * -22 without a lock or any write.  Ours only, counted, not compared.
		 */
		spi = cmd == T32_SSIO_CMD_BASE && sdw && argw && t32_g_rd32(arg, 32) == 2;
		if (spi) {
			k = t32_ssio_index(arg);
			if ((t32_data_d9008 && t32_data_d9450[k] + 1 == 2 && !strcmp((const char *)arg, t32_data_f384c + 40 * k)) ||
			    t32_g_rd32(sd, T32_SSIO_STATE(k)) == 1)
				spi = 0;
		}
		if (spi) {
			EV[0].n = 0;
			ninit = nreg = 0;
			ro = (u32)t32_subdev_sensor_ops_ioctl(&os, T32O_HOST(sdw), cmd, T32O_HOST(argw));
			dbg[0] = cmd; dbg[1] = ro; dbg[2] = 0; dbg[3] = sdw;
			if (ro != (u32)-22 || memcmp(img, save0, IMGSZ) || EV[0].n)
				return fail("SPI register guard (beyond vendor)", c);
			nspi++;
			continue;
		}
		for (side = 0; side < 2; side++) {
			memcpy(img, save0, IMGSZ);
			memcpy(t32_data_d9450, g0, sizeof(g0));
			EV[side].n = 0;
			ninit = nreg = 0;
			memset(EV[side].e, 0, sizeof(EV[side].e));
			if (!side) {
				ro = (u32)t32_subdev_sensor_ops_ioctl(&os, T32O_HOST(sdw), cmd, T32O_HOST(argw));
				memcpy(save1, img, IMGSZ);
				memcpy(g1, t32_data_d9450, sizeof(g1));
			} else {
				rr = (u32)t32o_call(R(subdev_sensor_ops_ioctl), sdw, cmd, argw, 0, 0);
			}
		}
		dbg[0] = cmd; dbg[1] = ro; dbg[2] = rr; dbg[3] = sdw;
		if (ro != rr)
			return fail("return value", c);
		if (memcmp(img, save1, IMGSZ))
			return fail("memory image", c);
		if (memcmp(t32_data_d9450, g1, sizeof(g1)))
			return fail("registration counts", c);
		if (EV[0].n != EV[1].n || memcmp(EV[0].e, EV[1].e, sizeof(EV[0].e)))
			return fail("hook call log", c);
		hit[ro == 0 ? (EV[0].n ? 0 : 1) : ro == (u32)-22 || ro == (u32)-19 || ro == (u32)-1 ? 2 : 3]++;
	}
	printf("%s: %u cases, 0 mismatches; 0 with hooks / 0 without / -22, -19 or -1 / other: %u %u %u %u;"
	       " %u SPI registers (beyond vendor, ours only)\n",
	       SSIO_NAME, n - nspi, hit[0], hit[1], hit[2], hit[3], nspi);
	if (SSIO_PHASE == 2 && !nspi)
		return 1;
	return 0;
}
