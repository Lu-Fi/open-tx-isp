// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: ioctl helpers (driver/t32/tx_isp_t32_txioctl.h) against
 * the vendor tx_isp_driver_version, _set_default_bin_path,
 * _get_default_bin_path (.isra), tx_isp_sensor_register_sensor,
 * _sensor_release_sensor, _multisensor_mode, _set_ae_algo_open,
 * _video_s_stream, _video_link_stream (Matt Davis' recovered C, PR #15 @
 * 2e2e06bc, symbols renamed op3_* by op3_ren.sh).
 *
 * Per case one random world (ISP device with kernel-call flag, 16
 * subdevice slots NULL or set, ops tables with recording callbacks or 0,
 * scripted results per (slot, subdevice), core bin path table with
 * strings, user block, scripted copy results, d9008 / da774 state).
 * Compared: the event log (copies with user address / size / content
 * hash, callbacks with their command and buffer words, prints), every
 * world and user byte, da774, the return value.  set_ae_algo_open: the
 * vendor copies 128 stack bytes it never wrote to the user when no AE
 * callback filled them; then only the first 8 bytes are compared.
 * Exit 0 = all equal, 1 = mismatch.
 */
#include "t32_op3_glue.h"
#include "tx_isp_t32_txioctl.h"

#define NCASE	30000
#define ARENA	0x6000u
#define NSD	6u
#define O_SD(i)	(0x400u + 0x200u * (i))	/* ops at +0x110: core +0x130, video +0x158, sensor +0x180 */
#define O_CORE	0x1000u
#define O_USER	0x2400u
#define USERB	0x200u

u32 op3_t32_data_d9008, op3_t32_data_da774[4];
u32 t32_tx_isp_dev, t32_tx_g_d9008, t32_tx_stream_count[4];
u8 op3_t32_data_be5b0[96] = { 'P', 'R', 'J', '0', '0', '7', [32] = '2', '0', '2', '5', '1', '1', '0', '7', 'a' };	/* data.c:1423 */
char op3_t32_data_beb6c[] = "tx_isp_driver_version";			/* data.c:1790 */
char op3_t32_data_c6b38[] = "[ %s:%d ] copy to user error\n";		/* data.c:9971 */
char op3_t32_data_be928[] = "tx_isp_set_default_bin_path";		/* data.c:1645 */
char op3_t32_data_c6ae4[] = "[ %s:%d ] copy from user error\n";	/* data.c:9950 */
char op3_t32_data_c6db4[] = "[ %s:%d ] vinum exceeds upper limit!!!\n";	/* data.c:10130 */
char op3_t32_data_be90c[] = "tx_isp_get_default_bin_path";		/* data.c:1638 */
char op3_t32_data_beab4[] = "tx_isp_video_s_stream";			/* data.c:1744 */
char op3_t32_data_c6b58[] = "\n==> [%s %d] %s \n";			/* data.c:9979 */
char op3_t32_data_c6b6c[] = "s_stream error\n";			/* data.c:9984 */
char op3_t32_data_be998[] = "tx_isp_set_ae_algo_open";			/* data.c:1673 */
char op3_t32_data_c6b04[] = "[ %s:%d ] Ae Algo Function register version error\n";	/* data.c:9958 */
char op3_t32_data_beacc[] = "tx_isp_sensor_release_sensor";		/* data.c:1750 */
char op3_t32_data_beaec[] = "tx_isp_sensor_register_sensor";		/* data.c:1758 */
char op3_t32_data_be9b0[] = "tx_isp_multisensor_mode";			/* data.c:1679 */
char op3_t32_data_bea50[] = "tx_isp_video_link_stream";		/* data.c:1719 */

u64 tx_isp_driver_version_isra_11(u32, u32, u32, u32);
u64 tx_isp_set_default_bin_path_isra_14(u32, u32, u32, u32);
u64 tx_isp_get_default_bin_path_isra_17(u32, u32, u32, u32);
u64 tx_isp_sensor_register_sensor(u32, u32, u32, u32);
u64 tx_isp_sensor_release_sensor(u32, u32, u32, u32);
u64 tx_isp_multisensor_mode(u32, u32, u32, u32);
u64 tx_isp_set_ae_algo_open(u32, u32, u32, u32);
u64 tx_isp_video_s_stream(u32, u32, u32, u32);
u64 tx_isp_video_link_stream(u32, u32, u32, u32);

static u8 *arena;
static u32 A(u32 off) { return T32O_PTR(arena) + off; }
static u32 w32(u32 p, u32 k) { return ((const u32 *)T32O_HOST(p))[k]; }
static u32 fnvb(const u8 *p, u32 n)
{
	u32 h = 2166136261u, i;

	for (i = 0; i < n; i++)
		h = (h ^ p[i]) * 16777619u;
	return h;
}

enum { S_CORE, S_SENS, S_SSTR, S_LINK, S_N };
static u32 rtab[S_N][NSD + 1], cfu_ret, ctu_ret;
static int cb_filled, cur_f;
static u32 sd_index(u32 sd)
{
	u32 i;

	for (i = 0; i < NSD; i++)
		if (sd == A(O_SD(i)))
			return i;
	return NSD;
}

static u32 cb_core(u32 sd, u32 cmd, u32 buf)
{
	u32 *b = T32O_HOST(buf), k;

	op3_call("core_ioctl", 4, sd, cmd, b[0], b[1]);
	for (k = 2; k < T32_AE_ALGO_BUF / 4; k++)
		b[k] = sd * 31 + k;
	cb_filled = 1;
	return rtab[S_CORE][sd_index(sd)];
}
static u32 cb_sens(u32 sd, u32 cmd, u32 buf)
{
	op3_call("sensor_ioctl", 3, sd, cmd, fnvb(T32O_HOST(buf), cmd == T32_SENSOR_MULTI ? T32_MULTI_BUF : T32_SENSOR_BUF), 0);
	return rtab[S_SENS][sd_index(sd)];
}
static u32 cb_sstr(u32 sd, u32 buf) { op3_call("video_s_stream", 3, sd, w32(buf, 0), w32(buf, 1), 0); return rtab[S_SSTR][sd_index(sd)]; }
static u32 cb_link(u32 sd, u32 buf) { op3_call("video_link_stream", 3, sd, w32(buf, 0), w32(buf, 1), 0); return rtab[S_LINK][sd_index(sd)]; }

static u32 cfu_rec(void *dst, u32 user, u32 n)
{
	op3_call("private_copy_from_user", 2, user, n, 0, 0);
	memcpy(dst, T32O_HOST(user), n);
	return cfu_ret;
}
static u32 ctu_rec(u32 user, const void *src, u32 n)
{
	u32 c = (cur_f == 6 && !cb_filled) ? 8 : n;	/* set_ae_algo_open without a callback: stack bytes */

	op3_call("private_copy_to_user", 3, user, n, fnvb(src, c), 0);
	memcpy(T32O_HOST(user), src, c);
	return ctu_ret;
}
static void sprintf_rec(char *dst, const char *fmt)
{
	op3_call("sprintf", 1, fnvb((const u8 *)fmt, (u32)strlen(fmt)), 0, 0, 0);
	strcpy(dst, fmt);	/* the paths hold no conversion */
}

u64 op3_private_copy_from_user(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; return cfu_rec(T32O_HOST(a0), a1, a2); }
u64 op3_private_copy_to_user(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; return ctu_rec(a0, T32O_HOST(a1), a2); }
u64 op3_sprintf(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; sprintf_rec(T32O_HOST(a0), T32O_HOST(a1)); return 0; }
u64 op3_memcpy(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; memcpy(T32O_HOST(a0), T32O_HOST(a1), a2); return a0; }
u32 t32_tx_os_copy_from_user(void *d, u32 u, u32 n) { return cfu_rec(d, u, n); }
u32 t32_tx_os_copy_to_user(u32 u, const void *s, u32 n) { return ctu_rec(u, s, n); }
void t32_tx_os_sprintf(char *d, const char *f) { sprintf_rec(d, f); }

/* ---- world ---- */
static u8 tmpl[ARENA];
static void sw(u32 off, u32 v) { *(u32 *)(tmpl + off) = v; }
static u32 rfn(void *fn) { return (op3_rnd() & 3) ? T32O_PTR(fn) : 0; }

static void build(u32 base)
{
	u32 k, i, j, m = op3_rnd();

	for (k = 0; k < ARENA; k += 4)
		sw(k, op3_rnd());
	sw(T32_ISP_KCALL, (m & 3) == 0 ? op3_rnd() : 0);
	sw(0, base + O_SD(0));
	sw(O_SD(0) + T32_SD_PRIV_CORE, base + O_CORE);
	for (k = 0; k < T32_DEV_SD_N; k++)
		sw(T32_DEV_SD + 4 * k, (op3_rnd() & 3) == 0 ? base + O_SD(op3_rnd() % NSD) : 0);
	for (i = 0; i < NSD; i++) {
		u32 s = O_SD(i), r = op3_rnd();

		snprintf((char *)tmpl + s + 0x1c0, 16, "sd%u", i);
		sw(s + T32_SD_NAME, base + s + 0x1c0);
		sw(s + T32_SD_OPS, base + s + 0x110);
		sw(s + 0x110 + T32_OPS_CORE, (r & 1) ? base + s + 0x130 : 0);
		sw(s + 0x110 + T32_OPS_VIDEO, (r & 2) ? base + s + 0x158 : 0);
		sw(s + 0x110 + T32_OPS_SENSOR, (r & 4) ? base + s + 0x180 : 0);
		sw(s + 0x130 + T32_CORE_IOCTL, rfn((void *)cb_core));
		sw(s + 0x158 + T32_VIDEO_S_STREAM, rfn((void *)cb_sstr));
		sw(s + 0x158 + T32_VIDEO_LINK_STREAM, rfn((void *)cb_link));
		sw(s + 0x180 + T32_SENSOR_IOCTL, rfn((void *)cb_sens));
	}
	for (i = 0; i < 4; i++)	/* bin path strings without conversions */
		for (j = 0; j < 4; j++) {
			char *p = (char *)tmpl + O_CORE + i * T32_BINPATH_INPUT + j * T32_BINPATH_BYTES + T32_BINPATH_GET;

			snprintf(p, 64, "/etc/sensor/p%u_%u_%08x.bin", i, j, op3_rnd());
		}
	/* user block: { input, slot, ... } for the bin paths, the AE version word */
	sw(O_USER, (m & 4) ? op3_rnd() : op3_rnd() % 5);
	sw(O_USER + 4, (m & 0x30) ? op3_rnd() % 4 : 3);
	if ((m & 0xc0) != 0xc0)
		sw(O_USER, (m & 0x100) && cur_f == 6 ? T32_AE_ALGO_VERSION : *(u32 *)(tmpl + O_USER));
	if (cur_f == 7 || cur_f == 8)
		sw(O_USER + 4, op3_rnd() % 4);
	for (i = 0; i < S_N; i++)
		for (k = 0; k <= NSD; k++) {
			u32 r = op3_rnd() & 7;

			rtab[i][k] = r < 4 ? 0 : r < 6 ? (u32)-T32_ENOIOCTLCMD : op3_rnd();
		}
	cfu_ret = (op3_rnd() & 7) ? 0 : op3_rnd();
	ctu_ret = (op3_rnd() & 7) ? 0 : op3_rnd();
}

static void setup(void)
{
	t32o_reset();
	memset(&op3_log, 0, sizeof(op3_log));
	arena = t32o_alloc(ARENA);
	memcpy(arena, tmpl, ARENA);
	cb_filled = 0;
}

enum { F_VER, F_SETBIN, F_GETBIN, F_REG, F_REL, F_MULTI, F_AE, F_SSTREAM, F_LINK, F_N };
static const char *const fname[F_N] = {
	"driver_version", "set_default_bin_path", "get_default_bin_path", "sensor_register_sensor",
	"sensor_release_sensor", "multisensor_mode", "set_ae_algo_open", "video_s_stream", "video_link_stream"
};
typedef u64 (*vfn)(u32, u32, u32, u32);
static const vfn vend[F_N] = {
	tx_isp_driver_version_isra_11, tx_isp_set_default_bin_path_isra_14, tx_isp_get_default_bin_path_isra_17,
	tx_isp_sensor_register_sensor, tx_isp_sensor_release_sensor, tx_isp_multisensor_mode,
	tx_isp_set_ae_algo_open, tx_isp_video_s_stream, tx_isp_video_link_stream
};
static u64 ours(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4)
{
	(void)a2; (void)a3; (void)a4;
	switch (cur_f) {
	case F_VER: return (u32)t32_tx_driver_version(a0);
	case F_SETBIN: return (u32)t32_tx_set_default_bin_path(a0, a1);
	case F_GETBIN: return (u32)t32_tx_get_default_bin_path(a0, a1);
	case F_REG: return (u32)t32_tx_sensor_register_sensor(a0, a1);
	case F_REL: return (u32)t32_tx_sensor_release_sensor(a0, a1);
	case F_MULTI: return (u32)t32_tx_multisensor_mode(a0, a1);
	case F_AE: return (u32)t32_tx_set_ae_algo_open(a0, a1);
	case F_SSTREAM: return (u32)t32_tx_video_s_stream(a0, a1);
	default: return (u32)t32_tx_video_link_stream(a0, a1);
	}
}

struct snap {
	struct op3_log log;
	u8 w[ARENA];
	u32 cnt[4], ret;
};
static struct snap sv, so;

int main(void)
{
	unsigned int i;
	u32 base, cnt0[4], d9008;

	setup();
	base = A(0);
	for (i = 0; i < NCASE; i++) {
		int f = (int)(i % F_N);
		u32 k, a0;

		cur_f = f;
		build(base);
		d9008 = (op3_rnd() & 1) ? op3_rnd() : 0;
		for (k = 0; k < 4; k++)
			cnt0[k] = op3_rnd() % 3;
		a0 = f == F_VER ? base + O_USER : base;

		setup();
		op3_t32_data_d9008 = d9008;
		memcpy(op3_t32_data_da774, cnt0, sizeof(cnt0));
		sv.ret = OP3_VC(vend[f], a0, base + O_USER, 0, 0, 0);
		sv.log = op3_log;
		memcpy(sv.w, arena, ARENA);
		memcpy(sv.cnt, op3_t32_data_da774, sizeof(cnt0));

		setup();
		t32_tx_g_d9008 = d9008;
		memcpy(t32_tx_stream_count, cnt0, sizeof(cnt0));
		so.ret = OP3_VC(ours, a0, base + O_USER, 0, 0, 0);
		so.log = op3_log;
		memcpy(so.w, arena, ARENA);
		memcpy(so.cnt, t32_tx_stream_count, sizeof(cnt0));

		if (op3_log_cmp(fname[f], &sv.log, &so.log))
			return 1;
		if (sv.ret != so.ret || memcmp(sv.w, so.w, ARENA) || memcmp(sv.cnt, so.cnt, sizeof(cnt0))) {
			u32 j;

			for (j = 0; j < ARENA && sv.w[j] == so.w[j]; j++)
				;
			fprintf(stderr, "%s: ret %x/%x, first world diff 0x%x\n", fname[f], sv.ret, so.ret, j);
			return 1;
		}
	}
	printf("t32_txioctl_oracle: %u cases equal (%u functions)\n", NCASE, F_N);
	return 0;
}
