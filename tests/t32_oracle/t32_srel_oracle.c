// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our release of all sensors (driver/t32/tx_isp_t32_sensor_rel.h)
 * against the unmodified vendor subdev_sensor_ops_release_all_sensor (Matt
 * Davis' recovered C, PR #15 @ 2e2e06bc).  A random image (subdev with a
 * circular list of 0..5 sensor nodes in random order, I2C clients) runs
 * through ours, is restored and runs through the vendor; compared: every
 * byte, the return value and the ordered log of the hook calls (mutexes,
 * adapter put, client unregister, isp_printf level / format / arguments).
 * Exit 0 = all equal, 1 = first mismatch.
 */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_sensor_rel.h"

#define R(f)	((t32o_fn)(void (*)(void))(f))
char t32_data_bd9c0[] = "subdev_sensor_ops_release_all_sensor";
char t32_data_c4478[] = "[ %s:%d ] the devnode does't have been opened.\n";
char t32_data_c44a8[] = "[ %s:%d ] the type of sensor SBUS hasn't been defined.\n";
u64 subdev_sensor_ops_release_all_sensor(u32, u32, u32, u32);

static u32 rng = 0x5851f42du;
static u32 rnd(void)
{
	rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
	return rng;
}

#define MAXE 32
static struct { unsigned int n; u32 e[MAXE][5]; } EV[2];
static int side;
static void ev(u32 a, u32 b)
{
	unsigned int n = EV[side].n++;

	if (n < MAXE) {
		EV[side].e[n][0] = a; EV[side].e[n][1] = b;
	}
}
static u32 hs(const char *s)
{
	u32 h = 2166136261u;

	while (*s)
		h = (h ^ (u8)*s++) * 16777619u;
	return h;
}
static void h_lock(u8 *m) { ev(1, T32O_PTR(m)); }
static void h_unlock(u8 *m) { ev(2, T32O_PTR(m)); }
static void h_put(u32 a) { ev(3, a); }
static void h_unreg(u32 c) { ev(4, c); }
static const struct t32_srel_os os = { h_lock, h_unlock, h_put, h_unreg };
u64 t32o_mutex_lock(u32 m, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; ev(1, m); return 0; }
u64 t32o_mutex_unlock(u32 m, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; ev(2, m); return 0; }
u64 private_i2c_put_adapter(u32 a, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; h_put(a); return 0; }
u64 private_i2c_unregister_device(u32 c, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; h_unreg(c); return 0; }
int isp_printf(unsigned int level, const char *fmt, ...)
{
	unsigned int n = EV[side].n++;
	unsigned long p;
	va_list ap;

	va_start(ap, fmt);
	p = va_arg(ap, unsigned long);
	if (n < MAXE) {
		EV[side].e[n][0] = 9; EV[side].e[n][1] = level; EV[side].e[n][2] = hs(fmt);
		EV[side].e[n][3] = hs(side ? (const char *)T32O_HOST((u32)p) : (const char *)p);
		EV[side].e[n][4] = va_arg(ap, unsigned int);
	}
	va_end(ap);
	return 0;
}

#define O_SD	0x0000u
#define O_SEN	0x0200u		/* 5 x 0x80, node at +0x40 */
#define O_CLI	0x0480u		/* 5 x 0x40 */
#define IMGSZ	0x0600u
static u8 *img, save0[IMGSZ], save1[IMGSZ];

static int fail(const char *what, unsigned int c)
{
	printf("t32_srel_oracle: FAIL case %u: %s\n", c, what);
	return 1;
}

int main(void)
{
	unsigned int c, i, n = 40000, eff = 0, ok = 0;

	img = t32o_alloc(IMGSZ);
	for (c = 0; c < n; c++) {
		u8 *sd = img + O_SD;
		u32 sdw = T32O_PTR(sd), ro = 0, rr = 0, ns = rnd() % 6, ord[5] = { 0, 1, 2, 3, 4 }, prev;

		for (i = 0; i < IMGSZ; i++)
			img[i] = (u8)rnd();
		for (i = 4; i > 0; i--) {
			u32 j = rnd() % (i + 1), t = ord[i];

			ord[i] = ord[j]; ord[j] = t;
		}
		prev = T32O_PTR(sd + T32_SRL_LIST);
		for (i = 0; i < ns; i++) {
			u8 *node = img + O_SEN + ord[i] * 0x80 + 0x40, *cli = img + O_CLI + ord[i] * 0x40;
			u32 k = rnd() % 16;

			t32_g_wr32(T32O_HOST(prev), 0, T32O_PTR(node));
			t32_g_wr32(node, 4, prev);
			prev = T32O_PTR(node);
			t32_g_wr32(node, T32_SRL_NODE_BUS, k < 8 ? 1 : k < 15 ? 2 : rnd() % 2 ? 0 : rnd());
			t32_g_wr32(node - T32_SRL_NODE_CLIENT, 0, T32O_PTR(cli));
			if (rnd() % 3 == 0)
				t32_g_wr32(cli, T32_I2C_CLIENT_ADAP, 0);
		}
		t32_g_wr32(T32O_HOST(prev), 0, T32O_PTR(sd + T32_SRL_LIST));
		t32_g_wr32(sd, T32_SRL_LIST + 4, prev);
		t32_g_wr32(sd, T32_SRL_OPEN, rnd() % 6 == 0 ? 1 : rnd() % 2 ? 0 : rnd());
		if (rnd() % 16 == 0)
			sdw = 0;
		memcpy(save0, img, IMGSZ);
		for (side = 0; side < 2; side++) {
			memcpy(img, save0, IMGSZ);
			EV[side].n = 0;
			memset(EV[side].e, 0, sizeof(EV[side].e));
			if (!side) {
				ro = (u32)t32_subdev_sensor_ops_release_all_sensor(&os, T32O_HOST(sdw));
				memcpy(save1, img, IMGSZ);
			} else {
				rr = (u32)t32o_call(R(subdev_sensor_ops_release_all_sensor), sdw, 0, 0, 0, 0);
			}
		}
		if (ro != rr)
			return fail("return value", c);
		if (memcmp(img, save1, IMGSZ))
			return fail("memory image", c);
		if (EV[0].n != EV[1].n || memcmp(EV[0].e, EV[1].e, sizeof(EV[0].e)))
			return fail("hook call log", c);
		eff += memcmp(img, save0, IMGSZ) != 0;
		ok += ro == 0;
	}
	printf("t32_srel_oracle: %u cases, 0 mismatches; changed memory %u; returned 0 %u\n", n, eff, ok);
	return 0;
}
