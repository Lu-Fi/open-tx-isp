// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: mipi_phy_stream_on.constprop.5 of driver/t32/tx_isp_t32_mipi.h
 * against Matt Davis' recovered vendor C (ref/, PR #15 @ 2e2e06bc).
 * Both sides run on the same arena (subdev, CSI device, two sensor infos,
 * two controller blocks, PHY block; restored between the runs); compared:
 * every arena byte, the calls in order (delays, lock and flags, isp_printf
 * format and value), return values.  Inputs: NULL / ERR_PTR subdev and CSI
 * pointers, sensor states around 2, bus type equal / 2 / other, mode
 * 0 / 1 / 2 / other, stream count 0 / set, controller 0 / 1, lanes 1..4,
 * MIPI rates incl. negative products and settle times around 0..255,
 * d9008 0 / set.  Exit 0 = all equal, 1 = mismatch.
 */
#include <stdlib.h>
#include "t32o8_rec.h"
#include "tx_isp_t32_mipi.h"

u64 mipi_phy_stream_on_constprop_5(u32, u32, u32, u32);

enum { F_UDELAY = 1, F_MSLEEP, F_LOCK, F_UNLOCK, F_PRINTF };
u32 t32_data_d9008;
char t32_data_c47a0[] = "csi settle_time[0] = 0x%x\n";	/* data.c strings */
char t32_data_c47bc[] = "The sensor dbus_type is %d\n";
char t32_data_c47d8[] = "csi settle_time[1] = 0x%x\n";

static struct t32o8_log *cur;
static u8 *arena;
static u32 r_flags;
#define ARENA 0x3000
static u32 off(u32 p) { return p - T32O_PTR(arena) < ARENA ? p - T32O_PTR(arena) : p; }

u64 __udelay(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; t32o8_rec(cur, F_UDELAY, a0, 0, 0, 0); return 0; }
u64 private_msleep(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; t32o8_rec(cur, F_MSLEEP, a0, 0, 0, 0); return 0; }
u64 __private_spin_lock_irqsave(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	t32o8_rec(cur, F_LOCK, off(a0), 0, 0, 0);
	*(u32 *)T32O_HOST(a1) = r_flags;
	return 0;
}
u64 private_spin_unlock_irqrestore(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; t32o8_rec(cur, F_UNLOCK, off(a0), a1, 0, 0); return 0; }
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6)
{
	u32 w[5] = { a2, a3, a4, a5, a6 };

	t32o8_rec_fmt(cur, F_PRINTF, a0, T32O_HOST(a1), w, 5);
	return 0;
}

static void o_udelay(u32 us) { __udelay(us, 0, 0, 0); }
static void o_msleep(u32 ms) { private_msleep(ms, 0, 0, 0); }
static void o_lock(void *l, u32 *f)
{
	static u32 tmp;

	__private_spin_lock_irqsave(T32O_PTR(l), T32O_PTR(&tmp), 0, 0);
	*f = tmp;
}
static void o_unlock(void *l, u32 f) { private_spin_unlock_irqrestore(T32O_PTR(l), f, 0, 0); }
static int o_printf(int level, const char *fmt, ...)
{
	u32 w[6];
	int n;
	va_list ap;

	va_start(ap, fmt);
	n = t32o8_va_words(fmt, ap, w, 6);
	va_end(ap);
	t32o8_rec_fmt(cur, F_PRINTF, (u32)level, fmt, w, n);
	return 0;
}
static const struct t32_mipi_ops ops = { o_udelay, o_msleep, o_lock, o_unlock, o_printf };

static u32 rs = 0x3191eedu;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

/* arena: +0 subdev, +0x400 CSI device, +0x800 / +0x900 sensor infos, +0x1000 / +0x1400 controllers,
 * +0x2000 PHY, +0x2c00 attr */
int main(void)
{
	unsigned int c, k, failed = 0;
	static u8 init[ARENA], vend[ARENA];

	t32o_reset();
	arena = t32o_alloc(ARENA);
	for (c = 0; c < 40000; c++) {
		u8 *sd = arena, *csi = arena + 0x400, *attr = arena + 0x2c00;
		u32 sdp, idx = rnd() & 1, vr, orr, i;
		int bad;

		for (k = 0; k < ARENA; k++)
			arena[k] = (u8)rnd();
		*(u32 *)(sd + 280) = (rnd() % 10) ? T32O_PTR(csi) : (rnd() & 1) ? 0 : 0xfffff001u + rnd() % 0xfff;
		*(u32 *)(csi + 244) = T32O_PTR(arena + 0x1000);
		*(u32 *)(csi + 248) = T32O_PTR(arena + 0x1400);
		*(u32 *)(csi + 736) = T32O_PTR(arena + 0x2000);
		for (i = 0; i < 2; i++) {
			u8 *info = arena + 0x800 + 0x100 * i;

			*(u32 *)(csi + 340 + 104 * i) = T32O_PTR(info);
			*(u32 *)(csi + 704 + 4 * i) = (rnd() & 3) ? 2 + rnd() % 3 : (rnd() & 1) ? 1 : rnd();
			*(u32 *)(info + 20) = (rnd() & 1) ? 1 : rnd() % 4;
			*(u32 *)(info + 28) = (rnd() & 3) ? rnd() % 9000 : rnd();
			info[40] = (u8)(rnd() & 1);
			info[41] = (u8)(1 + rnd() % 4);
		}
		*(u32 *)(csi + 856) = (rnd() & 1) ? 0 : rnd();
		*(u32 *)(csi + 860) = (rnd() % 5) ? 1 + (rnd() & 1) : rnd() % 4;
		*(u32 *)(attr + 0) = (rnd() % 5) ? 1 : rnd() % 3;
		*(u32 *)(attr + 4) = idx;
		t32_data_d9008 = (rnd() & 1) ? 0 : rnd();
		r_flags = rnd();
		sdp = (rnd() % 20) ? T32O_PTR(sd) : (rnd() & 1) ? 0 : 0xfffff001u + rnd() % 0xfff;
		memcpy(init, arena, ARENA);
		t32o8_clear();
		cur = &t32o8_v;
		vr = (u32)t32o_call((t32o_fn)(void (*)(void))mipi_phy_stream_on_constprop_5, sdp, T32O_PTR(attr), 0, 0, 0);
		memcpy(vend, arena, ARENA);
		memcpy(arena, init, ARENA);
		cur = &t32o8_o;
		orr = (u32)t32_mipi_phy_stream_on(&ops, T32O_HOST(sdp), (const u32 *)attr, t32_data_d9008);
		bad = t32o8_cmp("mipi", failed < 8) || vr != orr || memcmp(vend, arena, ARENA);
		if (bad && failed++ < 8) {
			for (k = 0; k < ARENA && vend[k] == arena[k]; k++)
				;
			printf("MISMATCH case %u: ret %08x / %08x arena @%x mode %u\n", c, vr, orr, k, *(u32 *)(csi + 860));
		}
	}
	printf("t32_mipi_oracle: 40000 compared, %u mismatches\n", failed);
	return failed ? 1 : 0;
}
