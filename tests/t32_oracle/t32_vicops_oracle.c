// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our VIC subdev glue (driver/t32/tx_isp_t32_vicops.h) against
 * the unmodified vendor vic_core_ops_init, vic_sensor_ops_ioctl,
 * vic_sensor_ops_sync_sensor_attr (+ _part_2) and vic_mdma_irq_function (Matt
 * Davis' recovered C, PR #15 @ 2e2e06bc).  A random image (subdev, VIC device,
 * registers, info structs, arguments) and the MDMA globals run through ours,
 * are restored and run through the vendor; compared: every byte, the globals,
 * the return values and the ordered log of the hook calls (index_cal,
 * lib_tisp_vic_init with its 4 config words, deinit, cfg, stop, reset,
 * tx_isp_vic_start, private_complete (env.c counter), isp_printf level/line/value) with
 * scripted answers.  ioctl 0x200001d (vendor NULL dereference) is not run.
 * Exit 0 = all equal, 1 = first mismatch.
 */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_vicops.h"

#define WEAK __attribute__((weak))
#define R(f)	((t32o_fn)(void (*)(void))(f))
WEAK char t32_data_c39a0[4], t32_data_bd7b4[4], t32_data_c3e1c[4];
WEAK u32 t32_data_d9310, t32_data_d9318, t32_data_d9304, t32_data_d930c;
/* data.c:459.. (bd6a0): jump table of vic_sensor_ops_ioctl, MIPS code addresses of its cases */
WEAK u32 t32_data_bd6a0[18] = { 0x2810, 0x2910, 0x27d8, 0x27ec, 0x2910, 0x2910, 0x2910, 0x2868, 0x2910,
				0x2910, 0x2910, 0x2910, 0x2910, 0x2910, 0x2910, 0x2910, 0x289c, 0x28e8 };

u64 vic_core_ops_init(u32, u32, u32, u32);
u64 vic_sensor_ops_ioctl(u32, u32, u32, u32);
u64 vic_sensor_ops_sync_sensor_attr(u32, u32, u32, u32);
u64 vic_sensor_ops_sync_sensor_attr_part_2(u32, u32, u32, u32);
u64 vic_mdma_irq_function(u32, u32, u32, u32);

static u32 rng = 0x71c71c71u;
static u32 rnd(void)
{
	rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
	return rng;
}

#define MAXE 16
static struct { unsigned int n; u32 e[MAXE][6]; } EV[2];
static int side;
static u32 sc_k, sc_start[2];
static unsigned int nstart;
static void ev(u32 a, u32 b, u32 c, u32 d, u32 e, u32 f)
{
	unsigned int n = EV[side].n++;

	if (n < MAXE) {
		EV[side].e[n][0] = a; EV[side].e[n][1] = b; EV[side].e[n][2] = c;
		EV[side].e[n][3] = d; EV[side].e[n][4] = e; EV[side].e[n][5] = f;
	}
}
static u32 o_ical(u8 *v, u32 i) { ev(1, T32O_PTR(v), i, 0, 0, 0); return sc_k; }
static void o_init(u32 k, u32 a, const u32 *c) { ev(2, k, a, c[0] ^ c[1], c[2], c[3]); }
static void o_deinit(void) { ev(3, 0, 0, 0, 0, 0); }
static void o_cfg(u32 k, u32 a) { ev(4, k, a, 0, 0, 0); }
static void o_stop(void) { ev(5, 0, 0, 0, 0, 0); }
static void o_reset(void) { ev(6, 0, 0, 0, 0, 0); }
static int o_start(u8 *v, u32 m) { ev(7, T32O_PTR(v), m, 0, 0, 0); return (int)sc_start[nstart++ & 1]; }
/* the completion is the last call of the MDMA path: counted like env.c's private_complete, logged after the call */
static void o_complete(u8 *c) { t32o_complete_n++; t32o_complete_arg = T32O_PTR(c); }
static const struct t32_vicops_os os = { .index_cal = o_ical, .lib_init = o_init, .lib_deinit = o_deinit,
	.lib_cfg = o_cfg, .lib_stop = o_stop, .lib_reset = o_reset, .vic_start = o_start, .complete = o_complete };
u64 tx_isp_vic_index_cal(u32 v, u32 i, u32 a2, u32 a3) { (void)a2; (void)a3; ev(1, v, i, 0, 0, 0); return sc_k; }
u64 lib_tisp_vic_init(u32 k, u32 a, u32 c, u32 a3) { const u32 *w = T32O_HOST(c); (void)a3; ev(2, k, a, w[0] ^ w[1], w[2], w[3]); return 0; }
u64 lib_tisp_vic_deinit(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; ev(3, 0, 0, 0, 0, 0); return 0; }
u64 lib_tisp_vic_cfg(u32 k, u32 a, u32 a2, u32 a3) { (void)a2; (void)a3; ev(4, k, a, 0, 0, 0); return 0; }
u64 lib_tisp_vic_stop(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; ev(5, 0, 0, 0, 0, 0); return 0; }
u64 lib_tisp_vic_reset(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; ev(6, 0, 0, 0, 0, 0); return 0; }
u64 tx_isp_vic_start(u32 v, u32 m, u32 a2, u32 a3) { (void)a2; (void)a3; ev(7, v, m, 0, 0, 0); return sc_start[nstart++ & 1]; }
u64 tx_vic_enable_irq(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 0; }
u64 tx_vic_disable_irq(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 0; }
int isp_printf(unsigned int level, const char *fmt, ...)
{
	va_list ap;
	unsigned long a3 = 0, a4 = 0;
	int line = side ? fmt != t32_data_c39a0 : strcmp(fmt, "vic: invalid subdev\n") != 0;

	va_start(ap, fmt);
	(void)va_arg(ap, unsigned long);
	a3 = va_arg(ap, unsigned long);
	a4 = va_arg(ap, unsigned long);
	va_end(ap);
	ev(9, level, line ? (u32)a3 : 0, line ? (u32)a4 : 0, 0, 0);
	return 0;
}

#define O_SD	0x0000u
#define O_VIC	0x0200u		/* 0xa80 */
#define O_REGS	0x0c80u		/* 0x400 */
#define O_INFO	0x1080u
#define O_ARG	0x1100u		/* 108 B (vin copy 104 B / cfg) */
#define O_REQ	0x1180u
#define IMGSZ	0x11c0u
static u8 *img, save0[IMGSZ], save1[IMGSZ];

static int fail(const char *what, unsigned int c, unsigned int op)
{
	printf("t32_vicops_oracle: FAIL case %u op %u: %s\n", c, op, what);
	return 1;
}

int main(void)
{
	unsigned int c, i, n = 40000, eff[5] = { 0 };

	img = t32o_alloc(IMGSZ);
	for (c = 0; c < n; c++) {
		unsigned int op = c % 5;
		u8 *sd = img + O_SD, *vic = img + O_VIC, *req = img + O_REQ, *arg = img + O_ARG;
		u32 sdw = T32O_PTR(sd), ro = 0, rr = 0, cmd, g0[4], go[4], gr[4], done = rnd() % 2 ? 0 : rnd(), argp;
		struct t32_vic_mdma m;

		for (i = 0; i < IMGSZ; i++)
			img[i] = (u8)rnd();
		for (i = 0; i < 4; i++)
			g0[i] = rnd();
		g0[0] = rnd() % 4;
		if (rnd() % 2)
			g0[3] = 0;
		sc_k = rnd() % 4;
		sc_start[0] = rnd();
		sc_start[1] = rnd();
		cmd = 0x200000cu + rnd() % 19;
		if (cmd == T32_VIC_IOC_LINK)
			cmd = rnd() % 2 ? 0x2000000u : 0x3000000u;
		switch (rnd() % 8) {
		case 0: sdw = 0; break;
		case 1: sdw = rnd() % 2 ? 0xfffff001u : 0xfffff001u + rnd() % 0xfff; break;
		default: break;
		}
		t32_g_wr32(sd, T32_VICD_SD_PRIV, T32O_PTR(vic));
		if (op != 0 && rnd() % 6 == 0)
			t32_g_wr32(sd, T32_VICD_SD_PRIV, rnd() % 2 ? 0 : 0xfffff001u + rnd() % 0xfff);
		t32_g_wr32(vic, T32_VICD_BASE, T32O_PTR(img + O_REGS));
		for (i = 0; i < 4; i++) {
			t32_g_wr32(vic, T32_VICD_STATE(i), rnd() % 3 ? 2 + rnd() % 2 : rnd());
			t32_g_wr32(vic, T32_VICD_LINK(i), rnd() % 4);
		}
		t32_g_wr32(vic, T32_VICD_RUNNING, rnd() % 2 ? 0 : rnd());
		t32_g_wr32(req, 0, rnd() % 2 ? 0 : rnd());
		t32_g_wr32(req, 4, rnd() % 4);
		t32_g_wr32(arg, T32_VICD_VIN_INFO, T32O_PTR(img + O_INFO));
		t32_g_wr16(img + O_INFO, T32_VICD_INFO_INDEX, (u16)(rnd() % 4));
		if (op == 3 && cmd == T32_VIC_IOC_START)
			t32_g_wr32(arg, 0, rnd() % 3 ? rnd() % 8 : rnd());	/* sensor index + mode, also negative */
		argp = rnd() % 6 ? T32O_PTR(arg) : 0;
		if (((op == 0 && t32_g_rd32(req, 0)) || op == 3) && rnd() % 2)
			sc_k = rnd();	/* the start paths use only k & 0xff */
		memcpy(save0, img, IMGSZ);
		for (side = 0; side < 2; side++) {
			memcpy(img, save0, IMGSZ);
			EV[side].n = 0;
			nstart = 0;
			t32o_complete_n = 0;
			if (!side) {
				m.frames_left = g0[0]; m.frame = g0[1]; m.dma_base = g0[2]; m.no_complete = g0[3];
				switch (op) {
				case 0: ro = (u32)t32_vic_core_ops_init(&os, T32O_HOST(sdw), (u32 *)req); break;
				case 1: ro = (u32)t32_vic_sync_sensor_attr(T32O_HOST(sdw), T32O_HOST(argp)); break;
				case 2: ro = (u32)t32_vic_bad_subdev(); break;
				case 3: ro = (u32)t32_vic_sensor_ops_ioctl(&os, T32O_HOST(sdw), cmd, (u32 *)arg); break;
				default: t32_vic_mdma_irq_function(&os, &m, vic, done); break;
				}
				if (t32o_complete_n)
					ev(8, t32o_complete_n, t32o_complete_arg, 0, 0, 0);
				go[0] = m.frames_left; go[1] = m.frame; go[2] = m.dma_base; go[3] = m.no_complete;
				memcpy(save1, img, IMGSZ);
			} else {
				t32_data_d9310 = g0[0]; t32_data_d9318 = g0[1]; t32_data_d9304 = g0[2]; t32_data_d930c = g0[3];
				switch (op) {
				case 0: rr = (u32)t32o_call(R(vic_core_ops_init), sdw, T32O_PTR(req), 0, 0, 0); break;
				case 1: rr = (u32)t32o_call(R(vic_sensor_ops_sync_sensor_attr), sdw, argp, 0, 0, 0); break;
				case 2: rr = (u32)t32o_call(R(vic_sensor_ops_sync_sensor_attr_part_2), rnd(), 0, 0, 0, 0); break;
				case 3: rr = (u32)t32o_call(R(vic_sensor_ops_ioctl), sdw, cmd, T32O_PTR(arg), 0, 0); break;
				default: t32o_call(R(vic_mdma_irq_function), T32O_PTR(vic), done, 0, 0, 0); break;
				}
				if (t32o_complete_n)
					ev(8, t32o_complete_n, t32o_complete_arg, 0, 0, 0);
				gr[0] = t32_data_d9310; gr[1] = t32_data_d9318; gr[2] = t32_data_d9304; gr[3] = t32_data_d930c;
			}
		}
		if (op != 4 && ro != rr)
			return fail("return value", c, op);
		if (memcmp(go, gr, sizeof(go)))
			return fail("MDMA globals", c, op);
		if (memcmp(img, save1, IMGSZ)) {
			for (i = 0; i < IMGSZ && img[i] == save1[i]; i++)
				;
			printf("  first difference at image +0x%x\n", i);
			return fail("memory image", c, op);
		}
		if (EV[0].n != EV[1].n || memcmp(EV[0].e, EV[1].e, sizeof(EV[0].e)))
			return fail("hook call log", c, op);
		if (memcmp(img, save0, IMGSZ) || EV[0].n || memcmp(g0, go, sizeof(g0)))
			eff[op]++;
	}
	printf("t32_vicops_oracle: %u cases, 0 mismatches; cases with an effect per op: %u %u %u %u %u\n", n,
	       eff[0], eff[1], eff[2], eff[3], eff[4]);
	return 0;
}
