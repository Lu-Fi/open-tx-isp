// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our frame channel ioctl dispatcher
 * (driver/t32/tx_isp_t32_fch_ioctl.h) against the unmodified vendor
 * frame_channel_unlocked_ioctl (Matt Davis' recovered C, PR #15 @ 2e2e06bc).
 * Random commands (all handled ones, neighbours, random), kernel channel
 * indexes and files, user / kernel callers, module flags and tables, and
 * scripted hook answers run through ours, are restored and run through the
 * vendor; compared: every byte, the module tables, the return value and the
 * ordered hook log (the seven handlers with channel and argument, user
 * copies with the copied-out bytes, fill, event with its word, semaphore /
 * completion waits, isp_printf with all arguments).  Exit 0 = all equal.
 */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_fch_ioctl.h"

#define R(f)	((t32o_fn)(void (*)(void))(f))
char t32_data_be558[] = "frame_channel_unlocked_ioctl";
char t32_data_c5fcc[] = "The parameter from user is invalid!\n";
char t32_data_c5f08[] = "Failed to copy from user\n";
char t32_data_c6230[] = "dqbuf: invalid buffer type\n";
char t32_data_c5f98[] = "Failed to copy to user\n";
char t32_data_c68b4[] = "querybuf: buffer index out of range\n";
char t32_data_c60b8[] = "streamon: driver refused to start streaming\n";
char t32_data_c68dc[] = "[ %s:%d ] the frame ioctl cmd(%d) couldn't be handled!\n";
u32 t32_data_d9008, t32_data_d9004, t32_data_d9950;
u32 t32_data_f37d4[12], t32_data_f37a4[12], t32_data_f3774[12], t32_data_d9ae4[12];

u64 frame_channel_unlocked_ioctl(u32, u32, u32, u32);

static u32 rng = 0x27d4eb2fu;
static u32 rnd(void)
{
	rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
	return rng;
}

#define MAXE 12
static struct { unsigned int n; u32 e[MAXE][8]; } EV[2];
static int side;
static u32 r_h, r_cfu, r_ctu, r_ev, r_wait, ub[24];
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
static int hd(u32 k, u32 ch, u32 arg) { ev(10 + k, ch, arg, 0); return (int)r_h; }
static int h_set_fmt(u8 *ch, u32 arg) { return hd(0, T32O_PTR(ch), arg); }
u64 frame_channel_vidioc_set_fmt(u32 ch, u32 arg, u32 a2, u32 a3) { (void)a2; (void)a3; return (u32)hd(0, ch, arg); }
static int h_get_fmt(u8 *ch, u32 arg) { return hd(1, T32O_PTR(ch), arg); }
u64 frame_channel_vidioc_get_fmt(u32 ch, u32 arg, u32 a2, u32 a3) { (void)a2; (void)a3; return (u32)hd(1, ch, arg); }
static int h_reqbufs(u8 *ch, u32 arg) { return hd(2, T32O_PTR(ch), arg); }
u64 frame_channel_reqbufs(u32 ch, u32 arg, u32 a2, u32 a3) { (void)a2; (void)a3; return (u32)hd(2, ch, arg); }
static int h_qbuf(u8 *ch, u32 arg) { return hd(3, T32O_PTR(ch), arg); }
u64 frame_channel_vb2_qbuf(u32 ch, u32 arg, u32 a2, u32 a3) { (void)a2; (void)a3; return (u32)hd(3, ch, arg); }
static int h_dqbuf(u8 *ch, u32 arg) { return hd(4, T32O_PTR(ch), arg); }
u64 frame_channel_vb2_dqbuf(u32 ch, u32 arg, u32 a2, u32 a3) { (void)a2; (void)a3; return (u32)hd(4, ch, arg); }
static int h_streamon(u8 *ch, u32 arg) { return hd(5, T32O_PTR(ch), arg); }
u64 frame_channel_vb2_streamon(u32 ch, u32 arg, u32 a2, u32 a3) { (void)a2; (void)a3; return (u32)hd(5, ch, arg); }
static int h_streamoff(u8 *ch, u32 arg) { return hd(6, T32O_PTR(ch), arg); }
u64 frame_channel_vb2_streamoff(u32 ch, u32 arg, u32 a2, u32 a3) { (void)a2; (void)a3; return (u32)hd(6, ch, arg); }
static u32 h_cfu(u8 *dst, u32 user, u32 n) { ev(1, user, n, 0); memcpy(dst, ub, n); return r_cfu; }
static u32 h_ctu(u32 user, const u8 *src, u32 n) { ev(2, user, n, hash(src, n)); return r_ctu; }
static void h_fill(u32 vb, u8 *buf) { ev(3, vb, hash(buf, T32_FIO_BUF_BYTES), 0); t32_g_wr32(buf, 8 + vb % 64, vb); }
static int h_ev(u32 pad, u32 cmd, u8 *d) { ev(4, pad, cmd, t32_g_rd32(d, 0)); return (int)r_ev; }
static int h_down(u8 *s, u32 j) { ev(5, T32O_PTR(s), j, 0); return (int)r_wait; }
static int h_wc(u8 *c) { ev(6, T32O_PTR(c), 0, 0); return (int)r_wait; }
static const struct t32_fio_os os = { h_set_fmt, h_get_fmt, h_reqbufs, h_qbuf, h_dqbuf, h_streamon, h_streamoff,
				      h_cfu, h_ctu, h_fill, h_ev, h_down, h_wc,
				      &t32_data_d9008, &t32_data_d9004, &t32_data_d9950,
				      t32_data_f37d4, t32_data_f37a4, t32_data_f3774, t32_data_d9ae4 };
u64 private_copy_from_user(u32 d, u32 u, u32 n, u32 a3) { (void)a3; return h_cfu(T32O_HOST(d), u, n); }
u64 private_copy_to_user(u32 u, u32 s, u32 n, u32 a3) { (void)a3; return h_ctu(u, T32O_HOST(s), n); }
u64 __fill_tisp_buffer(u32 vb, u32 buf, u32 a2, u32 a3) { (void)a2; (void)a3; h_fill(vb, T32O_HOST(buf)); return 0; }
u64 tx_isp_send_event_to_remote(u32 pad, u32 cmd, u32 d, u32 a3) { (void)a3; return (u32)h_ev(pad, cmd, T32O_HOST(d)); }
u64 down_timeout(u32 s, u32 j, u32 a2, u32 a3) { (void)a2; (void)a3; return (u32)h_down(T32O_HOST(s), j); }
u64 private_wait_for_completion_interruptible(u32 c, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; return (u32)h_wc(T32O_HOST(c)); }

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
			f++;
			if (*f == 's') {
				const char *s = (const char *)T32O_HOST((u32)va_arg(ap, unsigned long));

				EV[side].e[n][k++] = hash((const u8 *)s, (u32)strlen(s));
			} else {
				EV[side].e[n][k++] = va_arg(ap, unsigned int);
			}
		}
	}
	va_end(ap);
	return 0;
}

#define O_CORE	0x0000u
#define O_FILE	0x0100u
#define O_CH	0x0200u		/* 3 x 816 */
#define O_UBUF	0x0e00u
#define IMGSZ	0x0f00u
static u8 *img, save0[IMGSZ], save1[IMGSZ];
static u32 tab0[4][12], tab1[4][12];
static u32 *const tabs[4] = { t32_data_f37d4, t32_data_f37a4, t32_data_f3774, t32_data_d9ae4 };

static int fail(const char *what, unsigned int c)
{
	printf("t32_fchio_oracle: FAIL case %u: %s\n", c, what);
	return 1;
}

int main(void)
{
	static const u32 cmds[12] = { T32_FIO_S_FMT, T32_FIO_G_FMT, T32_FIO_REQBUFS, T32_FIO_QUERYBUF, T32_FIO_QBUF,
				      T32_FIO_DQBUF, T32_FIO_STREAMON, T32_FIO_STREAMOFF, T32_FIO_SET_SRC, T32_FIO_WAIT,
				      0xc0045482u, 0xc060548du };
	unsigned int c, i, j, n = 60000, hit[4] = { 0 };

	img = t32o_alloc(IMGSZ);
	for (c = 0; c < n; c++) {
		u32 ci = rnd() % 3, cmd = rnd() % 16 ? cmds[rnd() % 12] : rnd() % 2 ? rnd() : 0xc0045480u + rnd() % 16;
		u8 *ch = img + O_CH + ci * T32_FIO_CH_BYTES;
		u32 file = rnd() % 2 ? ci : T32O_PTR(img + O_FILE), arg = T32O_PTR(img + O_UBUF), ro = 0, rr = 0;

		for (i = 0; i < IMGSZ; i++)
			img[i] = (u8)rnd();
		t32_data_d9950 = T32O_PTR(img + O_CORE);
		t32_g_wr32(img + O_CORE, T32_FIO_CORE_CHANS, T32O_PTR(img + O_CH));
		t32_g_wr32(img + O_FILE, T32_FIO_FILE_PRIV, T32O_PTR(ch));
		t32_g_wr32(ch, T32_FIO_INDEX, rnd() % 12);
		t32_g_wr32(ch, T32_FIO_NBUFS, rnd() % 6);
		if (file >= 256)
			t32_g_wr32(ch, T32_FIO_KERNEL, rnd() % 2);
		for (i = 0; i < 24; i++)
			ub[i] = rnd();
		ub[0] = rnd() % 4 ? rnd() % 8 : rnd();
		if (rnd() % 8)
			ub[1] = t32_g_rd32(ch, T32_FIO_TYPE);
		for (i = 0; i < 4; i++)
			for (j = 0; j < 12; j++)
				tab0[i][j] = i < 3 ? (rnd() % 4 ? rnd() % 2 : rnd()) : rnd() % 3;
		t32_data_d9008 = rnd() % 2 ? 0 : rnd() % 2 ? 1 : rnd();
		t32_data_d9004 = rnd() % 2 ? 0 : rnd() % 2 ? 1 : rnd();
		r_h = rnd(); r_ev = rnd() % 3 == 0 ? 0 : rnd() % 2 ? (u32)-515 : rnd();
		r_cfu = rnd() % 8 ? 0 : 1 + rnd() % 96;
		r_ctu = rnd() % 8 ? 0 : 1 + rnd() % 96;
		r_wait = rnd() % 3 ? 0 : rnd();
		if (rnd() % 16 == 0 && cmd != T32_FIO_WAIT)	/* the wait does not check its argument */
			arg = rnd() % 2 ? 0 : (u32)-1 - rnd() % 4095;
		memcpy(save0, img, IMGSZ);
		for (side = 0; side < 2; side++) {
			memcpy(img, save0, IMGSZ);
			for (i = 0; i < 4; i++)
				memcpy(tabs[i], tab0[i], sizeof(tab0[i]));
			EV[side].n = 0;
			memset(EV[side].e, 0, sizeof(EV[side].e));
			if (!side) {
				ro = (u32)t32_frame_channel_unlocked_ioctl(&os, file, cmd, arg);
				memcpy(save1, img, IMGSZ);
				for (i = 0; i < 4; i++)
					memcpy(tab1[i], tabs[i], sizeof(tab1[i]));
			} else {
				rr = (u32)t32o_call(R(frame_channel_unlocked_ioctl), file, cmd, arg, 0, 0);
			}
		}
		if (ro != rr)
			return fail("return value", c);
		if (memcmp(img, save1, IMGSZ))
			return fail("memory image", c);
		for (i = 0; i < 4; i++)
			if (memcmp(tabs[i], tab1[i], sizeof(tab1[i])))
				return fail("module tables", c);
		if (EV[0].n != EV[1].n || memcmp(EV[0].e, EV[1].e, sizeof(EV[0].e)))
			return fail("hook call log", c);
		hit[ro == 0 ? 0 : ro == (u32)-22 ? 1 : ro == (u32)-515 ? 2 : 3]++;
	}
	printf("t32_fchio_oracle: %u cases, 0 mismatches; 0 / -22 / -515 / other: %u %u %u %u\n",
	       n, hit[0], hit[1], hit[2], hit[3]);
	return 0;
}
