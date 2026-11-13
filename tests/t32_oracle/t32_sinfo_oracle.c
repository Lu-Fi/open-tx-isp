// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our sensor-info proc glue (driver/t32/tx_isp_t32_sinfo_proc.h)
 * against the unmodified vendor sinfo_open, sinfo_count_open, sinfo_count_show
 * and sinfo_slot_publish (Matt Davis' recovered C, PR #15 @ 2e2e06bc).
 * A random image (4 slots, the published slot) runs through ours, is restored
 * and runs through the vendor; compared: every byte, the return values and
 * one ordered log of the OS calls (mutex, PDE_DATA, single_open with the show
 * callback identity and data, seq_printf value, proc_mkdir name/parent,
 * proc_create_data name/mode/parent/fops/data) with scripted answers.
 * Exit 0 = all equal, 1 = first mismatch.
 */
#include <stdio.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_sinfo_proc.h"

#define WEAK __attribute__((weak))
#define R(f)	((t32o_fn)(void (*)(void))(f))

u64 sinfo_open(u32, u32, u32, u32);
u64 sinfo_count_open(u32, u32, u32, u32);
u64 sinfo_count_show(u32, u32, u32, u32);
u64 sinfo_slot_publish(u32, u32, u32, u32);
u64 sinfo_show(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 0; }	/* not in this block */

/* vendor data: slots (.bss), mutex (.data), root dir (.bss), fops and the name table (rodata, data.c:444..458), formats */
WEAK u8 t32_data_d9024[4 * 160] __attribute__((aligned(4)));
WEAK u8 t32_data_ce270[32];
WEAK u32 t32_data_d9020[1];
WEAK u8 t32_data_bd5f8[64];
WEAK u32 t32_data_bd664[15];
WEAK char t32_data_c373c[4] = "%d\n", t32_data_c3740[12] = "sensor%d";

static u32 rng = 0x51f0c0deu;
static u32 rnd(void)
{
	rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
	return rng;
}

#define MAXE 40
static struct { unsigned int n; u32 e[MAXE][6]; } EV[2];
static int side;
static u32 script[4];
static void ev(u32 a, u32 b, u32 c, u32 d, u32 e, u32 f)
{
	unsigned int n = EV[side].n++;

	if (n < MAXE) {
		EV[side].e[n][0] = a; EV[side].e[n][1] = b; EV[side].e[n][2] = c;
		EV[side].e[n][3] = d; EV[side].e[n][4] = e; EV[side].e[n][5] = f;
	}
}
static u32 strh(const char *s)
{
	u32 h = 2166136261u;

	while (*s)
		h = (h ^ (u8)*s++) * 16777619u;
	return h;
}
static u32 show_id(u32 fn)
{
	if (side)
		return fn == T32O_PTR(sinfo_show) ? 1 : fn == T32O_PTR(sinfo_count_show) ? 2 : 0xbad;
	return fn;
}

/* ours */
static void o_lock(u8 *m) { ev(1, T32O_PTR(m), 0, 0, 0, 0); }
static void o_unlock(u8 *m) { ev(2, T32O_PTR(m), 0, 0, 0, 0); }
static void *o_pde(void *i) { ev(3, T32O_PTR(i), 0, 0, 0, 0); return T32O_HOST(script[0]); }
static int o_sopen(void *f, u32 show, void *d) { ev(4, T32O_PTR(f), show_id(show), T32O_PTR(d), 0, 0); return (int)script[1]; }
static int o_seq(void *s, const char *fmt, u32 v) { ev(5, T32O_PTR(s), strh(fmt), v, 0, 0); return 0; }
static u32 o_mkdir(const char *n, u32 p) { ev(6, strh(n), p, 0, 0, 0); return script[2]; }
static u32 o_pcd(const char *n, u32 m, u32 p, const void *f, void *d) { ev(7, strh(n), m, p, T32O_PTR(f), T32O_PTR(d)); return 0; }
static u32 root_word;
static const struct t32_sinfo_os os = {
	.slots = t32_data_d9024, .mutex = t32_data_ce270, .root = t32_data_d9020, .fops = t32_data_bd5f8,
	.sinfo_show = 1, .count_show = 2,
	.lock = o_lock, .unlock = o_unlock, .pde_data = o_pde, .single_open = o_sopen, .seq_printf = o_seq,
	.proc_mkdir = o_mkdir, .proc_create_data = o_pcd,
};
/* vendor */
u64 mutex_lock(u32 m, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; ev(1, m, 0, 0, 0, 0); return 0; }
u64 mutex_unlock(u32 m, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; ev(2, m, 0, 0, 0, 0); return 0; }
u64 PDE_DATA(u32 i, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; ev(3, i, 0, 0, 0, 0); return script[0]; }
u64 single_open(u32 f, u32 s, u32 d, u32 a3) { (void)a3; ev(4, f, show_id(s), d, 0, 0); return script[1]; }
u64 seq_printf(u32 s, u32 fmt, u32 v, u32 a3) { (void)a3; ev(5, s, strh(T32O_HOST(fmt)), v, 0, 0); return 0; }
u64 proc_mkdir(u32 n, u32 p, u32 a2, u32 a3) { (void)a2; (void)a3; ev(6, strh(T32O_HOST(n)), p, 0, 0, 0); return script[2]; }
u64 proc_create_data(u32 n, u32 m, u32 p, u32 f, u32 d)
{
	ev(7, strh(T32O_HOST(n)), m, p, f, d);
	return 0;
}
u64 t32o_snprintf(u32 buf, u32 sz, u32 fmt, u32 v) { return (u32)snprintf(T32O_HOST(buf), sz, T32O_HOST(fmt), (int)v); }

#define IMGSZ 0x200u
static u8 *img, save0[IMGSZ], save1[IMGSZ], sl0[sizeof(t32_data_d9024)], sl1[sizeof(t32_data_d9024)];

static int fail(const char *what, unsigned int c, unsigned int op)
{
	printf("t32_sinfo_oracle: FAIL case %u op %u: %s\n", c, op, what);
	return 1;
}

int main(void)
{
	/* data.c:444..458 point at these strings (c37e8 .. c3868, decoded from data.c) */
	static char names[15][16] = { "name", "chip_id", "i2c_addr", "i2c_adapter", "width", "height", "fps", "status",
				      "min_fps", "max_fps", "mclk", "boot", "video_interface", "rst_gpio", "pwdn_gpio" };
	unsigned int c, i, n = 20000;

	for (i = 0; i < 15; i++)
		t32_data_bd664[i] = T32O_PTR(names[i]);
	img = t32o_alloc(IMGSZ);
	for (c = 0; c < n; c++) {
		unsigned int op = c % 4;
		u32 a0 = rnd(), a1 = rnd(), idx = rnd() % 3 ? rnd() % 8 : rnd(), ro = 0, rr = 0;

		for (i = 0; i < IMGSZ; i++)
			img[i] = (u8)rnd();
		for (i = 0; i < sizeof(t32_data_d9024); i++)
			t32_data_d9024[i] = (u8)(rnd() % 3 ? rnd() % 2 : rnd());
		for (i = 0; i < 4; i++)
			script[i] = rnd();
		if (rnd() % 4 == 0)
			script[2] = 0;		/* proc_mkdir fails */
		root_word = rnd();
		t32_data_d9020[0] = root_word;
		memcpy(save0, img, IMGSZ);
		memcpy(sl0, t32_data_d9024, sizeof(sl0));
		for (side = 0; side < 2; side++) {
			memcpy(img, save0, IMGSZ);
			memcpy(t32_data_d9024, sl0, sizeof(sl0));
			EV[side].n = 0;
			if (!side) {
				switch (op) {
				case 0: ro = (u32)t32_sinfo_open(&os, T32O_HOST(a0), T32O_HOST(a1)); break;
				case 1: ro = (u32)t32_sinfo_count_open(&os, T32O_HOST(a0), T32O_HOST(a1)); break;
				case 2: ro = (u32)t32_sinfo_count_show(&os, T32O_HOST(a0), T32O_HOST(a1)); break;
				default: t32_sinfo_slot_publish(&os, img, idx); break;
				}
				memcpy(save1, img, IMGSZ);
				memcpy(sl1, t32_data_d9024, sizeof(sl1));
			} else {
				switch (op) {
				case 0: rr = (u32)t32o_call(R(sinfo_open), a0, a1, 0, 0, 0); break;
				case 1: rr = (u32)t32o_call(R(sinfo_count_open), a0, a1, 0, 0, 0); break;
				case 2: rr = (u32)t32o_call(R(sinfo_count_show), a0, a1, 0, 0, 0); break;
				default: t32o_call(R(sinfo_slot_publish), T32O_PTR(img), idx, 0, 0, 0); break;
				}
			}
		}
		if (ro != rr)
			return fail("return value", c, op);
		if (memcmp(img, save1, IMGSZ) || memcmp(t32_data_d9024, sl1, sizeof(sl1)))
			return fail("memory", c, op);
		if (EV[0].n != EV[1].n || memcmp(EV[0].e, EV[1].e, sizeof(EV[0].e)))
			return fail("OS call log", c, op);
	}
	printf("t32_sinfo_oracle: %u cases, 0 mismatches\n", n);
	return 0;
}
