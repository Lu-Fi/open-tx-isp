// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our sensor input selection
 * (driver/t32/tx_isp_t32_sensor_input.h) against the unmodified vendor
 * subdev_sensor_ops_set_input (Matt Davis' recovered C, PR #15 @ 2e2e06bc).
 * A random image (sensor subdev with input slots, a circular list of 0..5
 * sensors in random order, an input) runs through ours, is restored and runs
 * through the vendor; compared: every byte, the return value and the ordered
 * log of the hook calls (mutex lock / unlock, every subdev callback with its
 * subdev, cmd and argument, isp_printf level / format text / every argument,
 * %s by its text).  The callbacks answer from the same random script on both
 * sides and may rewrite the input index.  Exit 0 = all equal.
 */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_sensor_input.h"

#define R(f)	((t32o_fn)(void (*)(void))(f))
/* vendor rodata (data.c, decoded) and the module flag */
char t32_data_bd9e8[] = "subdev_sensor_ops_set_input";
char t32_data_c411c[] = "Please, streamoff sensor firstly!\n";
char t32_data_c4140[] = "[ %s:%d ] Failed to deinit the pipeline of %s.\n";
char t32_data_c4170[] = "[ %s:%d  ] Failed to the set input sensor(%d) that .\n";
u32 t32_data_d9008;

u64 subdev_sensor_ops_set_input(u32, u32, u32, u32);

static u32 rng = 0x6c8e9cf5u;
static u32 rnd(void)
{
	rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
	return rng;
}

#define MAXE 16
static struct { unsigned int n; u32 e[MAXE][8]; } EV[2];
static int side;
static u32 inw, cbn, cb_ret[8], cb_mod[8];
static void ev(u32 a, u32 b, u32 c, u32 d)
{
	unsigned int n = EV[side].n++;

	if (n < MAXE) {
		EV[side].e[n][0] = a; EV[side].e[n][1] = b; EV[side].e[n][2] = c; EV[side].e[n][3] = d;
	}
}
static u32 hash(const u8 *p, u32 n)
{
	u32 h = 2166136261u, i;

	for (i = 0; i < n; i++)
		h = (h ^ p[i]) * 16777619u;
	return h;
}
static void h_lock(u8 *m) { ev(1, T32O_PTR(m), 0, 0); }
static void h_unlock(u8 *m) { ev(2, T32O_PTR(m), 0, 0); }
u64 t32o_mutex_lock(u32 m, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; ev(1, m, 0, 0); return 0; }
u64 t32o_mutex_unlock(u32 m, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; ev(2, m, 0, 0); return rnd(); }	/* v0 garbage */
static const struct t32_sinp_os os = { h_lock, h_unlock, &t32_data_d9008 };

static u32 cb_common(u32 sd, u32 cmd, u32 arg)
{
	u32 k = cbn++ % 8;

	ev(3, sd, cmd, arg);
	if (arg && arg == inw && cb_mod[k])
		t32_g_wr32(T32O_HOST(inw), 4, cb_mod[k] - 1);
	return cb_ret[k];
}
static int ho(u8 *sd, u32 cmd, void *arg) { return (int)cb_common(T32O_PTR(sd), cmd, T32O_PTR(arg)); }
static u64 hr(u32 sd, u32 cmd, u32 arg, u32 a3) { (void)a3; return cb_common(sd, cmd, arg); }

/* both sides: level, format text, then each conversion (%s by its text) */
int isp_printf(unsigned int level, const char *fmt, ...)
{
	unsigned int n = EV[side].n++, k = 3;
	const char *f;
	va_list ap;

	va_start(ap, fmt);
	if (n < MAXE) {
		EV[side].e[n][0] = 9; EV[side].e[n][1] = level;
		EV[side].e[n][2] = hash((const u8 *)fmt, (u32)strlen(fmt));
		for (f = fmt; *f && k < 8; f++) {
			if (*f != '%')
				continue;
			while (f[1] && !strchr("sdxuc", f[1]))
				f++;
			f++;
			if (*f == 's') {
				unsigned long p = va_arg(ap, unsigned long);
				const char *s = (const char *)T32O_HOST((u32)p);

				EV[side].e[n][k++] = hash((const u8 *)s, (u32)strlen(s));
			} else {
				EV[side].e[n][k++] = va_arg(ap, unsigned int);
			}
		}
	}
	va_end(ap);
	return 0;
}

#define O_SD	0x0000u		/* the subdev, also read as the "sensor" at sd - 0x10 */
#define O_SEN	0x0400u		/* 5 x 0x400 */
#define O_INP	0x1800u
#define O_NAME	0x1840u		/* 6 x 16 */
#define IMGSZ	0x1900u
#define NCB	7
static u8 *img, save0[IMGSZ], save1[IMGSZ];
static u32 cb_at[NCB], cb_on;

/* callback words: per side its own hook, a placeholder 1 in the compared image */
static void cb_set(int vendor)
{
	unsigned int k;

	for (k = 0; k < NCB; k++)
		t32_g_wr32(img, cb_at[k], (cb_on >> k & 1) ? (vendor ? T32O_PTR(hr) : T32O_PTR(ho)) : 0);
}
static void cb_clear(int vendor)
{
	unsigned int k;

	for (k = 0; k < NCB; k++)
		if (t32_g_rd32(img, cb_at[k]) == (vendor ? T32O_PTR(hr) : T32O_PTR(ho)))
			t32_g_wr32(img, cb_at[k], 1);
}

static int fail(const char *what, unsigned int c)
{
	printf("t32_sinp_oracle: FAIL case %u: %s\n", c, what);
	return 1;
}

int main(void)
{
	unsigned int c, i, n = 60000, hit[8] = { 0 };

	img = t32o_alloc(IMGSZ);
	cb_at[0] = O_SD + T32_SIN_IOCTL;
	cb_at[1] = O_SD - 0x10u + T32_SIN_IOCTL;	/* off-list cursor sd - 0x10 */
	for (i = 0; i < 5; i++)
		cb_at[2 + i] = O_SEN + i * 0x400u + T32_SIN_IOCTL;
	for (c = 0; c < n; c++) {
		unsigned int ns = rnd() % 6, ord[5] = { 0, 1, 2, 3, 4 };
		u8 *sd = img + O_SD, *inp = img + O_INP;
		u32 sdw = T32O_PTR(sd), ro = 0, rr = 0, prev, b;

		inw = T32O_PTR(inp);
		for (i = 0; i < IMGSZ; i++)
			img[i] = (u8)rnd();
		for (i = 0; i < 6; i++) {
			memcpy(img + O_NAME + i * 16, "sensor-x", 9);
			img[O_NAME + i * 16 + 7] = (u8)('0' + i);
		}
		for (i = 4; i > 0; i--) {	/* random list order */
			u32 j = rnd() % (i + 1), t = ord[i];

			ord[i] = ord[j]; ord[j] = t;
		}
		prev = T32O_PTR(sd + T32_SIN_LIST);
		for (i = 0; i < ns; i++) {
			u8 *s = img + O_SEN + ord[i] * 0x400;

			t32_g_wr32(T32O_HOST(prev), 0, T32O_PTR(s + T32_SIN_NODE));
			prev = T32O_PTR(s + T32_SIN_NODE);
		}
		t32_g_wr32(T32O_HOST(prev), 0, T32O_PTR(sd + T32_SIN_LIST));
		for (i = 0; i < 6; i++) {	/* the sensors and the off-list cursor */
			u8 *s = i < 5 ? img + O_SEN + i * 0x400 : sd - 0x10;

			t32_g_wr32(s, T32_SIN_INDEX, rnd() % 8);
			t32_g_wr16(s, T32_SIN_OWNIDX, (u16)(rnd() % 8));
			t32_g_wr32(s, T32_SIN_NAMEP, T32O_PTR(img + O_NAME + i * 16));
		}
		for (i = 0; i < 8; i++) {	/* input slots */
			u32 k = rnd() % 8;

			t32_g_wr32(sd, T32_SIN_SLOT + i * 4, k < 5 ? T32O_PTR(img + O_SEN + k * 0x400) : 0);
		}
		b = rnd() % 8;
		if (rnd() % 4 == 0)
			t32_g_wr32(sd, T32_SIN_SLOT + b * 4 + T32_SIN_BUSY, rnd() % 2 ? T32_SIN_STREAMING : rnd() % 8);
		t32_g_wr32(sd, T32_SIN_IDXMODE, rnd() % 2 ? 1 : rnd() % 2 ? 0 : rnd());
		t32_g_wr32(inp, 0, rnd() % 4 ? 1 + rnd() % 3 * (rnd() % 2) : 0);
		t32_g_wr32(inp, 4, rnd() % 8);
		t32_data_d9008 = rnd() % 2 ? 0 : rnd() % 2 ? 1 : rnd();
		cb_on = rnd() % 4 ? 0x7f : rnd();
		for (i = 0; i < 8; i++) {
			cb_ret[i] = rnd() % 5 ? 0 : rnd() % 2 ? (u32)-22 : rnd();
			cb_mod[i] = rnd() % 6 ? 0 : 1 + rnd() % 8;
		}
		if (rnd() % 16 == 0)
			sdw = 0;
		if (rnd() % 16 == 0)
			inw = 0;
		memcpy(save0, img, IMGSZ);
		for (side = 0; side < 2; side++) {
			memcpy(img, save0, IMGSZ);
			cb_set(side);
			EV[side].n = 0;
			cbn = 0;
			memset(EV[side].e, 0, sizeof(EV[side].e));
			if (!side) {
				ro = (u32)t32_subdev_sensor_ops_set_input(&os, T32O_HOST(sdw), T32O_HOST(inw));
				cb_clear(0);
				memcpy(save1, img, IMGSZ);
			} else {
				rr = (u32)t32o_call(R(subdev_sensor_ops_set_input), sdw, inw, 0, 0, 0);
				cb_clear(1);
			}
		}
		if (ro != rr)
			return fail("return value", c);
		if (memcmp(img, save1, IMGSZ))
			return fail("memory image", c);
		if (EV[0].n != EV[1].n || memcmp(EV[0].e, EV[1].e, sizeof(EV[0].e)))
			return fail("hook call log", c);
		hit[ro == 0 ? 0 : ro == (u32)-22 ? 1 : ro == (u32)-515 ? 2 : ro == (u32)-1 ? 3 : 4]++;
		if (ro == 0 && EV[0].n > 4)
			hit[5]++;
	}
	printf("t32_sinp_oracle: %u cases, 0 mismatches; 0 / -22 / -515 / -1 / other: %u %u %u %u %u; full select: %u\n",
	       n, hit[0], hit[1], hit[2], hit[3], hit[4], hit[5]);
	return 0;
}
