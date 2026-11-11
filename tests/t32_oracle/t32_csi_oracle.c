// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our MIPI CSI glue (driver/t32/tx_isp_t32_csi.h) against the
 * unmodified vendor csi_core_ops_init, csi_sensor_ops_sync_sensor_attr (+ _part_4),
 * csi_phy_start, csi_video_s_stream and csi_sensor_ops_ioctl (Matt Davis'
 * recovered C, PR #15 @ 2e2e06bc).  A random image (CSI device, two register
 * blocks, attributes, info structs, argument blocks) runs through ours, is
 * restored and runs through the vendor; compared: every byte, the return
 * values and the ordered log of spinlock calls (address, flags), D-PHY start
 * calls (device, argument words) and isp_printf (level).
 * Exit 0 = all equal, 1 = first mismatch.
 */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_csi.h"

#define WEAK __attribute__((weak))
#define R(f)	((t32o_fn)(void (*)(void))(f))
WEAK char t32_data_c4710[4];

u64 csi_core_ops_init(u32, u32, u32, u32);
u64 csi_sensor_ops_sync_sensor_attr(u32, u32, u32, u32);
u64 csi_sensor_ops_sync_sensor_attr_part_4(u32, u32, u32, u32);
u64 csi_phy_start(u32, u32, u32, u32);
u64 csi_video_s_stream(u32, u32, u32, u32);
u64 csi_sensor_ops_ioctl(u32, u32, u32, u32);

static u32 rng = 0xc51c51c5u;
static u32 rnd(void)
{
	rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
	return rng;
}

#define MAXE 16
static struct { unsigned int n; u32 e[MAXE][4]; } EV[2];
static int side;
static u32 sc_flags;
static void ev(u32 a, u32 b, u32 c, u32 d)
{
	unsigned int n = EV[side].n++;

	if (n < MAXE) {
		EV[side].e[n][0] = a; EV[side].e[n][1] = b; EV[side].e[n][2] = c; EV[side].e[n][3] = d;
	}
}
static void o_lock(u8 *l, u32 *f) { ev(1, T32O_PTR(l), 0, 0); *f = sc_flags; }
static void o_unlock(u8 *l, u32 f) { ev(2, T32O_PTR(l), f, 0); }
static void o_phy(u8 *csi, u32 *a) { ev(3, T32O_PTR(csi), a[0], a[1]); }
static const struct t32_csi_os os = { .lock = o_lock, .unlock = o_unlock, .phy_stream_on = o_phy };
u64 __private_spin_lock_irqsave(u32 l, u32 f, u32 a2, u32 a3) { (void)a2; (void)a3; ev(1, l, 0, 0); *(u32 *)T32O_HOST(f) = sc_flags; return 0; }
u64 private_spin_unlock_irqrestore(u32 l, u32 f, u32 a2, u32 a3) { (void)a2; (void)a3; ev(2, l, f, 0); return 0; }
u64 mipi_phy_stream_on_constprop_5(u32 csi, u32 a, u32 a2, u32 a3)
{
	const u32 *w = T32O_HOST(a);

	(void)a2; (void)a3;
	ev(3, csi, w[0], w[1]);
	return 0x5a5a;
}
int isp_printf(unsigned int level, const char *fmt, ...)
{
	(void)fmt;
	ev(4, level, 0, 0);
	return 0;
}

#define O_CSI	0x0000u		/* 0x400 */
#define O_REG0	0x0400u
#define O_REG1	0x0440u
#define O_ATTR	0x0480u		/* 4 x 0x20 */
#define O_INFO	0x0500u		/* 0x80 */
#define O_ARG	0x0580u		/* 108 B */
#define O_REQ	0x0600u
#define IMGSZ	0x0640u
static u8 *img, save0[IMGSZ], save1[IMGSZ];

static int fail(const char *what, unsigned int c, unsigned int op)
{
	printf("t32_csi_oracle: FAIL case %u op %u: %s\n", c, op, what);
	return 1;
}

int main(void)
{
	static const u32 cmds[] = { T32_CSI_IOC_PHY_START, T32_CSI_IOC_CFG, 0x200000du, 0x200001bu, 0 };
	unsigned int c, i, n = 30000, eff[6] = { 0 };

	img = t32o_alloc(IMGSZ);
	for (c = 0; c < n; c++) {
		unsigned int op = c % 6;
		u8 *csi = img + O_CSI, *req = img + O_REQ, *arg = img + O_ARG;
		u32 csiw = T32O_PTR(csi), ro = 0, rr = 0, cmd = cmds[rnd() % 5], idx = rnd() % 4, argw = T32O_PTR(arg);

		for (i = 0; i < IMGSZ; i++)
			img[i] = (u8)rnd();
		sc_flags = rnd();
		switch (rnd() % 8) {
		case 0: csiw = 0; break;
		case 1: csiw = rnd() % 2 ? 0xfffff001u : 0xfffff001u + rnd() % 0xfff; break;
		default: break;
		}
		t32_g_wr32(csi, T32_CSI_BASE, T32O_PTR(img + O_REG0));
		t32_g_wr32(csi, T32_CSI_BASE2, T32O_PTR(img + O_REG1));
		for (i = 0; i < 4; i++) {
			t32_g_wr32(csi, T32_CSI_VIN(i) + T32_CSI_VIN_ATTR, T32O_PTR(img + O_ATTR + i * 0x20));
			t32_g_wr32(img + O_ATTR + i * 0x20, T32_CSI_ATTR_DBUS, rnd() % 3 ? 1 : rnd() % 3);
		}
		t32_g_wr32(csi, T32_CSI_COUNT, rnd() % 3 ? rnd() % 3 : rnd());
		t32_g_wr32(csi, T32_CSI_PHY_MODE, rnd() % 4 ? 1 + rnd() % 2 : rnd() % 4);
		t32_g_wr32(csi, T32_CSI_CFG, rnd() % 4);
		t32_g_wr32(req, 0, rnd() % 2 ? 0 : rnd() % 2 ? 1 : rnd());
		t32_g_wr32(req, 4, idx);
		t32_g_wr32(arg, 0, rnd() % 4);	/* ioctl PHY start: sensor index */
		t32_g_wr32(arg, T32_CSI_INFO_INDEX - 92 + T32_CSI_VIN_INFO, T32O_PTR(img + O_INFO));
		t32_g_wr16(img + O_INFO, T32_CSI_INFO_INDEX, (u16)(rnd() % 4));
		if (op == 1 && rnd() % 6 == 0)
			argw = 0;
		memcpy(save0, img, IMGSZ);
		for (side = 0; side < 2; side++) {
			memcpy(img, save0, IMGSZ);
			EV[side].n = 0;
			if (!side) {
				u8 *s = T32O_HOST(csiw);

				switch (op) {
				case 0: ro = (u32)t32_csi_core_ops_init(&os, s, (u32 *)req); break;
				case 1: ro = (u32)t32_csi_sync_sensor_attr(s, T32O_HOST(argw)); break;
				case 2: ro = (u32)t32_csi_phy_start(&os, csi, idx); break;
				case 3: ro = (u32)t32_csi_video_s_stream(&os, s, (u32 *)req); break;
				case 4: ro = (u32)t32_csi_sensor_ops_ioctl(&os, s, cmd, (u32 *)arg); break;
				default: ro = (u32)t32_csi_bad_subdev(); break;
				}
				memcpy(save1, img, IMGSZ);
			} else {
				switch (op) {
				case 0: rr = (u32)t32o_call(R(csi_core_ops_init), csiw, T32O_PTR(req), 0, 0, 0); break;
				case 1: rr = (u32)t32o_call(R(csi_sensor_ops_sync_sensor_attr), csiw, argw, 0, 0, 0); break;
				case 2: rr = (u32)t32o_call(R(csi_phy_start), T32O_PTR(csi), idx, 0, 0, 0); break;
				case 3: rr = (u32)t32o_call(R(csi_video_s_stream), csiw, T32O_PTR(req), 0, 0, 0); break;
				case 4: rr = (u32)t32o_call(R(csi_sensor_ops_ioctl), csiw, cmd, T32O_PTR(arg), 0, 0); break;
				default: rr = (u32)t32o_call(R(csi_sensor_ops_sync_sensor_attr_part_4), rnd(), rnd(), 0, 0, 0); break;
				}
			}
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
		if (memcmp(img, save0, IMGSZ) || EV[0].n)
			eff[op]++;
	}
	printf("t32_csi_oracle: %u cases, 0 mismatches; cases with an effect per op: %u %u %u %u %u %u\n", n,
	       eff[0], eff[1], eff[2], eff[3], eff[4], eff[5]);
	return 0;
}
