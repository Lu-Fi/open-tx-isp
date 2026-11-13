// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: ISP core probe (driver/t32/tx_isp_t32_txcprobe.h)
 * against the vendor tx_isp_core_probe (Matt Davis' recovered C, PR #15 @
 * 2e2e06bc, symbols renamed op3_* by op3_ren.sh).  Inputs: PM global,
 * scripted kmalloc / kmem_cache_alloc / subdev_init (which sets 0..6 out
 * pads with enable bytes and a core state word) / tuning init results.
 * The callbacks the probe stores are shared symbols (ours alias the
 * vendor's).  Compared: the event log (lock names hashed), the core,
 * channel and pad bytes, the ops table, f3310, the return value.
 * Exit 0 = all equal, 1 = mismatch.
 */
#include "t32_op3_glue.h"
#include "tx_isp_t32_txcprobe.h"
#pragma GCC diagnostic ignored "-Wunused-parameter"	/* the callback stubs below */

#define NCASE	20000
#define ARENA	0x5000u
#define O_CORE	0x0000u		/* 4292 */
#define O_PDEV	0x1100u
#define O_PD	0x1200u
#define O_PADS	0x1300u		/* 6 x 36 */
#define O_TUNE	0x1400u
#define O_CHANS	0x1800u		/* 6 x 284 */
#define O_WQ	0x2000u

u32 op3_t32_data_d9004, op3_t32_data_f3310, op3_t32_data_d8c28[10], op3_t32_data_d8bbc[4], op3_t32_data_f331c[4];
u32 op3_kmalloc_caches[16];
u8 op3_t32_small_bss[64];
static u32 iops[8];
char op3_t32_data_c3634[] = "tx_isp_core_probe";				/* data.c:6576 */
char op3_t32_data_cd3b0[] = "[ %s:%d ] Failed to allocate sensor device\n";	/* data.c:16657 */
char op3_t32_data_cd3dc[] = "[ %s:%d ] Failed to init isp\n";		/* data.c:16668 */
char op3_t32_data_cd3fc[] = "core_dev->wqSwitchNormal";			/* data.c:16676 */
char op3_t32_data_cd418[] = "[ %s:%d ] Failed to init isp module(%d.%d)\n";	/* data.c:16683 */
char op3_t32_data_cd444[] = "&core_dev->mlock";				/* data.c:16694 */
char op3_t32_data_c3390[] = "isp_core_output_channel_init";			/* data.c:6407 */
char op3_t32_data_cd458[] = "[ %s:%d ] Failed to init output channels!\n";	/* data.c:16699 */
char op3_t32_data_cd484[] = "[ %s:%d ] Failed to init tuning module!\n";	/* data.c:16710 */
u64 tx_isp_core_probe(u32, u32, u32, u32);

#define OP3_FN(n, ours, ...) \
	int ours(__VA_ARGS__); \
	int ours(__VA_ARGS__) { return 0; } \
	__asm__(".globl op3_" #n "\n.set op3_" #n ", " #ours);
OP3_FN(ispcore_prepare_module, t32_tx_ispcore_prepare_module, u32 sd)
OP3_FN(ispcore_suspend_module, t32_tx_ispcore_suspend_module, u32 sd)
OP3_FN(ispcore_resume_early_module, t32_tx_ispcore_resume_early_module, u32 sd)
OP3_FN(ispcore_resume_module, t32_tx_ispcore_resume_module, u32 sd)
OP3_FN(ispcore_complete_module, t32_tx_ispcore_complete_module, u32 sd)
OP3_FN(isp_event_sleep_process, t32_tx_isp_event_sleep_process, u32 a)
OP3_FN(sensor_event_prepare_process, t32_tx_sensor_event_prepare_process, u32 a)
OP3_FN(sensor_event_suspend_process, t32_tx_sensor_event_suspend_process, u32 a)
OP3_FN(sensor_event_resume_process, t32_tx_sensor_event_resume_process, u32 a)
OP3_FN(sensor_event_complete_process, t32_tx_sensor_event_complete_process, u32 a)
OP3_FN(sensor_event_streamoff_process, t32_tx_sensor_event_streamoff_process, u32 a)
OP3_FN(ispcore_pad_event_handle, t32_tx_ispcore_pad_event_handle, u32 pad, u32 ev, u32 arg)

static u8 *arena;
static u32 A(u32 off) { return T32O_PTR(arena) + off; }
static u32 fnv(const char *s)
{
	u32 h = 2166136261u;

	while (*s)
		h = (h ^ (u8)*s++) * 16777619u;
	return h;
}
static u32 km_fail, n_km, cache_ok, init_ret, npad, st300, tune_ok;
static u32 km_rec(u32 n, u32 f)
{
	u32 k = n_km++;

	op3_call("private_kmalloc", 2, n, f, 0, 0);
	return (km_fail & (1u << k)) ? 0 : k ? A(O_CHANS) : A(O_CORE);
}
static u32 cache_rec(u32 c, u32 f) { op3_call("kmem_cache_alloc", 2, c, f, 0, 0); return cache_ok ? A(O_WQ) : 0; }
static u32 init_rec(u32 p, u32 sd, u32 ops)
{
	op3_call("tx_isp_subdev_init", 3, p, sd, ops, 0);
	*(u16 *)T32O_HOST(sd + 268) = (u16)npad;
	*(u32 *)T32O_HOST(sd + 272) = A(O_PADS);
	*(u32 *)T32O_HOST(sd + 300) = st300;
	return init_ret;
}
static void mutex_rec(u32 l, const char *n, u32 k) { op3_call("private_raw_mutex_init", 3, l, fnv(n), k, 0); }
static void wq_rec(u32 q, const char *n, u32 k) { op3_call("__init_waitqueue_head", 3, q, fnv(n), k, 0); }
static u32 tune_rec(u32 c) { op3_call("isp_core_tuning_init", 1, c, 0, 0, 0); return tune_ok ? A(O_TUNE) : 0; }

u64 op3_private_kmalloc(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return km_rec(a0, a1); }
u64 op3_memset(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; memset(T32O_HOST(a0), (int)a1, a2); return a0; }
u64 op3_kmem_cache_alloc(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return cache_rec(a0, a1); }
u64 op3___init_waitqueue_head(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; wq_rec(a0, T32O_HOST(a1), a2); return 0; }
u64 op3_tx_isp_subdev_init(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; return init_rec(a0, a1, a2); }
u64 op3_private_raw_mutex_init(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; mutex_rec(a0, T32O_HOST(a1), a2); return 0; }
u64 op3_isp_core_tuning_init(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; return tune_rec(a0); }
OP3_STUB(private_init_completion, 1)
OP3_STUB(isp_core_event_init, 1)
OP3_STUB(isp_core_event_set_cb, 3)
OP3_STUB(private_spin_lock_init, 1)
OP3_STUB(private_kfree, 1)
OP3_STUB(kfree, 1)
OP3_STUB(tx_isp_subdev_deinit, 1)
OP3_STUB(ispcore_slake_module, 1)
OP3_STUB(private_platform_set_drvdata, 2)
OP3_STUB(sensor_early_init, 1)

u32 t32_tx_os_kmalloc(u32 n, u32 f) { return km_rec(n, f); }
void t32_tx_os_kfree(u32 p) { op3_call("private_kfree", 1, p, 0, 0, 0); }
void t32_tx_os_kfree_plain(u32 p) { op3_call("kfree", 1, p, 0, 0, 0); }
void t32_tx_os_memset(u32 a, u32 c, u32 n) { memset(T32O_HOST(a), (int)c, n); }
void t32_tx_os_init_completion(u32 c) { op3_call("private_init_completion", 1, c, 0, 0, 0); }
void t32_tx_os_core_event_init(u32 a0) { op3_call("isp_core_event_init", 1, a0, 0, 0, 0); }
void t32_tx_os_core_event_set_cb(u32 a0, u32 ev, u32 fn) { op3_call("isp_core_event_set_cb", 3, a0, ev, fn, 0); }
u32 t32_tx_os_kmem_cache_alloc(u32 c, u32 f) { return cache_rec(c, f); }
void t32_tx_os_init_waitqueue_head_named(u32 q, const char *n, u32 k) { wq_rec(q, n, k); }
int t32_tx_os_subdev_init(u32 p, u32 sd, u32 ops) { return (int)init_rec(p, sd, ops); }
void t32_tx_os_subdev_deinit(u32 sd) { op3_call("tx_isp_subdev_deinit", 1, sd, 0, 0, 0); }
void t32_tx_os_spin_lock_init(u32 l) { op3_call("private_spin_lock_init", 1, l, 0, 0, 0); }
void t32_tx_os_raw_mutex_init(u32 l, const char *n, u32 k) { mutex_rec(l, n, k); }
u32 t32_tx_os_core_tuning_init(u32 c) { return tune_rec(c); }
void t32_tx_os_ispcore_slake_module(u32 c) { op3_call("ispcore_slake_module", 1, c, 0, 0, 0); }
void t32_tx_os_set_drvdata(u32 p, u32 d) { op3_call("private_platform_set_drvdata", 2, p, d, 0, 0); }
void t32_tx_os_sensor_early_init(u32 c) { op3_call("sensor_early_init", 1, c, 0, 0, 0); }

static u8 tmpl[ARENA];
static u32 iops0[8];
static struct t32_tx_core_env env;
static u64 ours(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4) { (void)a1; (void)a2; (void)a3; (void)a4; return (u32)t32_tx_core_probe(&env, a0); }

struct snap {
	struct op3_log log;
	u8 w[ARENA];
	u32 iops[8], g, ret;
};
static struct snap sv, so;

static void setup(void)
{
	t32o_reset();
	memset(&op3_log, 0, sizeof(op3_log));
	arena = t32o_alloc(ARENA);
	memcpy(arena, tmpl, ARENA);
	n_km = 0;
	memcpy(iops, iops0, sizeof(iops));
	op3_t32_data_d8c28[4] = T32O_PTR(iops);
}

int main(void)
{
	unsigned int i, k;
	u32 base, pm, g0;

	setup();
	base = A(0);
	for (i = 0; i < NCASE; i++) {
		u32 m = op3_rnd();

		for (k = 0; k < ARENA; k += 4)
			*(u32 *)(tmpl + k) = op3_rnd();
		*(u32 *)(tmpl + O_PDEV + 88) = base + O_PD;
		for (k = 0; k < 6; k++)
			tmpl[O_PADS + 36 * k + 5] = (op3_rnd() & 3) ? (u8)op3_rnd() : 0;
		for (k = 0; k < 8; k++)
			iops0[k] = op3_rnd();
		for (k = 0; k < 16; k++)
			op3_kmalloc_caches[k] = op3_rnd();
		pm = (m & 1) ? 0 : op3_rnd();
		km_fail = (m & 0xe) == 0 ? 1 : (m & 0xe) == 2 ? 2 : 0;
		cache_ok = (m & 0x70) != 0;
		init_ret = (m & 0x380) ? 0 : op3_rnd();
		npad = (m >> 10) % 7;
		st300 = (m & 0x2000) ? op3_rnd() % 4 : 0;
		tune_ok = (m & 0x1c000) != 0;
		g0 = op3_rnd();

		setup();
		op3_t32_data_d9004 = pm; op3_t32_data_f3310 = g0;
		sv.ret = OP3_VC(tx_isp_core_probe, base + O_PDEV, 0, 0, 0, 0);
		sv.log = op3_log;
		memcpy(sv.w, arena, ARENA);
		memcpy(sv.iops, iops, sizeof(iops));
		sv.g = op3_t32_data_f3310;

		setup();
		env.pm_on = pm; env.lock_key = T32O_PTR(op3_t32_small_bss); env.ops = T32O_PTR(op3_t32_data_d8c28);
		env.cfg = T32O_PTR(op3_t32_data_d8bbc); env.done = T32O_PTR(op3_t32_data_f331c);
		env.cache = op3_kmalloc_caches[7]; env.core = g0;
		so.ret = OP3_VC(ours, base + O_PDEV, 0, 0, 0, 0);
		so.log = op3_log;
		memcpy(so.w, arena, ARENA);
		memcpy(so.iops, iops, sizeof(iops));
		so.g = env.core;

		if (op3_log_cmp("core_probe", &sv.log, &so.log))
			return 1;
		if (sv.ret != so.ret || memcmp(sv.w, so.w, ARENA) || memcmp(sv.iops, so.iops, sizeof(sv.iops)) || sv.g != so.g) {
			u32 j;

			for (j = 0; j < ARENA && sv.w[j] == so.w[j]; j++)
				;
			fprintf(stderr, "core_probe: ret %x/%x, first world diff 0x%x\n", sv.ret, so.ret, j);
			return 1;
		}
	}
	printf("t32_txcprobe_oracle: %u cases equal\n", NCASE);
	return 0;
}
