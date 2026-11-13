// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our tx-isp core glue (driver/t32/tx_isp_t32_txcore6.h)
 * against the unmodified vendor sensor_event_{prepare,suspend,resume,complete,
 * streamoff}_process and subdev_video_destroy_link (Matt Davis' recovered C,
 * PR #15 @ 2e2e06bc).  A random image (subdev, links, pads, decoy bytes)
 * runs through ours, is restored and runs through the vendor; compared: every
 * byte, the return values, every notify call (sd, cmd, the 3 event words)
 * with scripted answers (failing at a chosen call) and every isp_printf
 * (level, line).  Exit 0 = all equal, 1 = first mismatch.
 */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_txcore6.h"

#define WEAK __attribute__((weak))
#define R(f)	((t32o_fn)(void (*)(void))(f))
WEAK char t32_data_c3598[4], t32_data_c35b8[4], t32_data_c35d8[4], t32_data_c35f4[4], t32_data_c3614[4], t32_data_ccc60[4], t32_data_ccc80[4];

u64 sensor_event_prepare_process(u32, u32, u32, u32);
u64 sensor_event_suspend_process(u32, u32, u32, u32);
u64 sensor_event_resume_process(u32, u32, u32, u32);
u64 sensor_event_complete_process(u32, u32, u32, u32);
u64 sensor_event_streamoff_process(u32, u32, u32, u32);
u64 subdev_video_destroy_link(u32, u32, u32, u32);

static u32 rng = 0x0badf00du;
static u32 rnd(void)
{
	rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
	return rng;
}

#define MAXE 32
static struct { unsigned int n; u32 e[MAXE][5]; } EV[2];
static int side;
static u32 fail_at, fail_ret;
static void ev(u32 a, u32 b, u32 c, u32 d, u32 e)
{
	unsigned int n = EV[side].n++;

	if (n < MAXE) {
		EV[side].e[n][0] = a; EV[side].e[n][1] = b; EV[side].e[n][2] = c; EV[side].e[n][3] = d; EV[side].e[n][4] = e;
	}
}
static unsigned int ncall;
static int nt_common(u32 sd, u32 cmd, const u32 *w)
{
	ev(sd, cmd, w[0], w[1], w[2]);
	return ++ncall == fail_at ? (int)fail_ret : 0;
}
static int nt_ours(u8 *sd, u32 cmd, void *arg) { return nt_common(T32O_PTR(sd), cmd, arg); }
static u64 nt_ref(u32 sd, u32 cmd, u32 arg, u32 a3) { (void)a3; return (u32)nt_common(sd, cmd, T32O_HOST(arg)); }

int isp_printf(unsigned int level, const char *fmt, ...)
{
	va_list ap;
	unsigned long a3;

	(void)fmt;
	va_start(ap, fmt);
	(void)va_arg(ap, unsigned long);
	a3 = va_arg(ap, unsigned long);
	va_end(ap);
	ev(0xfeed, level, (u32)a3, 0, 0);
	return 0;
}

#define O_SD	0x000u
#define O_LINK	0x200u		/* link, reverse link */
#define O_PAD	0x300u		/* source pad, sink pad (16 B each) */
#define IMGSZ	0x400u
static u8 *img, save0[IMGSZ], save1[IMGSZ];

static int fail(const char *what, unsigned int c, unsigned int op)
{
	printf("t32_txcore6_oracle: FAIL case %u op %u: %s\n", c, op, what);
	return 1;
}

int main(void)
{
	static u64 (*const evf[5])(u32, u32, u32, u32) = { sensor_event_prepare_process, sensor_event_suspend_process,
		sensor_event_resume_process, sensor_event_complete_process, sensor_event_streamoff_process };
	unsigned int c, i, n = 40000, eff[6] = { 0 };

	img = t32o_alloc(IMGSZ);
	for (c = 0; c < n; c++) {
		unsigned int op = c % 6;
		u32 arg = rnd(), sdw, nn = rnd() % 5, ro = 0, rr = 0, nt_on = rnd() % 5 != 0;
		u8 *link = img + O_LINK;

		for (i = 0; i < IMGSZ; i++)
			img[i] = (u8)rnd();
		sdw = rnd() % 6 ? T32O_PTR(img + O_SD) : 0;
		fail_at = rnd() % 6;	/* 0 = never */
		fail_ret = rnd() % 2 ? 1 : 0xffffffeau;
		if (rnd() % 4 == 0)
			t32_g_wr32(link, T32_LINK_FLAGS, 0);
		t32_g_wr32(link, T32_LINK_SOURCE, T32O_PTR(img + O_PAD));
		t32_g_wr32(link, T32_LINK_SINK, rnd() % 3 ? T32O_PTR(img + O_PAD + 16) : 0);
		t32_g_wr32(link, T32_LINK_REVERSE, rnd() % 3 ? T32O_PTR(link + 16) : 0);
		t32_g_wr32(img + O_SD, T32_G_SD_NOTIFY, 0);
		memcpy(save0, img, IMGSZ);
		for (side = 0; side < 2; side++) {
			memcpy(img, save0, IMGSZ);
			EV[side].n = 0;
			ncall = 0;
			t32_g_wr32(img + O_SD, T32_G_SD_NOTIFY, nt_on ? (side ? T32O_PTR(nt_ref) : T32O_PTR(nt_ours)) : 0);
			if (!side) {
				u8 *sd = T32O_HOST(sdw);

				switch (op) {
				case 0: ro = (u32)t32_sensor_event_prepare_process(arg, sd); break;
				case 1: ro = (u32)t32_sensor_event_suspend_process(arg, sd); break;
				case 2: ro = (u32)t32_sensor_event_resume_process(arg, sd); break;
				case 3: ro = (u32)t32_sensor_event_complete_process(arg, sd); break;
				case 4: ro = (u32)t32_sensor_event_streamoff_process(arg, sd, rnd(), nn); break;
				default: ro = (u32)t32_subdev_video_destroy_link(link); break;
				}
			} else if (op < 5) {
				rr = (u32)t32o_call(R(evf[op]), arg, sdw, rnd(), nn, 0);
			} else {
				rr = (u32)t32o_call(R(subdev_video_destroy_link), T32O_PTR(link), 0, 0, 0, 0);
			}
			t32_g_wr32(img + O_SD, T32_G_SD_NOTIFY, 0);
			if (!side)
				memcpy(save1, img, IMGSZ);
		}
		if (ro != rr)
			return fail("return value", c, op);
		if (memcmp(img, save1, IMGSZ))
			return fail("memory image", c, op);
		if (EV[0].n != EV[1].n || memcmp(EV[0].e, EV[1].e, sizeof(EV[0].e)))
			return fail("notify / printf log", c, op);
		if (EV[0].n || memcmp(img, save0, IMGSZ))
			eff[op]++;
	}
	printf("t32_txcore6_oracle: %u cases, 0 mismatches; cases with an effect per op: %u %u %u %u %u %u\n", n,
	       eff[0], eff[1], eff[2], eff[3], eff[4], eff[5]);
	return 0;
}
