// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: IQ bin ioctl helpers (driver/t32/tx_isp_t32_txbin.h)
 * against the vendor tx_isp_switch_bin_isra_23 and tx_isp_default_bin_isra_23
 * (Matt Davis' recovered C, PR #15 @ 2e2e06bc, symbols renamed op3_* by
 * op3_ren.sh).  Inputs: random user blocks (path mode, slot 0..5, check
 * flag, sensor index), core input states / sensor number, current slots,
 * scripted copy / vmalloc / switch / completion / sensor-setting results,
 * d9008 and cfb54.  Compared: the event log (with block hashes), the user
 * block, the core / ISP words, cfb54, the return value.
 * Exit 0 = all equal, 1 = mismatch.
 */
#include "t32_op3_glue.h"
#include "tx_isp_t32_txbin.h"

#define NCASE	30000
#define ARENA	0x3000u
#define O_ISP	0x000u
#define O_SD	0x100u
#define O_CORE	0x200u
#define O_USER	0xa00u
#define O_PATHS	0x1000u

u32 op3_t32_data_d9008, t32_tx_isp_dev, t32_tx_g_d9008, t32_tx_stream_count[4];
u8 op3_t32_data_cfb54[4], t32_tx_first_bin;
char op3_t32_data_be0c0[] = "tx_isp_switch_bin";				/* data.c:1107 */
char op3_t32_data_c4ad4[] = "[ %s:%d ] copy error!!!\n";			/* data.c:7898 */
char op3_t32_data_c4b50[] = "[ %s:%d ] vmalloc failed!!!\n";			/* data.c:7929 */
char op3_t32_data_c4b70[] = "Timeout waiting for completion\n";		/* data.c:7937 */
char op3_t32_data_c4b90[] = "[ %s:%d ] The camera is not activated!!!\n";	/* data.c:7945 */
char op3_t32_data_c4bbc[] = "[ %s:%d ] switch bin failed!!!\n";		/* data.c:7956 */
char op3_t32_data_be764[] = "tx_isp_default_bin";				/* data.c:1532 */
char op3_t32_data_c6ae4[] = "[ %s:%d ] copy from user error\n";		/* data.c:9950 */
char op3_t32_data_c6b38[] = "[ %s:%d ] copy to user error\n";			/* data.c:9971 */
char op3_t32_data_c6ddc[] = "to tiziano iq_index:%d binNum:%d\n";		/* data.c:10140 */

u64 tx_isp_switch_bin_isra_23(u32, u32, u32, u32);
u64 tx_isp_default_bin_isra_23(u32, u32, u32, u32);

static u8 *arena;
static u32 A(u32 off) { return T32O_PTR(arena) + off; }
static u32 fnvb(const u8 *p, u32 n)
{
	u32 h = 2166136261u, i;

	for (i = 0; i < n; i++)
		h = (h ^ p[i]) * 16777619u;
	return h;
}

static u32 cfu_ret, ctu_ret, vm_ok, sw_ret, sw_slot, wait_ret, iq_v, nsec_v, dn_v, wdr_v, set_v[8], len_v[8];

static u32 cfu_rec(void *d, u32 u, u32 n) { op3_call("private_copy_from_user", 2, u, n, 0, 0); memcpy(d, T32O_HOST(u), n); return cfu_ret; }
static u32 ctu_rec(u32 u, const void *s, u32 n) { op3_call("private_copy_to_user", 3, u, n, fnvb(s, n), 0); memcpy(T32O_HOST(u), s, n); return ctu_ret; }
static u32 vm_rec(u32 n) { op3_call("private_vmalloc", 1, n, 0, 0, 0); return vm_ok ? A(O_PATHS) : 0; }
static void path_rec(u32 ch, u32 p)
{
	u32 k;

	op3_call("lib_tisp_get_bin_path", 2, ch, p, 0, 0);
	for (k = 0; k < 6; k++)
		snprintf((char *)T32O_HOST(p) + T32_SWBIN_PATH(k), 100, "/etc/sensor/ch%u_slot%u.bin", ch, k);
}
static void sprintf_rec(char *d, const char *f) { op3_call("sprintf", 1, fnvb((const u8 *)f, (u32)strlen(f)), 0, 0, 0); strcpy(d, f); }
static u32 sw_rec(u32 ch, u8 *b) { op3_call("tisp_switch_bin", 2, ch, fnvb(b, T32_SWBIN_BYTES), 0, 0); b[4] = (u8)sw_slot; return sw_ret; }
static u32 wait_rec(u32 c, u32 t) { op3_call("private_wait_for_completion_timeout", 2, c, t, 0, 0); return wait_ret; }
static void info_rec(u32 s, u8 *b)
{
	u32 k;

	op3_call("lib_tisp_get_bin_info", 2, s, fnvb(b, T32_DEFBIN_BYTES), 0, 0);
	for (k = 100; k < 400; k++)
		b[k] = (u8)(k * 7 + s);
}

u64 op3_private_copy_from_user(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; return cfu_rec(T32O_HOST(a0), a1, a2); }
u64 op3_private_copy_to_user(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; return ctu_rec(a0, T32O_HOST(a1), a2); }
u64 op3_private_vmalloc(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; return vm_rec(a0); }
OP3_STUB(private_vfree, 1)
u64 op3_lib_tisp_get_bin_path(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; path_rec(a0, a1); return 0; }
u64 op3_sprintf(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; sprintf_rec(T32O_HOST(a0), T32O_HOST(a1)); return 0; }
u64 op3_tisp_switch_bin(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return sw_rec(a0, T32O_HOST(a1)); }
u64 op3_private_wait_for_completion_timeout(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return wait_rec(a0, a1); }
u64 op3_get_sensor_start_iq_index(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; op3_call("get_sensor_start_iq_index", 1, a0, 0, 0, 0); return iq_v; }
u64 op3_get_sensor_section_num(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; op3_call("get_sensor_section_num", 2, a0, a1, 0, 0); return nsec_v; }
u64 op3_get_sensor_setting(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; op3_call("get_sensor_setting", 3, a0, a1, a2, 0); return set_v[a2 & 7]; }
u64 op3_get_sensor_setting_len(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; op3_call("get_sensor_setting_len", 3, a0, a1, a2, 0); return len_v[a2 & 7]; }
OP3_STUB(get_sensor_setting_md5, 3)
u64 op3_lib_tisp_get_bin_info(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; info_rec(a0, T32O_HOST(a1)); return 0; }
u64 op3_tisp_day_or_night_g_ctrl(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; op3_call("tisp_day_or_night_g_ctrl", 1, a0, 0, 0, 0); return dn_v; }
u64 op3_lib_tisp_get_wdr_enable(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; op3_call("lib_tisp_get_wdr_enable", 1, a0, 0, 0, 0); return wdr_v; }

u32 t32_tx_os_copy_from_user(void *d, u32 u, u32 n) { return cfu_rec(d, u, n); }
u32 t32_tx_os_copy_to_user(u32 u, const void *s, u32 n) { return ctu_rec(u, s, n); }
void t32_tx_os_sprintf(char *d, const char *f) { sprintf_rec(d, f); }
u32 t32_tx_os_vmalloc(u32 n) { return vm_rec(n); }
void t32_tx_os_vfree(u32 a) { op3_call("private_vfree", 1, a, 0, 0, 0); }
void t32_tx_os_get_bin_path(u32 ch, u32 b) { path_rec(ch, b); }
int t32_tx_os_tisp_switch_bin(u32 ch, void *b) { return (int)sw_rec(ch, b); }
int t32_tx_os_wait_for_completion_timeout(u32 c, u32 t) { return (int)wait_rec(c, t); }
u32 t32_tx_os_sensor_start_iq_index(u32 s) { return (u32)op3_get_sensor_start_iq_index(s, 0, 0, 0); }
u32 t32_tx_os_sensor_section_num(u32 s, u32 iq) { return (u32)op3_get_sensor_section_num(s, iq, 0, 0); }
u32 t32_tx_os_sensor_setting(u32 s, u32 iq, u32 k) { return (u32)op3_get_sensor_setting(s, iq, k, 0); }
u32 t32_tx_os_sensor_setting_len(u32 s, u32 iq, u32 k) { return (u32)op3_get_sensor_setting_len(s, iq, k, 0); }
void t32_tx_os_sensor_setting_md5(u32 s, u32 iq, u32 k) { op3_call("get_sensor_setting_md5", 3, s, iq, k, 0); }
void t32_tx_os_tisp_get_bin_info(u32 s, void *b) { info_rec(s, b); }
u32 t32_tx_os_tisp_day_or_night(u32 s) { return (u32)op3_tisp_day_or_night_g_ctrl(s, 0, 0, 0); }
u32 t32_tx_os_tisp_wdr_enable(u32 s) { return (u32)op3_lib_tisp_get_wdr_enable(s, 0, 0, 0); }

static u8 tmpl[ARENA];
static void sw(u32 off, u32 v) { *(u32 *)(tmpl + off) = v; }

static int cur_f;
static u64 ours(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4)
{
	(void)a3; (void)a4;
	return cur_f ? (u32)t32_tx_default_bin(a0, a1) : (u32)t32_tx_switch_bin(a0, a1, a2);
}

struct snap {
	struct op3_log log;
	u8 w[ARENA];
	u8 first;
	u32 ret;
};
static struct snap sv, so;

static void setup(void)
{
	t32o_reset();
	memset(&op3_log, 0, sizeof(op3_log));
	arena = t32o_alloc(ARENA);
	memcpy(arena, tmpl, ARENA);
}

int main(void)
{
	unsigned int i, k;
	u32 base;
	u8 first0;

	setup();
	base = A(0);
	for (i = 0; i < NCASE; i++) {
		int f = (int)(i & 1);
		u32 m = op3_rnd(), ch = op3_rnd() % 4, d9008 = (op3_rnd() & 1) ? op3_rnd() : 0;

		for (k = 0; k < ARENA; k += 4)
			sw(k, op3_rnd());
		sw(O_ISP, base + O_SD);
		sw(O_SD + T32_SD_PRIV_CORE, base + O_CORE);
		for (k = 0; k < 4; k++) {
			sw(O_CORE + T32_CORE_IN_STATE(k), (op3_rnd() & 1) ? 4 : op3_rnd() % 6);
			sw(O_ISP + T32_ISP_BIN_SLOT(k), op3_rnd() % 6);
		}
		sw(O_CORE + T32_CORE_SNS_NUM, op3_rnd() % 6);
		sw(O_USER, (m & 1) ? 0 : op3_rnd());
		tmpl[O_USER + 4] = (u8)(op3_rnd() % 6);
		tmpl[O_USER + T32_DEFBIN_CHECK] = (m & 2) ? 0 : (u8)op3_rnd();
		tmpl[O_USER + T32_DEFBIN_SENSOR] = (u8)(op3_rnd() % 6);
		cfu_ret = (op3_rnd() & 7) ? 0 : op3_rnd();
		ctu_ret = (op3_rnd() & 7) ? 0 : op3_rnd();
		vm_ok = (op3_rnd() & 7) != 0;
		sw_ret = (op3_rnd() & 1) ? 0 : op3_rnd();
		sw_slot = (op3_rnd() & 1) ? tmpl[O_USER + 4] : op3_rnd() % 6;
		wait_ret = (op3_rnd() & 1) ? 0 : op3_rnd();
		iq_v = op3_rnd();
		nsec_v = (op3_rnd() & 0xffffff00u) | (op3_rnd() % 7);
		dn_v = op3_rnd();
		wdr_v = op3_rnd();
		for (k = 0; k < 8; k++) {
			set_v[k] = op3_rnd();
			len_v[k] = op3_rnd();
		}
		first0 = (op3_rnd() & 1) ? (u8)op3_rnd() : 0;

		setup();
		op3_t32_data_d9008 = d9008;
		op3_t32_data_cfb54[0] = first0;
		sv.ret = OP3_VC(f ? tx_isp_default_bin_isra_23 : tx_isp_switch_bin_isra_23, f ? base + O_ISP : ch,
				f ? base + O_USER : base + O_ISP, base + O_USER, 0, 0);
		sv.log = op3_log;
		memcpy(sv.w, arena, ARENA);
		sv.first = op3_t32_data_cfb54[0];

		setup();
		t32_tx_g_d9008 = d9008;
		t32_tx_first_bin = first0;
		cur_f = f;
		so.ret = OP3_VC(ours, f ? base + O_ISP : ch, f ? base + O_USER : base + O_ISP, base + O_USER, 0, 0);
		so.log = op3_log;
		memcpy(so.w, arena, ARENA);
		so.first = t32_tx_first_bin;

		if (op3_log_cmp(f ? "default_bin" : "switch_bin", &sv.log, &so.log))
			return 1;
		if (sv.ret != so.ret || memcmp(sv.w, so.w, ARENA) || sv.first != so.first) {
			fprintf(stderr, "%s: ret %x/%x, world or cfb54 differs\n", f ? "default_bin" : "switch_bin", sv.ret, so.ret);
			return 1;
		}
	}
	printf("t32_txbin_oracle: %u cases equal (2 functions)\n", NCASE);
	return 0;
}
