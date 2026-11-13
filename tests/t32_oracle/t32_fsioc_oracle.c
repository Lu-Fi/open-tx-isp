// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our frame-source core ioctl (driver/t32/tx_isp_t32_fs_ioctl.h)
 * against the unmodified vendor fs_core_ops_ioctl (Matt Davis' recovered C,
 * PR #15 @ 2e2e06bc).  A random image (fs device, 12 channels) runs through
 * ours, is restored and runs through the vendor; compared: every byte, the
 * return value and the ordered log of the hook calls (misc register /
 * deregister, spin lock with flags).  Not exercised (vendor oopses): a NULL /
 * ERR_PTR fs, a registration failure at channel 0.
 * Exit 0 = all equal, 1 = first mismatch.
 */
#include <stdio.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_fs_ioctl.h"

#define R(f)	((t32o_fn)(void (*)(void))(f))
u64 fs_core_ops_ioctl(u32, u32, u32, u32);

static u32 rng = 0x7f4a7c15u;
static u32 rnd(void)
{
	rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
	return rng;
}

#define MAXE 32
static struct { unsigned int n; u32 e[MAXE][3]; } EV[2];
static int side;
static u32 fail_at, fail_val, flags_val;
static void ev(u32 a, u32 b, u32 c)
{
	unsigned int n = EV[side].n++;

	if (n < MAXE) {
		EV[side].e[n][0] = a; EV[side].e[n][1] = b; EV[side].e[n][2] = c;
	}
}
static u8 *chans;
static int h_reg(u8 *ch) { ev(1, T32O_PTR(ch), 0); return (u32)(ch - chans) / 816 == fail_at ? (int)fail_val : 0; }
static void h_dereg(u8 *ch) { ev(2, T32O_PTR(ch), 0); }
static void h_lock(u8 *l, u32 *f) { ev(3, T32O_PTR(l), 0); *f = flags_val; }
static void h_unlock(u8 *l, u32 f) { ev(4, T32O_PTR(l), f); }
static const struct t32_fs_ioctl_os os = { h_reg, h_dereg, h_lock, h_unlock };

u64 tx_isp_frame_chan_misc_register(u32 c, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; return (u32)h_reg(T32O_HOST(c)); }
u64 private_misc_deregister(u32 c, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; h_dereg(T32O_HOST(c)); return 0; }
u64 __private_spin_lock_irqsave(u32 l, u32 f, u32 a2, u32 a3) { (void)a2; (void)a3; u32 v; h_lock(T32O_HOST(l), &v); t32_g_wr32(T32O_HOST(f), 0, v); return 0; }
u64 private_spin_unlock_irqrestore(u32 l, u32 f, u32 a2, u32 a3) { (void)a2; (void)a3; h_unlock(T32O_HOST(l), f); return 0; }

#define O_FS	0x0000u
#define O_CH	0x0200u		/* 12 x 816 */
#define O_ARG	0x2800u
#define IMGSZ	0x2840u
static u8 *img, save0[IMGSZ], save1[IMGSZ];

static int fail(const char *what, unsigned int c)
{
	printf("t32_fsioc_oracle: FAIL case %u: %s\n", c, what);
	return 1;
}

int main(void)
{
	static const u32 cmds[] = { 0x1000002u, 0x100000eu, 0x100000fu, 0x1000001u, 0x1000003u, 0x100000du, 0x1000010u, 0 };
	unsigned int c, i, n = 40000, eff = 0, nreg = 0, ndereg = 0;

	img = t32o_alloc(IMGSZ);
	chans = img + O_CH;
	for (c = 0; c < n; c++) {
		u8 *fs = img + O_FS, *arg = img + O_ARG;
		u32 cmd = rnd() % 4 ? cmds[rnd() % 3] : rnd() % 2 ? cmds[rnd() % 8] : rnd(), ro = 0, rr = 0, grp;

		for (i = 0; i < IMGSZ; i++)
			img[i] = (u8)rnd();
		grp = rnd() % 4 ? 1 + rnd() % 4 : rnd() % 2 ? 0 : rnd() % 2 ? 5 : rnd();
		t32_g_wr32(arg, 0, grp);
		t32_g_wr32(fs, T32_FS_CHANS, T32O_PTR(chans));
		fail_at = rnd() % 2 ? 99 : rnd() % 12;
		if (fail_at == 0)
			fail_at = 99;	/* the vendor runs below the array */
		fail_val = rnd() % 2 ? (u32)-12 : rnd() | 1;
		flags_val = rnd();
		memcpy(save0, img, IMGSZ);
		for (side = 0; side < 2; side++) {
			memcpy(img, save0, IMGSZ);
			EV[side].n = 0;
			memset(EV[side].e, 0, sizeof(EV[side].e));
			if (!side) {
				ro = (u32)t32_fs_core_ops_ioctl(&os, fs, cmd, arg);
				memcpy(save1, img, IMGSZ);
			} else {
				rr = (u32)t32o_call(R(fs_core_ops_ioctl), T32O_PTR(fs), cmd, T32O_PTR(arg), 0, 0);
			}
		}
		if (ro != rr)
			return fail("return value", c);
		if (memcmp(img, save1, IMGSZ))
			return fail("memory image", c);
		if (EV[0].n != EV[1].n || memcmp(EV[0].e, EV[1].e, sizeof(EV[0].e)))
			return fail("hook call log", c);
		if (memcmp(img, save0, IMGSZ))
			eff++;
		for (i = 0; i < EV[0].n && i < MAXE; i++) {
			nreg += EV[0].e[i][0] == 1;
			ndereg += EV[0].e[i][0] == 2;
		}
	}
	printf("t32_fsioc_oracle: %u cases, 0 mismatches; changed memory %u; misc register %u deregister %u\n", n, eff,
	       nreg, ndereg);
	return 0;
}
