// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our VIN stream control (driver/t32/tx_isp_t32_vin.h)
 * against the unmodified vendor vin_set_stream, vin_s_stream,
 * vin_oneshot_allsensor_stream and vin_core_ops_ioctl (Matt Davis' recovered
 * C, PR #15 @ 2e2e06bc).  A random image (VIN device, 4 sensor subdevs with
 * ops tables and info blocks, a sensor list, the request) runs through ours,
 * is restored and runs through the vendor.  Compared: every byte, the return
 * values and one ordered log of the mutex calls (address), the sensor
 * callbacks (s_stream, core +4, core +12 with their argument words), the
 * notify calls (cmd, event words) and isp_printf (level, line); the callbacks
 * answer from a script (0, -515, 7).  Exit 0 = all equal, 1 = first mismatch.
 */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_vin.h"

#define WEAK __attribute__((weak))
#define R(f)	((t32o_fn)(void (*)(void))(f))
WEAK char t32_data_bd8f4[4], t32_data_c41c8[4], t32_data_c41e0[4];

u64 vin_set_stream(u32, u32, u32, u32);
u64 vin_s_stream(u32, u32, u32, u32);
u64 vin_oneshot_allsensor_stream(u32, u32, u32, u32);
u64 vin_core_ops_ioctl(u32, u32, u32, u32);

static u32 rng = 0x7e57c0deu;
static u32 rnd(void)
{
	rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
	return rng;
}

#define MAXE 96
static struct { unsigned int n; u32 e[MAXE][5]; } EV[2];
static int side;
static u32 script[16];
static unsigned int ncb;
static void ev(u32 a, u32 b, u32 c, u32 d, u32 e)
{
	unsigned int n = EV[side].n++;

	if (n < MAXE) {
		EV[side].e[n][0] = a; EV[side].e[n][1] = b; EV[side].e[n][2] = c; EV[side].e[n][3] = d; EV[side].e[n][4] = e;
	}
}
static int answer(void)
{
	static const int a[4] = { 0, 0, -515, 7 };

	return a[script[ncb++ & 15] & 3];
}
/* mutex */
static void o_lock(u8 *m) { ev(1, T32O_PTR(m), 0, 0, 0); }
static void o_unlock(u8 *m) { ev(2, T32O_PTR(m), 0, 0, 0); }
u64 t32o_mutex_lock(u32 m, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; ev(1, m, 0, 0, 0); return 0; }
u64 t32o_mutex_unlock(u32 m, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; ev(2, m, 0, 0, 0); return 0; }
static const struct t32_vin_os os = { .lock = o_lock, .unlock = o_unlock };
/* sensor callbacks: 3 = s_stream, 4 = core +4, 5 = core +12 */
static int sd_common(u32 id, u32 sd, const u32 *w)
{
	ev(id, sd, w[0], w[1], 0);
	return answer();
}
static int o_sst(u8 *sd, void *a) { return sd_common(3, T32O_PTR(sd), a); }
static int o_c4(u8 *sd, void *a) { return sd_common(4, T32O_PTR(sd), a); }
static int o_c12(u8 *sd, void *a) { return sd_common(5, T32O_PTR(sd), a); }
static u64 r_sst(u32 sd, u32 a, u32 a2, u32 a3) { (void)a2; (void)a3; return (u32)sd_common(3, sd, T32O_HOST(a)); }
static u64 r_c4(u32 sd, u32 a, u32 a2, u32 a3) { (void)a2; (void)a3; return (u32)sd_common(4, sd, T32O_HOST(a)); }
static u64 r_c12(u32 sd, u32 a, u32 a2, u32 a3) { (void)a2; (void)a3; return (u32)sd_common(5, sd, T32O_HOST(a)); }
static int nt_common(u32 sd, u32 cmd, const u32 *w)
{
	ev(6, sd, cmd, w[0], w[1] << 16 | w[2]);
	return answer() == 7;
}
static int o_nt(u8 *sd, u32 cmd, void *a) { return nt_common(T32O_PTR(sd), cmd, a); }
static u64 r_nt(u32 sd, u32 cmd, u32 a, u32 a3) { (void)a3; return (u32)nt_common(sd, cmd, T32O_HOST(a)); }
int isp_printf(unsigned int level, const char *fmt, ...)
{
	va_list ap;
	unsigned long a3;

	(void)fmt;
	va_start(ap, fmt);
	(void)va_arg(ap, unsigned long);
	a3 = va_arg(ap, unsigned long);
	va_end(ap);
	ev(7, level, (u32)a3, 0, 0);
	return 0;
}

#define O_VIN	0x0000u		/* 0x200 */
#define O_SD(i)	(0x0200u + (i) * 0x400u)
#define O_OPS(i) (0x1200u + (i) * 0x40u)	/* { core, video }, core table +0x10 (4 words), video table +0x30 */
#define O_INFO(i) (0x1300u + (i) * 0x150u)
#define O_ENT(i) (0x1900u + (i) * 0x140u)
#define O_REQ	0x1e00u
#define IMGSZ	0x1e40u
static u8 *img, save0[IMGSZ], save1[IMGSZ];

static void fns(int vendor, int clear)
{
	unsigned int i;

	for (i = 0; i < 4; i++) {
		u8 *o = img + O_OPS(i);

		if (t32_g_rd32(o, 0x38))	/* marker: functions present */
			continue;
		t32_g_wr32(o, 0x14, clear || !(o[0x3c] & 1) ? 0 : vendor ? T32O_PTR(r_c4) : T32O_PTR(o_c4));
		t32_g_wr32(o, 0x1c, clear || !(o[0x3c] & 2) ? 0 : vendor ? T32O_PTR(r_c12) : T32O_PTR(o_c12));
		t32_g_wr32(o, 0x30, clear || !(o[0x3c] & 4) ? 0 : vendor ? T32O_PTR(r_sst) : T32O_PTR(o_sst));
	}
	t32_g_wr32(img + O_VIN, T32_G_SD_NOTIFY, clear || !(img[O_REQ + 0x30] & 1) ? 0 : vendor ? T32O_PTR(r_nt) : T32O_PTR(o_nt));
}

static int fail(const char *what, unsigned int c, unsigned int op)
{
	printf("t32_vin_oracle: FAIL case %u op %u: %s\n", c, op, what);
	return 1;
}

int main(void)
{
	static const u32 cmds[] = { T32_VIN_IOC_INIT, T32_VIN_IOC_EVENT, T32_VIN_IOC_ONESHOT_ON, T32_VIN_IOC_ONESHOT_OFF,
				    T32_VIN_IOC_START_ALL, 0x1000001u, 0x1000011u, 0x2000000u };
	unsigned int c, i, n = 40000, eff[4] = { 0 };

	img = t32o_alloc(IMGSZ);
	for (c = 0; c < n; c++) {
		unsigned int op = c % 4, nent = rnd() % 4;
		u8 *vin = img + O_VIN, *req = img + O_REQ;
		u32 ro = 0, rr = 0, force = rnd() % 3 ? 0 : rnd(), cmd = cmds[rnd() % 8], en = rnd() % 2, vinw;

		for (i = 0; i < IMGSZ; i++)
			img[i] = (u8)rnd();
		for (i = 0; i < 16; i++)
			script[i] = rnd();
		for (i = 0; i < 4; i++) {
			u8 *sd = img + O_SD(i), *o = img + O_OPS(i);

			t32_g_wr32(vin, T32_VIN_SENSOR(i), rnd() % 5 ? T32O_PTR(sd) : 0);
			t32_g_wr32(vin, T32_VIN_STATE(i), rnd() % 3 ? 3 + rnd() % 2 : rnd());
			t32_g_wr32(sd, T32_VSD_OPS, T32O_PTR(o));
			t32_g_wr32(o, 0, rnd() % 5 ? T32O_PTR(o + 0x10) : 0);
			t32_g_wr32(o, 4, rnd() % 5 ? T32O_PTR(o + 0x30) : 0);
			t32_g_wr32(o, 0x38, 0);
			t32_g_wr32(sd, T32_VSD_INFO_SENT, rnd() % 2 ? 0 : rnd());
			t32_g_wr32(sd, T32_VSD_INFO, T32O_PTR(img + O_INFO(i)));
			t32_g_wr32(img + O_INFO(i), T32_VSD_INFO_COUNT, rnd() % 4);
		}
		t32_g_wr32(vin, T32_VIN_ONESHOT, rnd() % 3);
		t32_g_wr32(vin, T32_VIN_MODE, rnd() % 4);
		/* sensor list: head -> ent0 -> .. -> head */
		{
			u8 *prev = vin + T32_VIN_LIST;

			for (i = 0; i < nent; i++) {
				t32_g_wr32(prev, 0, T32O_PTR(img + O_ENT(i) + T32_VSE_NODE));
				prev = img + O_ENT(i) + T32_VSE_NODE;
			}
			t32_g_wr32(prev, 0, T32O_PTR(vin + T32_VIN_LIST));
		}
		t32_g_wr32(req, 0, rnd() % 4 ? en : rnd());
		t32_g_wr32(req, 4, rnd() % 4);
		vinw = T32O_PTR(vin);
		if (op == 3 && cmd == T32_VIN_IOC_EVENT && rnd() % 5 == 0)
			vinw = rnd() % 3 == 0 ? 0 : rnd() % 2 ? 0xfffff001u : 0xfffff001u + rnd() % 0xfff;
		fns(0, 1);
		memcpy(save0, img, IMGSZ);
		for (side = 0; side < 2; side++) {
			memcpy(img, save0, IMGSZ);
			EV[side].n = 0;
			ncb = 0;
			fns(side, 0);
			if (!side) {
				switch (op) {
				case 0: ro = (u32)t32_vin_set_stream(&os, vin, (u32 *)req, force); break;
				case 1: ro = (u32)t32_vin_s_stream(&os, vin, (u32 *)req); break;
				case 2: ro = (u32)t32_vin_oneshot_allsensor_stream(&os, vin, en); break;
				default: ro = (u32)t32_vin_core_ops_ioctl(&os, T32O_HOST(vinw), cmd, (u32 *)req); break;
				}
			} else {
				switch (op) {
				case 0: rr = (u32)t32o_call(R(vin_set_stream), T32O_PTR(vin), T32O_PTR(req), force, 0, 0); break;
				case 1: rr = (u32)t32o_call(R(vin_s_stream), T32O_PTR(vin), T32O_PTR(req), rnd(), 0, 0); break;
				case 2: rr = (u32)t32o_call(R(vin_oneshot_allsensor_stream), T32O_PTR(vin), en, 0, 0, 0); break;
				default: rr = (u32)t32o_call(R(vin_core_ops_ioctl), vinw, cmd, T32O_PTR(req), 0, 0); break;
				}
			}
			fns(side, 1);
			if (!side)
				memcpy(save1, img, IMGSZ);
		}
		if (ro != rr)
			return fail("return value", c, op);
		if (memcmp(img, save1, IMGSZ)) {
			for (i = 0; i < IMGSZ && img[i] == save1[i]; i++)
				;
			printf("  first difference at image +0x%x\n", i);
			return fail("memory image", c, op);
		}
		if (EV[0].n != EV[1].n || memcmp(EV[0].e, EV[1].e, sizeof(EV[0].e)))
			return fail("call log", c, op);
		if (memcmp(img, save0, IMGSZ) || EV[0].n > 2)
			eff[op]++;
	}
	printf("t32_vin_oracle: %u cases, 0 mismatches; cases with an effect per op: %u %u %u %u\n", n,
	       eff[0], eff[1], eff[2], eff[3]);
	return 0;
}
