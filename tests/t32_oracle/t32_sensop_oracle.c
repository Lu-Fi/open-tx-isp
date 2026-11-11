// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our sensor control ops (driver/t32/tx_isp_t32_sensor_ops.h)
 * against the unmodified vendor functions sensor_init, sensor_early_init and
 * the 28 ops it installs and sensor_setting_init (Matt Davis' recovered C, PR #15 @ 2e2e06bc).
 *
 * One memory image (ISP core with 3 channels of video_in / update blocks,
 * 3 sensor attributes, names, value block, control block, register-access
 * words) starts random per case; ours runs on it, the image is restored and
 * the vendor runs.  Compared: every byte of the image, the return values, every
 * call of the sensor alloc callbacks (which callback, value, shift), every
 * notify call (sd, cmd, the argument bytes) and every isp_printf (level, arg).
 * The callbacks / notify answer from the same random script on both sides.
 * The channels the case does not use are the decoys.  sensor_init's table is
 * compared by function identity (our op <-> the vendor op of the same name).
 * Inputs avoid the vendor UB named in the header (den 0, NULL callbacks that
 * the vendor calls unchecked, a NULL core for set/get_register).
 * Exit 0 = all equal, 1 = first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_sensor_ops.h"

/*
 * #282 F5: a mutated port that follows a wrong table offset into unmapped memory used to die with SIGSEGV
 * (rc 139), which run.sh could only count as "detected" by crash.  The reference never faults on these inputs
 * (the unmutated oracle passes), so a fault is the port's: report it as a mismatch with exit 1 instead.
 */
#include <signal.h>
#include <unistd.h>
static void t32o_segv(int sig)
{
	static const char m[] = "MISMATCH: the port faulted (wild guest access, wrong table offset); the vendor did not\n";

	(void)sig;
	if (write(2, m, sizeof(m) - 1) < 0)
		_exit(2);
	_exit(1);
}
static void __attribute__((constructor)) t32o_segv_install(void)
{
	signal(SIGSEGV, t32o_segv);
	signal(SIGBUS, t32o_segv);
}

#define WEAK __attribute__((weak))
#define R(f)	((t32o_fn)(void (*)(void))(f))

WEAK u32 t32_data_da760[1];	/* ISP core pointer (.bss) */
WEAK char t32_data_c6a9c[64] = "sensor_set_mode fmt";	/* printk format, content irrelevant */
u8 *t32_isp_core;

/* reference functions */
#define REFS_LIST(X) X(sensor_alloc_analog_gain) X(sensor_alloc_analog_gain_short) X(sensor_alloc_digital_gain) \
	X(sensor_alloc_digital_gain_short) X(sensor_alloc_integration_time) X(sensor_alloc_integration_time_short) \
	X(sensor_set_analog_gain) X(sensor_set_analog_gain_short) X(sensor_set_digital_gain) X(sensor_set_digital_gain_short) \
	X(sensor_set_integration_time) X(sensor_set_integration_time_short) X(sensor_get_normal_fps) X(sensor_fps_control) \
	X(sensor_set_fps) X(sensor_get_id) X(sensor_set_mode) X(sensor_set_register) X(sensor_get_register) \
	X(sensor_init) X(sensor_early_init) X(sensor_hw_reset_disable) X(sensor_hw_reset_enable) X(sensor_end_changes) \
	X(sensor_set_wdr_mode) X(sensor_disable_isp)
#define DECL(n) u64 n(u32, u32, u32, u32);
REFS_LIST(DECL)
void sensor_start_changes(void);
int32_t sensor_read_black_pedestal(void);
int32_t sensor_get_lines_per_second(void);
int32_t sensor_setting_init(void);

static u32 rng = 0x13579bdfu;
static u32 rnd(void)
{
	rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
	return rng;
}

/* ---- call log + answer script ---- */
#define MAXL 16
struct clog {
	unsigned int n, nn, np;
	u32 cb[MAXL][3];		/* id, value, shift */
	u32 nt[MAXL][2];		/* sd, cmd */
	u8 nb[MAXL][64];
	u32 pr[MAXL][2];		/* level, arg */
};
static struct clog L[2];	/* 0 ours, 1 vendor */
static int side;
static u32 script[8];	/* answers: cb out, cb ret, notify ret, notify out, notify mode byte */
static unsigned int argsz;	/* bytes of the notify argument to log: 64 (registers) or 1 (set_mode) */

static u32 cb_common(u32 id, u32 v, u32 s, u32 *out)
{
	struct clog *l = &L[side];

	if (l->n < MAXL) {
		l->cb[l->n][0] = id; l->cb[l->n][1] = v; l->cb[l->n][2] = s;
	}
	l->n++;
	*out = script[0] ^ id;
	return script[1] + id;
}
#define HOOKS(k) \
	static u32 ho##k(u32 v, u32 s, u32 *out) { return cb_common(k, v, s, out); } \
	static u64 hr##k(u32 v, u32 s, u32 out, u32 a3) { (void)a3; return cb_common(k, v, s, T32O_HOST(out)); }
HOOKS(0) HOOKS(1) HOOKS(2) HOOKS(3) HOOKS(4) HOOKS(5)
static u32 hook_word(int vendor, int k)
{
	static const t32_sensor_alloc_cb o[6] = { ho0, ho1, ho2, ho3, ho4, ho5 };
	static u64 (*const r[6])(u32, u32, u32, u32) = { hr0, hr1, hr2, hr3, hr4, hr5 };

	return vendor ? T32O_PTR(r[k]) : T32O_PTR(o[k]);
}

static int nt_common(u32 sd, u32 cmd, u8 *arg)
{
	struct clog *l = &L[side];

	if (l->nn < MAXL) {
		l->nt[l->nn][0] = sd; l->nt[l->nn][1] = cmd;
		memcpy(l->nb[l->nn], arg, argsz);
	}
	l->nn++;
	if (cmd == T32_NOTIFY_GET_REG)
		t32_g_wr32(arg, 56, script[3]);
	if (cmd == T32_NOTIFY_SET_MODE)
		arg[0] = (u8)script[4];
	return (int)script[2];
}
static int nt_ours(u8 *sd, u32 cmd, void *arg) { return nt_common(T32O_PTR(sd), cmd, arg); }
static u64 nt_ref(u32 sd, u32 cmd, u32 arg, u32 a3) { (void)a3; return (u32)nt_common(sd, cmd, T32O_HOST(arg)); }

/* isp_printf of both sides (the env.c one is weak) */
int isp_printf(unsigned int level, const char *fmt, ...)
{
	struct clog *l = &L[side];
	va_list ap;

	(void)fmt;
	va_start(ap, fmt);
	if (l->np < MAXL) {
		l->pr[l->np][0] = level;
		l->pr[l->np][1] = (u32)va_arg(ap, int);
	}
	va_end(ap);
	l->np++;
	return (int)(script[5] & 0xff);
}

/* ---- image ---- */
#define O_CORE	0x0000u
#define O_ATTR	0x0800u		/* 3 x 0x200 */
#define O_NAME	0x1400u		/* 3 x 64 */
#define O_VAL	0x1500u
#define O_CTL	0x1600u
#define O_ARGS	0x1700u		/* reg, val, ch words */
#define IMGSZ	0x1800u
#define NCH	3u
static u8 *img, save0[IMGSZ], save1[IMGSZ];

/* our ops / vendor ops by table slot (sensor_init) */
typedef void (*vfn)(void);
static const struct { u32 slot; vfn ours, vendor; } optab[] = {
#define OP(s, o, v) { s, (vfn)(o), (vfn)(v) },
	OP(T32_OP_ALLOC_AGAIN, t32_sensor_alloc_analog_gain, sensor_alloc_analog_gain)
	OP(T32_OP_ALLOC_AGAIN_S, t32_sensor_alloc_analog_gain_short, sensor_alloc_analog_gain_short)
	OP(T32_OP_ALLOC_DGAIN, t32_sensor_alloc_digital_gain, sensor_alloc_digital_gain)
	OP(T32_OP_ALLOC_DGAIN_S, t32_sensor_alloc_digital_gain_short, sensor_alloc_digital_gain_short)
	OP(T32_OP_ALLOC_IT, t32_sensor_alloc_integration_time, sensor_alloc_integration_time)
	OP(T32_OP_FPS_CONTROL, t32_sensor_fps_control, sensor_fps_control)
	OP(T32_OP_ALLOC_IT_S, t32_sensor_alloc_integration_time_short, sensor_alloc_integration_time_short)
	OP(T32_OP_LINES_PER_SEC, t32_sensor_get_lines_per_second, sensor_get_lines_per_second)
	OP(T32_OP_NORMAL_FPS, t32_sensor_get_normal_fps, sensor_get_normal_fps)
	OP(T32_OP_BLACK_PEDESTAL, t32_sensor_read_black_pedestal, sensor_read_black_pedestal)
	OP(T32_OP_GET_ID, t32_sensor_get_id, sensor_get_id)
	OP(T32_OP_SET_IT, t32_sensor_set_integration_time, sensor_set_integration_time)
	OP(T32_OP_SET_IT_S, t32_sensor_set_integration_time_short, sensor_set_integration_time_short)
	OP(T32_OP_START_CHANGES, t32_sensor_start_changes, sensor_start_changes)
	OP(T32_OP_END_CHANGES, t32_sensor_end_changes, sensor_end_changes)
	OP(T32_OP_SET_AGAIN, t32_sensor_set_analog_gain, sensor_set_analog_gain)
	OP(T32_OP_SET_AGAIN_S, t32_sensor_set_analog_gain_short, sensor_set_analog_gain_short)
	OP(T32_OP_SET_DGAIN, t32_sensor_set_digital_gain, sensor_set_digital_gain)
	OP(T32_OP_SET_DGAIN_S, t32_sensor_set_digital_gain_short, sensor_set_digital_gain_short)
	OP(T32_OP_SET_MODE, t32_sensor_set_mode, sensor_set_mode)
	OP(T32_OP_SET_WDR_MODE, t32_sensor_set_wdr_mode, sensor_set_wdr_mode)
	OP(T32_OP_DISABLE_ISP, t32_sensor_disable_isp, sensor_disable_isp)
	OP(T32_OP_SET_FPS, t32_sensor_set_fps, sensor_set_fps)
	OP(T32_OP_HW_RESET_DIS, t32_sensor_hw_reset_disable, sensor_hw_reset_disable)
	OP(T32_OP_HW_RESET_EN, t32_sensor_hw_reset_enable, sensor_hw_reset_enable)
	OP(T32_OP_GET_REGISTER, t32_sensor_get_register, sensor_get_register)
	OP(T32_OP_SET_REGISTER, t32_sensor_set_register, sensor_set_register)
};
#define NOPS (sizeof(optab) / sizeof(optab[0]))

/* slot words -> table index (+1), so that both sides compare */
static int ops_normalize(u8 *im, int vendor)
{
	unsigned int k, j;

	for (k = 0; k < NOPS; k++) {
		u32 w = t32_g_rd32(im + O_CTL, optab[k].slot);

		for (j = 0; j < NOPS; j++)
			if (w == T32_WORD(vendor ? optab[j].vendor : optab[j].ours))
				break;
		if (j == NOPS)
			return 1;
		t32_g_wr32(im + O_CTL, optab[k].slot, j + 1);
	}
	return 0;
}

static int fail(const char *what, unsigned int c, unsigned int op)
{
	printf("t32_sensop_oracle: FAIL case %u op %u: %s\n", c, op, what);
	return 1;
}

/* attribute callback slots per side (restored to 0 placeholders before the compare) */
static const u32 cb_off[6] = { T32_SA_CB_AGAIN, T32_SA_CB_AGAIN_S, T32_SA_CB_DGAIN, T32_SA_CB_DGAIN_S, T32_SA_CB_IT, T32_SA_CB_IT_S };
static u32 cb_on[NCH];	/* bit k: callback k present */
static void cb_set(int vendor, int clear)
{
	unsigned int ch, k;

	for (ch = 0; ch < NCH; ch++)
		for (k = 0; k < 6; k++)
			t32_g_wr32(img + O_ATTR + ch * 0x200, cb_off[k],
				   (!clear && (cb_on[ch] >> k & 1)) ? hook_word(vendor, (int)k) : 0);
}
static void nt_set(int vendor, int on, int clear)
{
	t32_g_wr32(img + O_CORE, T32_G_SD_NOTIFY, (on && !clear) ? (vendor ? T32O_PTR(nt_ref) : T32O_PTR(nt_ours)) : 0);
}

#define NOP_CASES 22
int main(void)
{
	unsigned int c, i, n = 60000, cov[NOP_CASES] = { 0 };

	img = t32o_alloc(IMGSZ);
	for (c = 0; c < n; c++) {
		unsigned int op = c % NOP_CASES, ch, k;
		u32 value, ro = 0, rr = 0, gro = 0, grr = 0, early_arg;
		int nt_on, core_null = 0;
		u8 *val = img + O_VAL, *ctl = img + O_CTL, *attr;

		for (i = 0; i < IMGSZ; i++)
			img[i] = (u8)rnd();
		for (i = 0; i < 8; i++)
			script[i] = rnd();
		if (rnd() % 3 == 0)
			script[2] = 0;		/* notify success */
		ch = rnd() % NCH;
		for (k = 0; k < NCH; k++) {
			u8 *vin = img + O_CORE + T32_CORE_VIN(k);

			attr = img + O_ATTR + k * 0x200;
			t32_g_wr32(vin, T32_VIN_ATTR, T32O_PTR(attr));
			t32_g_wr32(attr, T32_SA_NAME, T32O_PTR(img + O_NAME + k * 64));
			if (!(t32_g_rd32(vin, T32_VIN_FPS) & 0xffff))
				t32_g_wr32(vin, T32_VIN_FPS, t32_g_rd32(vin, T32_VIN_FPS) | 1);
			cb_on[k] = rnd() & 0x3f;
			cb_on[k] |= 7;		/* again, again_short, dgain: called unchecked */
			if (cb_on[k] & 0x10)
				cb_on[k] |= 0x20;	/* it_short: called when the it callback is set */
		}
		t32_g_wr32(val, T32_SV_CH, ch);
		t32_g_wr32(ctl, T32_SC_CH, (rnd() & 1) ? ch : (ch + 1) % NCH);
		attr = img + O_ATTR + t32_g_rd32(ctl, T32_SC_CH) * 0x200;
		value = rnd();
		switch (rnd() % 4) {	/* hit the "unchanged" branch of the setters */
		case 0: value = t32_g_rd32(img + O_ATTR + ch * 0x200, rnd() % 2 ? T32_SA_AGAIN : T32_SA_DGAIN); break;
		case 1: value = t32_g_rd32(attr, rnd() % 2 ? T32_SA_IT : T32_SA_IT_S) & 0xffff; break;
		case 2: value = t32_g_rd32(img + O_CORE + T32_CORE_VIN(t32_g_rd32(ctl, T32_SC_CH)), T32_VIN_FPS); break;
		default: break;
		}
		if (rnd() % 3 == 0) {	/* set_*_short equal values */
			value = t32_g_rd32(img + O_ATTR + ch * 0x200, rnd() % 2 ? T32_SA_AGAIN_S : T32_SA_DGAIN_S);
		}
		if ((op == 10 || op == 11) && rnd() % 4 == 0)	/* the vendor compares the 16 bit time with the full word */
			t32_g_wr32(attr, op == 10 ? T32_SA_IT : T32_SA_IT_S, value & 0xffff);
		nt_on = rnd() % 4 != 0;
		if (op == 16 && rnd() % 5 == 0)
			core_null = 1;
		if (op == 20)
			core_null = rnd() % 2;
		t32_g_wr32(img + O_ARGS, 0, rnd());
		t32_g_wr32(img + O_ARGS, 4, rnd());
		t32_g_wr32(img + O_ARGS, 8, ch);
		early_arg = (op == 20 && rnd() % 3 == 0) ? 0 : T32O_PTR(img + O_CORE + 0x10);

		cb_set(0, 1);	/* placeholders 0 in the saved image: the slots are set per side and cleared after */
		nt_set(0, 0, 1);
		for (side = 0; side < 2; side++) {
			u32 core = core_null ? 0 : T32O_PTR(img + O_CORE);
			u32 *args = (u32 *)(img + O_ARGS);

			if (side)
				memcpy(img, save0, IMGSZ);
			else
				memcpy(save0, img, IMGSZ);
			memset(&L[side], 0, sizeof(L[side]));
			argsz = op == 16 ? 1 : 64;
			cb_set(side, 0);
			nt_set(side, nt_on, 0);
			if (side == 0) {
				t32_isp_core = T32O_HOST(core);
				switch (op) {
				case 0: ro = t32_sensor_alloc_analog_gain(value, val); break;
				case 1: ro = t32_sensor_alloc_analog_gain_short(value, val); break;
				case 2: ro = t32_sensor_alloc_digital_gain(value, val); break;
				case 3: ro = t32_sensor_alloc_digital_gain_short(value, val); break;
				case 4: ro = t32_sensor_alloc_integration_time(value, val); break;
				case 5: ro = t32_sensor_alloc_integration_time_short(value, val); break;
				case 6: t32_sensor_set_analog_gain(value, val); break;
				case 7: t32_sensor_set_analog_gain_short(value, val); break;
				case 8: t32_sensor_set_digital_gain(value, val); break;
				case 9: t32_sensor_set_digital_gain_short(value, val); break;
				case 10: t32_sensor_set_integration_time(value, ctl); break;
				case 11: t32_sensor_set_integration_time_short(value, ctl); break;
				case 12: ro = t32_sensor_get_normal_fps(ctl); break;
				case 13: ro = t32_sensor_fps_control(value, ctl); break;
				case 14: t32_sensor_set_fps(value, ctl); break;
				case 15: ro = t32_sensor_get_id(val); break;
				case 16: ro = (u32)t32_sensor_set_mode(value, ctl); break;
				case 17: ro = (u32)t32_sensor_set_register(&args[0], &args[1], &args[2]); break;
				case 18: ro = (u32)t32_sensor_get_register(&args[0], &args[1], &args[2]); break;
				case 19: t32_sensor_init(ctl, t32_g_rd32(ctl, T32_SC_CH)); break;
				case 20: ro = (u32)t32_sensor_early_init(T32O_HOST(early_arg)); gro = T32O_PTR(t32_isp_core); break;
				default:
					t32_sensor_hw_reset_disable(); t32_sensor_hw_reset_enable(); t32_sensor_start_changes();
					t32_sensor_end_changes(); t32_sensor_set_wdr_mode(); t32_sensor_disable_isp();
					ro = (u32)t32_sensor_read_black_pedestal() << 8 | (u32)t32_sensor_get_lines_per_second() |
					     (u32)t32_sensor_setting_init() << 16;
					break;
				}
			} else {
				u32 A = T32O_PTR(args);

				t32_data_da760[0] = core;
				switch (op) {
				case 0: rr = (u32)t32o_call(R(sensor_alloc_analog_gain), value, T32O_PTR(val), 0, 0, 0); break;
				case 1: rr = (u32)t32o_call(R(sensor_alloc_analog_gain_short), value, T32O_PTR(val), 0, 0, 0); break;
				case 2: rr = (u32)t32o_call(R(sensor_alloc_digital_gain), value, T32O_PTR(val), 0, 0, 0); break;
				case 3: rr = (u32)t32o_call(R(sensor_alloc_digital_gain_short), value, T32O_PTR(val), 0, 0, 0); break;
				case 4: rr = (u32)t32o_call(R(sensor_alloc_integration_time), value, T32O_PTR(val), 0, 0, 0); break;
				case 5: rr = (u32)t32o_call(R(sensor_alloc_integration_time_short), value, T32O_PTR(val), 0, 0, 0); break;
				case 6: t32o_call(R(sensor_set_analog_gain), value, T32O_PTR(val), 0, 0, 0); break;
				case 7: t32o_call(R(sensor_set_analog_gain_short), value, T32O_PTR(val), 0, 0, 0); break;
				case 8: t32o_call(R(sensor_set_digital_gain), value, T32O_PTR(val), 0, 0, 0); break;
				case 9: t32o_call(R(sensor_set_digital_gain_short), value, T32O_PTR(val), 0, 0, 0); break;
				case 10: t32o_call(R(sensor_set_integration_time), value, T32O_PTR(ctl), 0, 0, 0); break;
				case 11: t32o_call(R(sensor_set_integration_time_short), value, T32O_PTR(ctl), 0, 0, 0); break;
				case 12: rr = (u32)t32o_call(R(sensor_get_normal_fps), T32O_PTR(ctl), 0, 0, 0, 0); break;
				case 13: rr = (u32)t32o_call(R(sensor_fps_control), value, T32O_PTR(ctl), 0, 0, 0); break;
				case 14: t32o_call(R(sensor_set_fps), value, T32O_PTR(ctl), 0, 0, 0); break;
				case 15: rr = (u32)t32o_call(R(sensor_get_id), T32O_PTR(val), 0, 0, 0, 0); break;
				case 16: rr = (u32)t32o_call(R(sensor_set_mode), value, T32O_PTR(ctl), 0, 0, 0); break;
				case 17: rr = (u32)t32o_call(R(sensor_set_register), A, A + 4, A + 8, 0, 0); break;
				case 18: rr = (u32)t32o_call(R(sensor_get_register), A, A + 4, A + 8, 0, 0); break;
				case 19: t32o_call(R(sensor_init), T32O_PTR(ctl), t32_g_rd32(ctl, T32_SC_CH), 0, 0, 0); break;
				case 20: rr = (u32)t32o_call(R(sensor_early_init), early_arg, 0, 0, 0, 0); grr = t32_data_da760[0]; break;
				default:
					t32o_call(R(sensor_hw_reset_disable), 1, 2, 3, 4, 0);
					t32o_call(R(sensor_hw_reset_enable), 1, 2, 3, 4, 0);
					t32o_call(R(sensor_start_changes), 1, 2, 3, 4, 0);
					t32o_call(R(sensor_end_changes), 1, 2, 3, 4, 0);
					t32o_call(R(sensor_set_wdr_mode), 1, 2, 3, 4, 0);
					t32o_call(R(sensor_disable_isp), 1, 2, 3, 4, 0);
					rr = (u32)t32o_call(R(sensor_read_black_pedestal), 1, 2, 3, 4, 0) << 8 |
					     (u32)t32o_call(R(sensor_get_lines_per_second), 1, 2, 3, 4, 0) |
					     (u32)t32o_call(R(sensor_setting_init), 1, 2, 3, 4, 0) << 16;
					break;
				}
			}
			cb_set(side, 1);
			nt_set(side, nt_on, 1);
			if (op == 19 && ops_normalize(img, side))
				return fail("sensor_init: unknown function in the ops table", c, op);
			if (!side)
				memcpy(save1, img, IMGSZ);
		}
		/* return values: set_* / sensor_init return leftovers in the vendor code */
		if (!(op >= 6 && op <= 11) && op != 14 && op != 19 && ro != rr)
			return fail("return value", c, op);
		if (op == 12 && ro != (rr & 0xffff))
			return fail("get_normal_fps", c, op);
		if (gro != grr)
			return fail("ISP core pointer", c, op);
		if (memcmp(img, save1, IMGSZ)) {
			for (i = 0; i < IMGSZ && img[i] == save1[i]; i++)
				;
			printf("  first difference at image +0x%x: ours %02x vendor %02x\n", i, save1[i], img[i]);
			return fail("memory image", c, op);
		}
		if (L[0].n != L[1].n || memcmp(L[0].cb, L[1].cb, sizeof(L[0].cb)))
			return fail("sensor alloc callbacks", c, op);
		if (L[0].nn != L[1].nn || memcmp(L[0].nt, L[1].nt, sizeof(L[0].nt)) || memcmp(L[0].nb, L[1].nb, sizeof(L[0].nb)))
			return fail("notify calls", c, op);
		if (L[0].np != L[1].np || memcmp(L[0].pr, L[1].pr, sizeof(L[0].pr)))
			return fail("isp_printf calls", c, op);
		if (memcmp(img, save0, IMGSZ) || L[0].n || L[0].nn || L[0].np)
			cov[op]++;
	}
	printf("t32_sensop_oracle: %u cases, 0 mismatches; cases with an effect per op:", n);
	for (i = 0; i < NOP_CASES; i++)
		printf(" %u", cov[i]);
	printf("\n");
	return 0;
}
