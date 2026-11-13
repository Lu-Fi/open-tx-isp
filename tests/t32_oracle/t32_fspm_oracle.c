// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our frame-source module glue (driver/t32/tx_isp_t32_fs_pm.h)
 * against the unmodified vendor fs_activate_module, fs_complete_module,
 * fs_suspend_module, fs_slake_module and frame_channel_unlocked_ioctl_first
 * (Matt Davis' recovered C, PR #15 @ 2e2e06bc).  A random image (fs device,
 * up to 5 channels of 816 bytes) runs through ours, is restored and runs
 * through the vendor; compared: every byte, the return values and the
 * ordered log of the hook calls (stream-off, queue free, misc deregister,
 * unlocked_ioctl, isp_printf level / channel / line).
 * Exit 0 = all equal, 1 = first mismatch.
 */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_fs_pm.h"

#define WEAK __attribute__((weak))
#define R(f)	((t32o_fn)(void (*)(void))(f))
WEAK char t32_data_be4dc[4], t32_data_be518[4], t32_data_c5eac[4], t32_data_c6318[4], t32_data_c6330[4], t32_data_c6894[4];

u64 fs_activate_module(u32, u32, u32, u32);
u64 fs_complete_module(u32, u32, u32, u32);
u64 fs_suspend_module(u32, u32, u32, u32);
u64 fs_slake_module(u32, u32, u32, u32);
u64 frame_channel_unlocked_ioctl_first(u32, u32, u32, u32);

static u32 rng = 0xf5f5a5a5u;
static u32 rnd(void)
{
	rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
	return rng;
}

#define MAXE 24
static struct { unsigned int n; u32 e[MAXE][4]; } EV[2];
static int side;
static u32 sc_ret;
static void ev(u32 a, u32 b, u32 c, u32 d)
{
	unsigned int n = EV[side].n++;

	if (n < MAXE) {
		EV[side].e[n][0] = a; EV[side].e[n][1] = b; EV[side].e[n][2] = c; EV[side].e[n][3] = d;
	}
}
static void o_soff(u8 *ch, u32 q) { ev(1, T32O_PTR(ch), q, 0); }
static void o_qfree(u8 *q, u32 m) { ev(2, T32O_PTR(q), m, 0); }
static void o_mdereg(u8 *ch) { ev(3, T32O_PTR(ch), 0, 0); }
static long o_ioctl(u32 f, u32 c, u32 a) { ev(4, f, c, a); return (long)(int)sc_ret; }
static const struct t32_fs_os os = { .streamoff = o_soff, .queue_free = o_qfree, .misc_deregister = o_mdereg,
				     .unlocked_ioctl = o_ioctl };
u64 __frame_channel_vb2_streamoff(u32 ch, u32 q, u32 a2, u32 a3) { (void)a2; (void)a3; ev(1, ch, q, 0); return 0; }
u64 __vb2_queue_free(u32 q, u32 m, u32 a2, u32 a3) { (void)a2; (void)a3; ev(2, q, m, 0); return 0; }
u64 private_misc_deregister(u32 ch, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; ev(3, ch, 0, 0); return 0; }
u64 frame_channel_unlocked_ioctl(u32 f, u32 c, u32 a, u32 a3) { (void)a3; ev(4, f, c, a); return sc_ret; }
int isp_printf(unsigned int level, const char *fmt, ...)
{
	va_list ap;
	unsigned long a2, a3;
	int chan = side ? fmt == t32_data_c5eac : !strncmp(fmt, "framesource: channel", 20);

	va_start(ap, fmt);
	a2 = va_arg(ap, unsigned long);
	a3 = va_arg(ap, unsigned long);
	va_end(ap);
	ev(5, level, chan ? (u32)a2 : 0, (u32)a3);
	return (int)(sc_ret ^ 0x55);
}

#define O_FS	0x0000u
#define O_CH	0x0200u		/* 5 x 816 */
#define IMGSZ	(0x0200u + 5 * 816u)
static u8 *img, save0[IMGSZ], save1[IMGSZ];

static int fail(const char *what, unsigned int c, unsigned int op)
{
	printf("t32_fspm_oracle: FAIL case %u op %u: %s\n", c, op, what);
	return 1;
}

int main(void)
{
	unsigned int c, i, n = 40000, eff[5] = { 0 };

	img = t32o_alloc(IMGSZ);
	for (c = 0; c < n; c++) {
		unsigned int op = c % 5;
		u8 *fs = img + O_FS;
		u32 fsw = T32O_PTR(fs), ro = 0, rr = 0, a[3] = { rnd(), rnd(), rnd() };

		for (i = 0; i < IMGSZ; i++)
			img[i] = (u8)rnd();
		sc_ret = rnd();
		switch (rnd() % 8) {
		case 0: fsw = 0; break;
		case 1: fsw = rnd() % 2 ? 0xfffff001u : 0xfffff001u + rnd() % 0xfff; break;
		default: break;
		}
		t32_g_wr32(fs, T32_FS_CHANS, T32O_PTR(img + O_CH));
		t32_g_wr32(fs, T32_FS_NCHAN, rnd() % 8 == 0 ? 0x80000000u + rnd() % 4 : rnd() % 6);
		t32_g_wr32(fs, T32_FS_STATE, rnd() % 4 ? 1 + rnd() % 2 : rnd() % 2 ? 0 : rnd());
		for (i = 0; i < 5; i++) {
			u8 *ch = img + O_CH + i * 816;

			t32_g_wr32(ch, T32_FCH_STATE, rnd() % 5 ? (op == 0 ? 1 : 1 + rnd() % 4) : rnd());
			if (rnd() % 3 == 0)
				t32_g_wr32(ch, T32_FCH_MISC, 0);
		}
		memcpy(save0, img, IMGSZ);
		for (side = 0; side < 2; side++) {
			memcpy(img, save0, IMGSZ);
			EV[side].n = 0;
			if (!side) {
				u8 *f = T32O_HOST(fsw);

				switch (op) {
				case 0: ro = (u32)t32_fs_activate_module(f); break;
				case 1: ro = (u32)t32_fs_complete_module(f); break;
				case 2: ro = (u32)t32_fs_suspend_module(f); break;
				case 3: ro = (u32)t32_fs_slake_module(&os, f); break;
				default: ro = (u32)t32_frame_channel_unlocked_ioctl_first(&os, a[0], a[1], a[2]); break;
				}
				memcpy(save1, img, IMGSZ);
			} else {
				switch (op) {
				case 0: rr = (u32)t32o_call(R(fs_activate_module), fsw, 0, 0, 0, 0); break;
				case 1: rr = (u32)t32o_call(R(fs_complete_module), fsw, 0, 0, 0, 0); break;
				case 2: rr = (u32)t32o_call(R(fs_suspend_module), fsw, 0, 0, 0, 0); break;
				case 3: rr = (u32)t32o_call(R(fs_slake_module), fsw, 0, 0, 0, 0); break;
				default: rr = (u32)t32o_call(R(frame_channel_unlocked_ioctl_first), a[0], a[1], a[2], 0, 0); break;
				}
			}
		}
		if (ro != rr)
			return fail("return value", c, op);
		if (memcmp(img, save1, IMGSZ))
			return fail("memory image", c, op);
		if (EV[0].n != EV[1].n || memcmp(EV[0].e, EV[1].e, sizeof(EV[0].e)))
			return fail("hook call log", c, op);
		if (memcmp(img, save0, IMGSZ) || EV[0].n)
			eff[op]++;
	}
	printf("t32_fspm_oracle: %u cases, 0 mismatches; cases with an effect per op: %u %u %u %u %u\n", n,
	       eff[0], eff[1], eff[2], eff[3], eff[4]);
	return 0;
}
