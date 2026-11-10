// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: VIC / VIN / CSI / LDC probes (driver/t32/
 * tx_isp_t32_txprobe.h) against the vendor tx_isp_vic_probe, _vin_probe,
 * _csi_probe, _ldc_probe (Matt Davis' recovered C, PR #15 @ 2e2e06bc,
 * symbols renamed op3_* by op3_ren.sh).
 *
 * Per case: scripted kmalloc / subdev_init / request_mem_region / ioremap /
 * misc_register results, PM global on / off, random platform data, ops
 * tables and config objects at fixed addresses shared by both sides.
 * Compared: the event log (hooks with arguments, lock names as hashes,
 * prints), the allocated devices, the ops tables (PM callbacks through a
 * map: vendor tx_isp_*_module <-> our t32_tx_*_module), the config copies,
 * the device globals, the return value.
 * Exit 0 = all equal, 1 = mismatch.
 */
#include "t32_op3_glue.h"
#include "tx_isp_t32_txprobe.h"

#define NCASE	30000
#define ARENA	0x2000u
#define O_DEV	0x000u
#define O_MISC	0xc00u
#define O_PDEV	0xd00u
#define O_PDATA	0xe00u
#define O_RES	0xf00u

/* vendor objects (data.c@2e2e06bc): ops tables { core, video, pad, sensor, internal, ... } and configs */
u32 op3_t32_data_ced7c[10], op3_t32_data_cef08[10], op3_t32_data_cf048[10], op3_t32_data_cf424[10];
u32 op3_t32_data_ced0c[4], op3_t32_data_cee9c[4], op3_t32_data_cefdc[4], op3_t32_data_cf3b8[4];
u32 op3_t32_data_ce22c[1], op3_t32_data_cf34c[8], op3_t32_data_d9004, op3_t32_data_d9348, op3_t32_data_d9680;
u32 op3_t32_data_d9944, op3_t32_data_d9948;
u8 op3_t32_small_bss[64];
static u32 iops[4][8];	/* the internal ops the tables point to */
char op3_t32_data_c393c[] = "Failed to allocate vic device\n";			/* data.c:6772 */
char op3_t32_data_c395c[] = "Failed to init isp module(%d.%d)\n";		/* data.c:6780 */
char op3_t32_data_c3980[] = "&vsd->mlock";					/* data.c:6789 */
char op3_t32_data_c398c[] = "&vsd->snap_mlock";				/* data.c:6792 */
char op3_t32_data_c422c[] = "ERROR: [ %s:%d ] Failed to allocate sensor subdev\n";	/* data.c:7344 */
char op3_t32_data_bda98[] = "tx_isp_vin_probe";				/* data.c:713 */
char op3_t32_data_c4260[] = "&vin->mlock";					/* data.c:7357 */
char op3_t32_data_c426c[] = "&vin->sen_mlock[i]";				/* data.c:7360 */
char op3_t32_data_c4280[] = "ERROR: [ %s:%d ] Failed to init isp module(%d.%d)\n";	/* data.c:7365 */
char op3_t32_data_c468c[] = "Failed to allocate csi device\n";			/* data.c:7624 */
char op3_t32_data_c46ac[] = "Failed to init isp module(%d.%d)\n";		/* data.c:7632 */
char op3_t32_data_c46d0[] = "mipi-phy";					/* data.c:7641 */
char op3_t32_data_bdb20[] = "tx_isp_csi_probe";				/* data.c:747 */
char op3_t32_data_c46dc[] = "%s[%d] Not enough memory for resources\n";	/* data.c:7644 */
char op3_t32_data_c4704[] = "&csd->mlock";					/* data.c:7654 */
char op3_t32_data_be4a8[] = "tx_isp_ldc_probe";				/* data.c:1357 */
char op3_t32_data_c5d9c[] = "[ %s:%d ] Failed to allocate sensor device\n";	/* data.c:9100 */
char op3_t32_data_c5dc8[] = "[ %s:%d ] Failed to allocate offline ldc device\n";	/* data.c:9111 */
char op3_t32_data_c5dfc[] = "[ %s:%d ] Failed to init isp module(%d.%d)\n";	/* data.c:9124 */
char op3_t32_data_c5e28[] = "&ldc_dev->mlock";				/* data.c:9135 */
char op3_t32_data_c5e38[] = "offline_ldc";					/* data.c:9139 */
char op3_t32_data_c5e44[] = "Failed to register offline_ldc!\n";		/* data.c:9142 */

u64 tx_isp_vic_probe(u32, u32, u32, u32);
u64 tx_isp_vin_probe(u32, u32, u32, u32);
u64 tx_isp_csi_probe(u32, u32, u32, u32);
u64 tx_isp_ldc_probe(u32, u32, u32, u32);

/* the vendor PM callbacks the probes store (addresses only) */
#define OP3_FN(n) u64 op3_##n(u32 a0, u32 a1, u32 a2, u32 a3); u64 op3_##n(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 0; }
OP3_FN(tx_isp_vic_suspend_module) OP3_FN(tx_isp_vic_resume_module)
OP3_FN(tx_isp_vin_prepare_module) OP3_FN(tx_isp_vin_suspend_module) OP3_FN(tx_isp_vin_resume_module)
OP3_FN(tx_isp_vin_complete_module) OP3_FN(tx_isp_csi_suspend_module) OP3_FN(tx_isp_csi_resume_module)

static u8 *arena;
static u32 A(u32 off) { return T32O_PTR(arena) + off; }
static u32 fnv(const char *s)
{
	u32 h = 2166136261u;

	while (*s)
		h = (h ^ (u8)*s++) * 16777619u;
	return h;
}

static u32 km_fail, n_km, init_ret, req_ok, ioremap_v, misc_ret;
static u32 km_rec(u32 n, u32 f)
{
	u32 k = n_km++;

	op3_call("private_kmalloc", 2, n, f, 0, 0);
	if (km_fail & (1u << k))
		return 0;
	return k ? A(O_MISC) : A(O_DEV);
}
static void mutex_rec(u32 l, const char *n, u32 key) { op3_call("private_raw_mutex_init", 3, l, fnv(n), key, 0); }
static u32 req_rec(u32 s, u32 n, const char *name)
{
	op3_call("private_request_mem_region", 3, s, n, fnv(name), 0);
	return req_ok ? A(O_RES) : 0;
}

OP3_STUB(private_kfree, 1)
OP3_STUB(private_platform_set_drvdata, 2)
OP3_STUB(private_spin_lock_init, 1)
OP3_STUB(private_init_completion, 1)
OP3_STUB(tx_isp_subdev_deinit, 1)
u64 op3_private_kmalloc(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return km_rec(a0, a1); }
u64 op3_memset(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; memset(T32O_HOST(a0), (int)a1, a2); return a0; }
u64 op3_tx_isp_subdev_init(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; op3_call("tx_isp_subdev_init", 3, a0, a1, a2, 0); return init_ret; }
u64 op3_private_raw_mutex_init(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; mutex_rec(a0, T32O_HOST(a1), a2); return 0; }
u64 op3_private_request_mem_region(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; return req_rec(a0, a1, T32O_HOST(a2)); }
u64 op3_private_ioremap(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; op3_call("private_ioremap", 2, a0, a1, 0, 0); return ioremap_v; }
u64 op3_private_misc_register(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; op3_call("private_misc_register", 1, a0, 0, 0, 0); return misc_ret; }

u32 t32_tx_os_kmalloc(u32 n, u32 f) { return km_rec(n, f); }
void t32_tx_os_kfree(u32 p) { op3_call("private_kfree", 1, p, 0, 0, 0); }
void t32_tx_os_memset(u32 a, u32 c, u32 n) { memset(T32O_HOST(a), (int)c, n); }
void t32_tx_os_set_drvdata(u32 p, u32 d) { op3_call("private_platform_set_drvdata", 2, p, d, 0, 0); }
int t32_tx_os_subdev_init(u32 p, u32 sd, u32 ops) { op3_call("tx_isp_subdev_init", 3, p, sd, ops, 0); return (int)init_ret; }
void t32_tx_os_spin_lock_init(u32 l) { op3_call("private_spin_lock_init", 1, l, 0, 0, 0); }
void t32_tx_os_raw_mutex_init(u32 l, const char *n, u32 k) { mutex_rec(l, n, k); }
void t32_tx_os_init_completion(u32 c) { op3_call("private_init_completion", 1, c, 0, 0, 0); }
u32 t32_tx_os_request_mem_region(u32 s, u32 n, const char *name) { return req_rec(s, n, name); }
u32 t32_tx_os_ioremap(u32 s, u32 n) { op3_call("private_ioremap", 2, s, n, 0, 0); return ioremap_v; }
int t32_tx_os_misc_register(u32 m) { op3_call("private_misc_register", 1, m, 0, 0, 0); return (int)misc_ret; }
void t32_tx_os_subdev_deinit(u32 sd) { op3_call("tx_isp_subdev_deinit", 1, sd, 0, 0, 0); }

/* hooks of the PM functions whose addresses the probes store (never called here) */
void t32_tx_isp_irq_handle(void) {}
void t32_tx_isp_irq_thread_handle(void) {}
u32 t32_tx_isp_dev, t32_tx_g_d9008;
void t32_tx_os_mutex_lock(u32 m) { (void)m; }
void t32_tx_os_mutex_unlock(u32 m) { (void)m; }
void t32_tx_os_vic_reset(void) {}
void t32_tx_os_vic_stream_on(void) {}
void t32_tx_os_vic_deinit(void) {}
int t32_tx_os_vic_init(u32 i, u32 a, void *m) { (void)i; (void)a; (void)m; return 0; }
void t32_tx_os_vic_ctrl(u32 a, u32 b) { (void)a; (void)b; }
int t32_tx_os_vic_set_par(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5) { (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; return 0; }
int t32_tx_os_vic_dma_start(void) { return 0; }
int t32_tx_os_vic_core_s_stream(u32 sd, void *c) { (void)sd; (void)c; return 0; }
int t32_tx_os_vic_core_ops_init(u32 sd, void *c) { (void)sd; (void)c; return 0; }
void t32_tx_os_gpio_request(u32 g, const char *l) { (void)g; (void)l; }
u32 t32_tx_os_gpio_to_desc(u32 g) { return g; }
u32 t32_tx_os_gpiod_get_raw_value(u32 d) { return d; }
void t32_tx_os_gpio_direction_output(u32 g, u32 v) { (void)g; (void)v; }
void t32_tx_os_gpio_free(u32 g) { (void)g; }
u32 t32_tx_os_spin_lock_irqsave(u32 l) { return l; }
void t32_tx_os_spin_unlock_irqrestore(u32 l, u32 f) { (void)l; (void)f; }
void t32_tx_os_clk_prepare_enable(u32 c) { (void)c; }
void t32_tx_os_clk_disable_unprepare(u32 c) { (void)c; }
int t32_tx_os_core_event_push(u32 a0, void *m) { (void)a0; (void)m; return 0; }
int t32_tx_os_vin_set_stream(u32 v, void *c, u32 on) { (void)v; (void)c; (void)on; return 0; }
int t32_tx_os_sensor_set_input(u32 v, void *c) { (void)v; (void)c; return 0; }
int t32_tx_os_sensor_release_all(u32 v) { (void)v; return 0; }
int t32_tx_os_mipi_phy_stream_on(u32 sd, void *c) { (void)sd; (void)c; return 0; }
int t32_tx_os_csi_video_s_stream(u32 sd, void *c) { (void)sd; (void)c; return 0; }
int t32_tx_os_csi_core_ops_init(u32 sd, void *c) { (void)sd; (void)c; return 0; }

/* ---- shared objects: reset per side, snapshot after ---- */
struct objs {
	u32 ops[4][10], cfg[4][4], iops[4][8], g[5];
};
static struct objs o0;
static u32 *const opsp[4] = { op3_t32_data_ced7c, op3_t32_data_cef08, op3_t32_data_cf048, op3_t32_data_cf424 };
static u32 *const cfgp[4] = { op3_t32_data_ced0c, op3_t32_data_cee9c, op3_t32_data_cefdc, op3_t32_data_cf3b8 };

static void objs_set(const struct objs *o)
{
	u32 k;

	for (k = 0; k < 4; k++) {
		memcpy(opsp[k], o->ops[k], sizeof(o->ops[k]));
		opsp[k][4] = T32O_PTR(iops[k]);
		memcpy(cfgp[k], o->cfg[k], sizeof(o->cfg[k]));
		memcpy(iops[k], o->iops[k], sizeof(o->iops[k]));
	}
	op3_t32_data_d9348 = o->g[0]; op3_t32_data_d9680 = o->g[1]; op3_t32_data_d9944 = o->g[2]; op3_t32_data_d9948 = o->g[3];
}

static void objs_get(struct objs *o, const u32 *g)
{
	u32 k;

	for (k = 0; k < 4; k++) {
		memcpy(o->ops[k], opsp[k], sizeof(o->ops[k]));
		memcpy(o->cfg[k], cfgp[k], sizeof(o->cfg[k]));
		memcpy(o->iops[k], iops[k], sizeof(o->iops[k]));
	}
	memcpy(o->g, g, 4 * sizeof(u32));
}

/* our PM callbacks / misc name -> the vendor's */
static void canon(u32 *w, u32 n)
{
	const u32 ours_fn[8] = {
		T32_TX_A(t32_tx_vic_suspend_module), T32_TX_A(t32_tx_vic_resume_module), T32_TX_A(t32_tx_vin_prepare_module),
		T32_TX_A(t32_tx_vin_suspend_module), T32_TX_A(t32_tx_vin_resume_module), T32_TX_A(t32_tx_vin_complete_module),
		T32_TX_A(t32_tx_csi_suspend_module), T32_TX_A(t32_tx_csi_resume_module)
	};
	const u32 vend_fn[8] = {
		T32O_PTR(op3_tx_isp_vic_suspend_module), T32O_PTR(op3_tx_isp_vic_resume_module), T32O_PTR(op3_tx_isp_vin_prepare_module),
		T32O_PTR(op3_tx_isp_vin_suspend_module), T32O_PTR(op3_tx_isp_vin_resume_module), T32O_PTR(op3_tx_isp_vin_complete_module),
		T32O_PTR(op3_tx_isp_csi_suspend_module), T32O_PTR(op3_tx_isp_csi_resume_module)
	};
	u32 i, k;

	for (i = 0; i < n; i++)
		for (k = 0; k < 8; k++)
			if (w[i] == ours_fn[k])
				w[i] = vend_fn[k];
}

static u8 tmpl[ARENA];
static struct t32_tx_probe_env env;
static int cur_f;
static u64 ours(u32 pdev, u32 a1, u32 a2, u32 a3, u32 a4)
{
	(void)a1; (void)a2; (void)a3; (void)a4;
	switch (cur_f) {
	case 0: return (u32)t32_tx_vic_probe(&env, pdev);
	case 1: return (u32)t32_tx_vin_probe(&env, pdev);
	case 2: return (u32)t32_tx_csi_probe(&env, pdev);
	default: return (u32)t32_tx_ldc_probe(&env, pdev);
	}
}

struct snap {
	struct op3_log log;
	u8 w[ARENA];
	struct objs o;
	u32 ret;
};
static struct snap sv, so;
static const char *const fname[4] = { "vic_probe", "vin_probe", "csi_probe", "ldc_probe" };
typedef u64 (*vfn)(u32, u32, u32, u32);
static const vfn vend[4] = { tx_isp_vic_probe, tx_isp_vin_probe, tx_isp_csi_probe, tx_isp_ldc_probe };

static void setup(void)
{
	t32o_reset();
	memset(&op3_log, 0, sizeof(op3_log));
	arena = t32o_alloc(ARENA);
	memcpy(arena, tmpl, ARENA);
	n_km = 0;
	objs_set(&o0);
}

int main(void)
{
	unsigned int i, k;
	u32 base, g[4], pm;

	setup();
	base = A(0);
	for (i = 0; i < NCASE; i++) {
		int f = (int)(i % 4);
		u32 m = op3_rnd();

		for (k = 0; k < ARENA; k += 4)
			*(u32 *)(tmpl + k) = op3_rnd();
		*(u32 *)(tmpl + O_PDEV + T32_PDEV_DATA) = base + O_PDATA;
		*(u32 *)(tmpl + O_RES) = op3_rnd();
		*(u32 *)(tmpl + O_RES + 4) = *(u32 *)(tmpl + O_RES) + op3_rnd() % 0x10000;
		for (k = 0; k < sizeof(o0) / 4; k++)
			((u32 *)&o0)[k] = op3_rnd();
		op3_t32_data_ce22c[0] = op3_rnd();
		pm = (m & 1) ? 0 : op3_rnd();
		km_fail = (m & 6) == 0 ? 1 : (m & 6) == 2 ? 2 : 0;
		init_ret = (m & 0x18) == 0 ? op3_rnd() | 1 : 0;
		req_ok = (m & 0x60) != 0;
		ioremap_v = op3_rnd();
		misc_ret = (m & 0x80) ? (u32)-(int)(op3_rnd() % 30 + 1) : op3_rnd() & 0x7fffffff;

		setup();
		op3_t32_data_d9004 = pm;
		sv.ret = OP3_VC(vend[f], base + O_PDEV, 0, 0, 0, 0);
		sv.log = op3_log;
		memcpy(sv.w, arena, ARENA);
		g[0] = op3_t32_data_d9348; g[1] = op3_t32_data_d9680; g[2] = op3_t32_data_d9944; g[3] = op3_t32_data_d9948;
		objs_get(&sv.o, g);

		setup();
		env.pm_on = pm;
		env.lock_key = T32O_PTR(op3_t32_small_bss);
		env.cfg_src = T32O_PTR(op3_t32_data_ce22c);
		env.vic_ops = T32O_PTR(op3_t32_data_ced7c); env.vic_cfg = T32O_PTR(op3_t32_data_ced0c);
		env.vin_ops = T32O_PTR(op3_t32_data_cef08); env.vin_cfg = T32O_PTR(op3_t32_data_cee9c);
		env.csi_ops = T32O_PTR(op3_t32_data_cf048); env.csi_cfg = T32O_PTR(op3_t32_data_cefdc);
		env.ldc_ops = T32O_PTR(op3_t32_data_cf424); env.ldc_cfg = T32O_PTR(op3_t32_data_cf3b8);
		env.ldc_fops = T32O_PTR(op3_t32_data_cf34c);
		env.vic_dev = o0.g[0]; env.csi_dev = o0.g[1]; env.ldc_dev = o0.g[2]; env.ldc_misc = o0.g[3];
		cur_f = f;
		so.ret = OP3_VC(ours, base + O_PDEV, 0, 0, 0, 0);
		so.log = op3_log;
		memcpy(so.w, arena, ARENA);
		g[0] = env.vic_dev; g[1] = env.csi_dev; g[2] = env.ldc_dev; g[3] = env.ldc_misc;
		objs_get(&so.o, g);
		canon(&so.o.iops[0][0], 4 * 8);
		/* the misc device name: the same string */
		if (f == 3 && *(u32 *)(so.w + O_MISC + 4) == T32_TX_A("offline_ldc") &&
		    *(u32 *)(sv.w + O_MISC + 4) == T32O_PTR(op3_t32_data_c5e38))
			*(u32 *)(so.w + O_MISC + 4) = T32O_PTR(op3_t32_data_c5e38);

		if (op3_log_cmp(fname[f], &sv.log, &so.log))
			return 1;
		if (sv.ret != so.ret || memcmp(sv.w, so.w, ARENA) || memcmp(&sv.o, &so.o, sizeof(sv.o))) {
			fprintf(stderr, "%s: ret %x/%x, world / objects differ\n", fname[f], sv.ret, so.ret);
			return 1;
		}
	}
	printf("t32_txprobe_oracle: %u cases equal (4 functions)\n", NCASE);
	return 0;
}
