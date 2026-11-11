// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our sensor input enumeration and I2C sensor registration
 * (driver/t32/tx_isp_t32_sensor_reg.h) against the unmodified vendor
 * subdev_sensor_ops_enum_input and subdev_sensor_register_i2c_device (Matt
 * Davis' recovered C, PR #15 @ 2e2e06bc).  A random image (sensor subdev with
 * a circular list of 0..5 sensors in random order, a v4l2_input, a register
 * info) runs through ours, is restored and runs through the vendor; compared:
 * every byte, the return values and the ordered log of the hook calls (mutex,
 * I2C adapter get / put, new subdev board with the 44 info bytes, isp_printf
 * level / format text / every argument, %s by its text).
 * Exit 0 = all equal, 1 = first mismatch.
 */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_sensor_reg.h"

#define R(f)	((t32o_fn)(void (*)(void))(f))
/* vendor rodata (data.c, decoded): function name and the three formats */
char t32_data_bd960[] = "subdev_sensor_register_i2c_device";
char t32_data_c44ec[] = "[ %s:%d ] Failed to get I2C adapter %d, deferring probe!!!\n";
char t32_data_c4528[] = "[ %s:%d ] sensor id is %d, i2c name size is %d, type is %s, addr is 0x%02x, adapter id is %d\n";
char t32_data_c4588[] = "[ %s:%d ] Failed to acquire subdev %s, deferring probe\n";

u64 subdev_sensor_ops_enum_input(u32, u32, u32, u32);
u64 subdev_sensor_register_i2c_device(u32, u32, u32, u32);

static u32 rng = 0x2545f491u;
static u32 rnd(void)
{
	rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
	return rng;
}

#define MAXE 16
static struct { unsigned int n; u32 e[MAXE][8]; } EV[2];
static int side;
static u32 r_adap, r_sd;
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
static u32 h_get(u32 nr) { ev(3, nr, 0, 0); return r_adap; }
static u32 h_new(u32 ad, u8 *info, u32 pd) { ev(4, ad, hash(info, T32_I2C_INFO_BYTES), pd); return r_sd; }
static void h_put(u32 ad) { ev(5, ad, 0, 0); }
static const struct t32_sreg_os os = { h_lock, h_unlock, h_get, h_new, h_put };

u64 t32o_mutex_lock(u32 m, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; ev(1, m, 0, 0); return 0; }
u64 t32o_mutex_unlock(u32 m, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; ev(2, m, 0, 0); return 0; }
u64 private_i2c_get_adapter(u32 nr, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; return h_get(nr); }
u64 isp_i2c_new_subdev_board(u32 ad, u32 info, u32 pd, u32 a3) { (void)a3; return h_new(ad, T32O_HOST(info), pd); }
u64 private_i2c_put_adapter(u32 ad, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; h_put(ad); return 0; }

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
				const char *s = side ? (const char *)T32O_HOST((u32)p) : (const char *)p;

				EV[side].e[n][k++] = hash((const u8 *)s, (u32)strlen(s));
			} else {
				EV[side].e[n][k++] = va_arg(ap, unsigned int);
			}
		}
	}
	va_end(ap);
	return 0;
}

#define O_SD	0x0000u
#define O_SEN	0x0200u		/* 5 x 0x200 */
#define O_INP	0x0c00u
#define O_RI	0x0c40u
#define IMGSZ	0x0d00u
static u8 *img, save0[IMGSZ], save1[IMGSZ];

static int fail(const char *what, unsigned int c, unsigned int op)
{
	printf("t32_sreg_oracle: FAIL case %u op %u: %s\n", c, op, what);
	return 1;
}

int main(void)
{
	unsigned int c, i, n = 40000, eff[2] = { 0 }, hit[3] = { 0 };

	img = t32o_alloc(IMGSZ);
	for (c = 0; c < n; c++) {
		unsigned int op = c % 2, ns = rnd() % 6, ord[5] = { 0, 1, 2, 3, 4 };
		u8 *sd = img + O_SD, *inp = img + O_INP, *ri = img + O_RI;
		u32 sdw = T32O_PTR(sd), inw = T32O_PTR(inp), ro = 0, rr = 0, prev;

		for (i = 0; i < IMGSZ; i++)
			img[i] = (u8)rnd();
		for (i = 4; i > 0; i--) {	/* random list order */
			u32 j = rnd() % (i + 1), t = ord[i];

			ord[i] = ord[j]; ord[j] = t;
		}
		prev = T32O_PTR(sd + T32_SSD_LIST);
		for (i = 0; i < ns; i++) {
			u8 *s = img + O_SEN + ord[i] * 0x200;

			t32_g_wr32(T32O_HOST(prev), 0, T32O_PTR(s + T32_SEN_NODE));
			prev = T32O_PTR(s + T32_SEN_NODE);
			if (rnd() % 2) {
				s[T32_SEN_OWNIDX] = (u8)(rnd() % 6);
				s[T32_SEN_OWNIDX + 1] = rnd() % 4 ? 0 : (u8)rnd();
			}
			if (rnd() % 3 == 0)
				s[T32_SEN_NAME + rnd() % 40] = 0;
		}
		t32_g_wr32(T32O_HOST(prev), 0, T32O_PTR(sd + T32_SSD_LIST));
		t32_g_wr32(sd, T32_SSD_IDXMODE, rnd() % 2 ? 1 : rnd() % 2 ? 0 : rnd());
		t32_g_wr32(inp, 0, rnd() % 5 ? rnd() % 7 : rnd());
		if (rnd() % 6 == 0)	/* the end-of-list quirk */
			t32_g_wr32(sd, T32_SEN_INDEX - T32_SEN_NODE + T32_SSD_LIST, t32_g_rd32(inp, 0));
		if (rnd() % 16 == 0)
			sdw = 0;
		if (rnd() % 16 == 0)
			inw = 0;
		r_adap = rnd() % 4 ? rnd() : 0;
		r_sd = rnd() % 3 == 0 ? 0 : rnd() % 3 == 0 ? 0xfffff001u + rnd() % 0xfff : rnd() % 2 ? 0xfffff000u : rnd();
		if (rnd() % 2)
			ri[T32_RINFO_NAME + rnd() % 24] = 0;
		ri[0xb0] = 0;	/* the name string ends inside the image */
		memcpy(save0, img, IMGSZ);
		for (side = 0; side < 2; side++) {
			memcpy(img, save0, IMGSZ);
			EV[side].n = 0;
			memset(EV[side].e, 0, sizeof(EV[side].e));
			if (!side) {
				if (op == 0)
					ro = (u32)t32_subdev_sensor_ops_enum_input(&os, T32O_HOST(sdw), T32O_HOST(inw));
				else
					ro = t32_subdev_sensor_register_i2c_device(&os, ri);
				memcpy(save1, img, IMGSZ);
			} else {
				if (op == 0)
					rr = (u32)t32o_call(R(subdev_sensor_ops_enum_input), sdw, inw, 0, 0, 0);
				else
					rr = (u32)t32o_call(R(subdev_sensor_register_i2c_device), T32O_PTR(ri), 0, 0, 0, 0);
			}
		}
		if (ro != rr)
			return fail("return value", c, op);
		if (memcmp(img, save1, IMGSZ))
			return fail("memory image", c, op);
		if (EV[0].n != EV[1].n || memcmp(EV[0].e, EV[1].e, sizeof(EV[0].e)))
			return fail("hook call log", c, op);
		if (memcmp(img, save0, IMGSZ))
			eff[op]++;
		if (op == 0)
			hit[ro == 0 ? (memcmp(img, save0, IMGSZ) ? 0 : 1) : 2]++;
	}
	printf("t32_sreg_oracle: %u cases, 0 mismatches; changed memory per op: %u %u; enum found / quirk 0 / -22: %u %u %u\n",
	       n, eff[0], eff[1], hit[0], hit[1], hit[2]);
	return 0;
}
