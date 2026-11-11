// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our VIC core ioctl (driver/t32/tx_isp_t32_vic_ioctl.h)
 * against the unmodified vendor vic_core_ops_ioctl (Matt Davis' recovered
 * C, PR #15 @ 2e2e06bc).  Random subdevs / VIC state (counts 0..4, op
 * tables and handlers present or not, streaming, one-shot state, GPIO
 * slots, interface), commands 0x0ffffff..0x1000011 and others, NULL /
 * error subdev and VIC pointers where the vendor checks them, and scripted
 * hook answers (index, init, reset, waits, GPIO values, handler results)
 * run through ours, are restored and run through the vendor; compared:
 * every byte, the return value and the ordered hook log (every hook with
 * its arguments, the init block by its bytes, isp_printf with all
 * arguments).  Exit 0 = all equal.
 */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_vic_ioctl.h"

#define R(f)	((t32o_fn)(void (*)(void))(f))
u32 t32_data_bd6e8[17] = { 0x309c, 0x3088, 0x3a60, 0x30d0, 0x3a60, 0x3a60, 0x3a60, 0x3260, 0x32b0, 0x3a60,
			   0x32dc, 0x3a60, 0x3440, 0x34d8, 0x37b4, 0x36ac, 0x38a0 };	/* the vendor jump table */
char t32_data_bd760[] = "tx_isp_vic_pm_streamon";
char t32_data_bd74c[] = "tx_isp_vic_snap_raw";
char t32_data_bd72c[] = "tx_isp_vic_gpio_state_switch";
char t32_data_c3d6c[] = "\n [%s %d] VIC is null \n";
char t32_data_c3f38[] = "\n [%s %d] Fail to vic streamon\n";
char t32_data_c3f58[] = "[%s %d] vic is streamon !!!\n";
char t32_data_c3f78[] = "Driver vinum = %d, raw_type = %d, wdr_rawmode = %d, paddr = 0x%08x, snap_cnt = %d, frame_size = %d\n";
char t32_data_c3fdc[] = "Wait for completion timeout after %d ms\n";
char t32_data_c4008[] = "Wait interrupted by signal\n";
char t32_data_c4024[] = "snap %d raw successed! \n";
char t32_data_c4040[] = "switch0";
char t32_data_c4048[] = "switch1";
char t32_data_c4050[] = "oneshot wait for prepare timeout\n";
char t32_data_c4074[] = "[%s %d] Sensor%d belongs to VIC%d. VIC can operate a maximum of 2 GPIOs..";

u64 vic_core_ops_ioctl(u32, u32, u32, u32);

static u32 rng = 0x6d2b79f5u;
static u32 rnd(void)
{
	rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
	return rng;
}

#define MAXE 40
static struct { unsigned int n; u32 e[MAXE][10]; } EV[2];
static int side;
static u32 nl, lk_fl[4];
static u32 *ev(u32 a, u32 b, u32 c, u32 d)
{
	static u32 dummy[10];
	unsigned int n = EV[side].n++;

	if (n >= MAXE)
		return dummy;
	EV[side].e[n][0] = a; EV[side].e[n][1] = b; EV[side].e[n][2] = c; EV[side].e[n][3] = d;
	return EV[side].e[n];
}
static u32 hash(const u8 *p, u32 n)
{
	u32 h = 2166136261u, i;

	for (i = 0; i < n; i++)
		h = (h ^ p[i]) * 16777619u;
	return h;
}
static u32 hstr(const char *s) { return hash((const u8 *)s, (u32)strlen(s)); }

static u32 r_idx[4], r_init[4], r_reset, r_wait[2], r_val, r_hnd, ni, nn, nw;
/* both sides */
static u32 c_idx(u32 vic, u32 n) { ev(3, vic, n, 0); return r_idx[ni++ % 4]; }
static u32 c_init(u32 idx, u32 mode, const u8 *p) { ev(4, idx, mode, hash(p, 16)); return r_init[nn++ % 4]; }
static u32 c_wait(u32 c, u32 ms) { ev(8, c, ms, 0); return r_wait[nw++ % 2]; }
static u32 c_desc(u32 g) { ev(10, g, 0, 0); return g ^ 0x5a5a5a5au; }
static u32 c_raw(u32 d) { ev(11, d, 0, 0); return d ^ r_val; }
static u64 hnd(u32 sd, u32 arg, u32 a2, u32 a3) { (void)a2; (void)a3; ev(30, sd, arg, 0); return (u64)r_hnd; }
static u64 ntf(u32 sd, u32 e, u32 v, u32 a3) { (void)a3; ev(31, sd, e, v); return 0; }
static void l_common(u32 l, u32 *fl) { ev(15, l, 0, 0); *fl = lk_fl[nl++ % 4]; }
/* ours */
static void h_stop(void) { ev(1, 0, 0, 0); }
static void h_deinit(void) { ev(2, 0, 0, 0); }
static u32 h_idx(u8 *vic, u32 n) { return c_idx(T32O_PTR(vic), n); }
static int h_init(u32 idx, u32 mode, const u8 *p) { return (int)c_init(idx, mode, p); }
static void h_son(void) { ev(5, 0, 0, 0); }
static int h_reset(u8 *sd) { ev(6, T32O_PTR(sd), 0, 0); return (int)r_reset; }
static void h_ic(u8 *c) { ev(7, T32O_PTR(c), 0, 0); }
static int h_wait(u8 *c, u32 ms) { return (int)c_wait(T32O_PTR(c), ms); }
static void h_greq(u32 g, const char *name) { ev(9, g, hstr(name), 0); }
static u32 h_desc(u32 g) { return c_desc(g); }
static u32 h_raw(u32 d) { return c_raw(d); }
static void h_dir(u32 g, u32 v) { ev(12, g, v, 0); }
static void h_sw(u32 i, u32 g0, u32 a0, u32 g1, u32 a1) { u32 *e = ev(13, i, g0, a0); e[4] = g1; e[5] = a1; }
static void h_gfree(u32 g) { ev(14, g, 0, 0); }
static void h_lock(u8 *l, u32 *fl) { l_common(T32O_PTR(l), fl); }
static void h_unlock(u8 *l, u32 fl) { ev(16, T32O_PTR(l), fl, 0); }
static void h_rstop(u32 n) { ev(17, n, 0, 0); }
static void h_vreset(void) { ev(18, 0, 0, 0); }
static void h_irq(u32 on) { ev(19, on, 0, 0); }
static void h_start(u8 *vic, u32 on) { ev(20, T32O_PTR(vic), on, 0); }
static void h_ctrl(u32 on, u32 v) { ev(21, on, v, 0); }
static const struct t32_vicio_os os = { h_stop, h_deinit, h_idx, h_init, h_son, h_reset, h_ic, h_wait, h_greq, h_desc,
					h_raw, h_dir, h_sw, h_gfree, h_lock, h_unlock, h_rstop, h_vreset, h_irq, h_start, h_ctrl };
/* vendor (argument registers the vendor leaves unset are not recorded) */
#define V0(name, body) u64 name(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; body; }
V0(lib_tisp_vic_stop, ev(1, 0, 0, 0); return 0)
V0(lib_tisp_vic_deinit, ev(2, 0, 0, 0); return 0)
V0(tx_isp_vic_index_cal, return c_idx(a0, a1))
V0(lib_tisp_vic_init, return (u64)(int)c_init(a0, a1, T32O_HOST(a2)))
V0(lib_tisp_vic_stream_on, ev(5, 0, 0, 0); return 0)
V0(tx_isp_vic_reset_isra_5, ev(6, a0, 0, 0); return (u64)(int)r_reset)
V0(private_init_completion, ev(7, a0, 0, 0); return 0)
V0(private_wait_for_completion_timeout, return (u64)(int)c_wait(a0, a1))
V0(private_gpio_request, ev(9, a0, hstr(T32O_HOST(a1)), 0); return 0)
V0(gpio_to_desc, return c_desc(a0))
V0(gpiod_get_raw_value, return c_raw(a0))
V0(private_gpio_direction_output, ev(12, a0, a1, 0); return 0)
u64 tx_isp_riscv_switch_gpio(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4) { u32 *e = ev(13, a0, a1, a2); e[4] = a3; e[5] = a4; return 0; }
V0(private_gpio_free, ev(14, a0, 0, 0); return 0)
u64 __private_spin_lock_irqsave(u32 l, u32 fl, u32 a2, u32 a3)
{
	u32 v;

	(void)a2; (void)a3;
	l_common(l, &v);
	t32_g_wr32(T32O_HOST(fl), 0, v);
	return 0;
}
V0(private_spin_unlock_irqrestore, ev(16, a0, a1, 0); return 0)
V0(tx_isp_riscv_stop_vic, ev(17, a0, 0, 0); return 0)
V0(lib_tisp_vic_reset, ev(18, 0, 0, 0); return 0)
V0(tx_vic_enable_irq, ev(19, a0, 0, 0); return 0)
V0(tx_isp_vic_start, ev(20, a0, a1, 0); return 0)
V0(lib_tisp_vic_ctrl, ev(21, a0, a1, 0); return 0)

/* both sides: level, format text, then each conversion (%s by its text) */
int isp_printf(unsigned int level, const char *fmt, ...)
{
	unsigned int k = 3;
	const char *f;
	va_list ap;
	u32 *e = ev(11, level, hstr(fmt), 0);

	va_start(ap, fmt);
	for (f = fmt; *f && k < 10; f++) {
		if (*f != '%')
			continue;
		while (f[1] && !strchr("sdxucp", f[1]))
			f++;
		f++;
		if (*f == 's') {	/* ours may pass a host stack buffer, the vendor passes 32-bit words */
			unsigned long p = va_arg(ap, unsigned long);

			e[k++] = hstr((const char *)(uintptr_t)(side ? (u32)p : p));
		} else if (*f == 'p')
			e[k++] = (u32)va_arg(ap, unsigned long);
		else
			e[k++] = va_arg(ap, unsigned int);
	}
	va_end(ap);
	return 0;
}

#define O_SD	0x0000u
#define O_OPS	0x0120u
#define O_OP0	0x0140u
#define O_OP1	0x0160u
#define O_SENS	0x0180u
#define O_ARG	0x0280u
#define O_VIC	0x0300u		/* 0xa00 */
#define IMGSZ	0x0d00u
static u8 *img, save0[IMGSZ], save1[IMGSZ];

static int fail(const char *what, unsigned int c)
{
	unsigned int i, k, s;

	printf("t32_vicio_oracle: FAIL case %u: %s\n", c, what);
	if (getenv("T32O_VERBOSE"))
		for (s = 0; s < 2; s++)
			for (i = 0; i < EV[s].n && i < MAXE; i++) {
				printf("%s %2u:", s ? "ref" : "our", i);
				for (k = 0; k < 10; k++)
					printf(" %08x", EV[s].e[i][k]);
				printf("\n");
			}
	return 1;
}

int main(void)
{
	static const u32 resets[] = { 0, 0, (u32)-515, (u32)-22, 5 };
	unsigned int c, i, n = 60000, hit[4] = { 0 };

	img = t32o_alloc(IMGSZ);
	for (c = 0; c < n; c++) {
		u8 *sd = img + O_SD, *vic = img + O_VIC;
		u32 sdw = T32O_PTR(sd), cmd, argw = T32O_PTR(img + O_ARG), ro = 0, rr = 0, cnt;

		for (i = 0; i < IMGSZ; i++)
			img[i] = (u8)rnd();
		cmd = rnd() % 16 ? T32_VIO_CMD_BASE - 1 + rnd() % 19 : rnd();
		t32_g_wr32(sd, T32_VIO_SD_PRIV, T32O_PTR(vic));
		t32_g_wr32(sd, T32_VIO_SD_OPS, T32O_PTR(img + O_OPS));
		t32_g_wr32(img + O_OPS, 0, rnd() % 4 ? T32O_PTR(img + O_OP0) : 0);
		t32_g_wr32(img + O_OPS, 12, rnd() % 4 ? T32O_PTR(img + O_OP1) : 0);
		t32_g_wr32(img + O_OP0, 4, rnd() % 4 ? T32O_PTR(hnd) : 0);
		t32_g_wr32(img + O_OP1, 4, rnd() % 4 ? T32O_PTR(hnd) : 0);
		t32_g_wr32(sd, T32_VIO_SD_NOTIFY, rnd() % 4 ? T32O_PTR(ntf) : 0);
		cnt = rnd() % 5;
		t32_g_wr32(vic, T32_VIO_COUNT, cnt);
		t32_g_wr32(vic, T32_VIO_STREAM, rnd() % 2 ? 0 : rnd() % 2 ? 1 : rnd());
		t32_g_wr32(vic, T32_VIO_OS, rnd() % 3 ? rnd() % 2 : rnd());
		t32_g_wr32(vic, 340, T32O_PTR(img + O_SENS));
		t32_g_wr32(img + O_SENS, 148, rnd() % 2 ? 2 : rnd() % 4);
		for (i = 0; i < 4; i++)
			vic[T32_VIO_GPIO_USED + i] = rnd() % 2 ? 0 : rnd() % 3 ? 1 : (u8)rnd();
		for (i = 0; i < 4; i++)
			t32_g_wr32(vic, T32_VIO_GPIO_SLOT + 16 * i + 8, rnd() % 2 ? 5 : rnd() % 8);
		for (i = 0; i < 4; i++) {
			r_idx[i] = cmd == T32_VIO_CMD_BASE + 3 && rnd() % 2 ? rnd() : rnd() % 2;
			r_init[i] = rnd() % 3 ? 0 : rnd();
			lk_fl[i] = rnd();
		}
		r_reset = rnd() % 8 ? resets[rnd() % 5] : rnd();
		r_hnd = rnd() % 8 ? resets[rnd() % 5] : rnd();
		r_wait[0] = rnd() % 3 ? 0 : rnd() % 2 ? 1 + rnd() % 1000 : (u32)-rnd() % 1000 - 1;
		r_wait[1] = rnd() % 3 ? 0 : rnd() % 2 ? 1 + rnd() % 1000 : (u32)-512;
		r_val = rnd();
		if (rnd() % 20 == 0)
			sdw = rnd() % 2 ? 0 : (u32)-1 - rnd() % 4095;
		if (rnd() % 20 == 0)
			t32_g_wr32(sd, T32_VIO_SD_PRIV, rnd() % 2 ? 0 : (u32)-1 - rnd() % 4095);
		/* the vendor reads through NULL / error pointers for these (UB, see the header) */
		if (cmd != T32_VIO_CMD_BASE + 3 && cmd != T32_VIO_CMD_BASE + 10) {
			if (cmd != T32_VIO_CMD_BASE && cmd != T32_VIO_CMD_BASE + 1)
				sdw = T32O_PTR(sd);
			if (sdw && cmd - T32_VIO_CMD_BASE < 2 && !t32_vio_valid(sdw))
				sdw = T32O_PTR(sd);
			t32_g_wr32(sd, T32_VIO_SD_PRIV, T32O_PTR(vic));
		}
		memcpy(save0, img, IMGSZ);
		for (side = 0; side < 2; side++) {
			memcpy(img, save0, IMGSZ);
			EV[side].n = 0;
			nl = ni = nn = nw = 0;
			memset(EV[side].e, 0, sizeof(EV[side].e));
			if (!side) {
				ro = (u32)t32_vic_core_ops_ioctl(&os, T32O_HOST(sdw), cmd, T32O_HOST(argw));
				memcpy(save1, img, IMGSZ);
			} else {
				rr = (u32)t32o_call(R(vic_core_ops_ioctl), sdw, cmd, argw, 0, 0);
			}
		}
		if (ro != rr)
			return fail("return value", c);
		if (memcmp(img, save1, IMGSZ))
			return fail("memory image", c);
		if (EV[0].n != EV[1].n || memcmp(EV[0].e, EV[1].e, sizeof(EV[0].e)))
			return fail("hook call log", c);
		hit[ro == 0 ? (EV[0].n ? 0 : 1) : ro == (u32)-22 || ro == (u32)-19 || ro == (u32)-1 ? 2 : 3]++;
	}
	printf("t32_vicio_oracle: %u cases, 0 mismatches; 0 with hooks / 0 without / -22, -19 or -1 / other: %u %u %u %u\n",
	       n, hit[0], hit[1], hit[2], hit[3]);
	return 0;
}
