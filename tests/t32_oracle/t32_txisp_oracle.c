// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: ISP platform probe (driver/t32/tx_isp_t32_txisp.h)
 * against the vendor tx_isp_probe (Matt Davis' recovered C, PR #15 @
 * 2e2e06bc, symbols renamed op3_* by op3_ren.sh; its tx_isp_module_init /
 * _module_deinit / _unregister_platforms / _create_graph_and_nodes calls
 * run the vendor functions, ours our ports).
 *
 * Per case: 0..6 (or 17+) module devices named after the 7 drivers or
 * not, drivers with probe hooks or 0, scripted kmalloc / device register
 * / misc / proc / graph (drvdata) / sensor init / start hook results,
 * d9008 0..3.  Two vendor prints take stale stack words (device number,
 * sensor name): those arguments are not compared.  Function pointers and
 * the misc name / version strings are compared through maps.  Compared:
 * the event log, every world byte, the driver objects, the globals, the
 * return value.
 * Exit 0 = all equal, 1 = mismatch.
 */
#include "t32_op3_glue.h"
#include "tx_isp_t32_txisp.h"

#define NCASE	20000
#define ARENA	0x3000u
#define O_PDEV	0x000u
#define O_PD	0x100u
#define O_DEVT	0x140u		/* device table */
#define O_MOD(i) (0x200u + 0x40u * (i))	/* module devices: +0 name */
#define O_ISP	0x400u		/* 568 */
#define O_SD(i)	(0x800u + 0x100u * (i))	/* drvdata of the graph walk */
#define O_STR	0x1000u
#define O_DRV(i) (0x1400u + 0x40u * (i))
#define O_SNS	0x1800u		/* 4 x 40 */

u32 op3_t32_data_d9008, op3_t32_data_da784, op3_t32_data_f35c0, op3_t32_data_f3760, op3_t32_data_f3764, op3_t32_data_f3834;
u32 op3_t32_data_bed60[7], op3_t32_data_ce22c[1], op3_t32_data_cf6d8[8], op3_t32_data_f384c[40];
u32 t32_tx_isp_dev, t32_tx_g_d9008;
void t32_tx_isp_irq_handle(void) {}
void t32_tx_isp_irq_thread_handle(void) {}
char op3_t32_data_be634[] = "tx_isp_probe";						/* data.c:1456 */
char op3_t32_data_c7570[] = "[ %s:%d ] Failed to allocate camera device\n";		/* data.c:10625 */
char op3_t32_data_be610[] = "tx_isp_match_and_register_platforms";			/* data.c:1447 */
char op3_t32_data_c759c[] = "[ %s:%d ] The number of platform device is too much!\n";	/* data.c:10636 */
char op3_t32_data_c75d4[] = "[ %s:%d ] Failed to register isp device(%d)\n";		/* data.c:10650 */
char op3_t32_data_c7604[] = "[ %s:%d ] Failed to init isp module(%d.%d)\n";		/* data.c:10662 */
char op3_t32_data_c7630[] = "tx-isp";							/* data.c:10673 */
char op3_t32_data_c7638[] = "[ %s:%d ] Failed to register tx-isp miscdev!\n";		/* data.c:10675 */
char op3_t32_data_c7668[] = "isp";							/* data.c:10687 */
char op3_t32_data_c766c[] = "[ %s:%d ] Failed to create debug directory of tx-isp!\n";	/* data.c:10688 */
char op3_t32_data_c76a4[] = "H20260618a";						/* data.c:10702 */
char op3_t32_data_c76fc[] = "14:41:04";						/* data.c:10724 */
char op3_t32_data_c76b0[] = "Jun 23 2026";						/* data.c:10705 */
char op3_t32_data_c76bc[] = "@@@@ tx-isp-probe ok (version %s), compiler date: %s %s @@@@@\n";	/* data.c:10708 */
char op3_t32_data_c7708[] = "LIB[%s.%d]sensor id:%d sensor name:%s\n";		/* data.c:10727 */
char op3_t32_data_c7730[] = "init_sensor %d failed\n";				/* data.c:10737 */
char op3_t32_data_c7748[] = "LIB:[%s.%d]use_num_sensor = %d \n";			/* data.c:10743 */
char op3_t32_data_c776c[] = "init_sensor_one failed\n";				/* data.c:10752 */
char op3_t32_data_c7784[] = "start_tx_isp_device failed\n";				/* data.c:10758 */
char op3_t32_data_be6a4[] = "tx_isp_create_graph_and_nodes";				/* data.c:1484 */
char op3_t32_data_c74d4[] = "[ %s:%d ] Can't find the module(%d) driver!\n";		/* data.c:10586 */
char op3_t32_data_c7504[] = "[ %s:%d ] the module(%d.%d) doesn't have parent!\n";	/* data.c:10598 */
char op3_t32_data_c7538[] = "[ %s:%d ] Failed to register tx-isp miscdev(%d.%d)!\n";	/* data.c:10611 */
char op3_t32_data_c7344[] = "[ %s:%d ] the parameters are invalid!\n";		/* data.c:10486 */
char op3_t32_data_be70c[] = "tx_isp_module_init";					/* data.c:1510 */

u64 tx_isp_probe(u32, u32, u32, u32);
u64 tx_isp_module_init(u32, u32, u32, u32);
u64 tx_isp_module_deinit(u32, u32, u32, u32);
u64 tx_isp_unregister_platforms(u32, u32, u32, u32);
u64 tx_isp_create_graph_and_nodes(u32, u32, u32, u32);

static u8 *arena;
static u32 A(u32 off) { return T32O_PTR(arena) + off; }
static u32 fnv(const char *s)
{
	u32 h = 2166136261u;

	while (*s)
		h = (h ^ (u8)*s++) * 16777619u;
	return h;
}

static u32 km_fail, reg_ret[8], n_reg, misc_ret[8], n_misc, proc_v, sns_ret[4], hook_ret, chose_ret, drv_map[8];
static u32 km_rec(u32 n, u32 f) { op3_call("private_kmalloc", 2, n, f, 0, 0); return km_fail ? 0 : A(O_ISP); }
static u32 reg_rec(u32 p) { op3_call("private_platform_device_register", 1, p, 0, 0, 0); return reg_ret[n_reg++ & 7]; }
static u32 misc_rec(u32 m) { op3_call("private_misc_register", 1, m, 0, 0, 0); return misc_ret[n_misc++ & 7]; }
static u32 drv_rec(u32 p) { op3_call("private_platform_get_drvdata", 1, p, 0, 0, 0); return A(O_SD(drv_map[(p / 64) & 7] % 4)); }
static u32 proc_rec(u32 n, u32 mode, u32 par, u32 fops, u32 data)
{
	u32 a[5] = { fnv(T32O_HOST(n)), mode, par, fops, data };

	op3_callv("private_proc_create_data", 5, a);
	return 0;
}
static u32 dprobe_rec(u32 p) { return op3_call("drv_probe", 1, p, 0, 0, 0); }
static u32 dremove_rec(u32 p) { return op3_call("drv_remove", 1, p, 0, 0, 0); }
static u32 sns_rec(int k) { op3_call("sensor_init", 1, (u32)k, 0, 0, 0); return sns_ret[k]; }
static u32 sns0(void) { return sns_rec(0); }
static u32 sns1(void) { return sns_rec(1); }
static u32 sns2(void) { return sns_rec(2); }
static u32 sns3(void) { return sns_rec(3); }
static u32 hook_rec(void) { op3_call("start_hook", 0, 0, 0, 0, 0); return hook_ret; }

u64 op3_private_kmalloc(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return km_rec(a0, a1); }
u64 op3_memset(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; memset(T32O_HOST(a0), (int)a1, a2); return a0; }
u64 op3_memcpy(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; memcpy(T32O_HOST(a0), T32O_HOST(a1), a2); return a0; }
u64 op3_private_platform_device_register(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; return reg_rec(a0); }
u64 op3_private_misc_register(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; return misc_rec(a0); }
u64 op3_private_platform_get_drvdata(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; return drv_rec(a0); }
u64 op3_private_proc_create_data(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4);
u64 op3_private_proc_create_data(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4) { return proc_rec(a0, a1, a2, a3, a4); }
u64 op3_private_jz_proc_mkdir(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; op3_call("private_jz_proc_mkdir", 1, fnv(T32O_HOST(a0)), 0, 0, 0); return proc_v; }
u64 op3_add_sensor_chose(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; op3_call("add_sensor_chose", 0, 0, 0, 0, 0); return chose_ret; }
OP3_STUB(private_kfree, 1)
OP3_STUB(private_spin_lock_init, 1)
OP3_STUB(private_platform_set_drvdata, 2)
OP3_STUB(private_proc_remove, 1)
OP3_STUB(private_misc_deregister, 1)
OP3_STUB(private_platform_device_unregister, 1)
OP3_STUB(isp_mem_init, 0)
OP3_STUB(tx_isp_riscv_probe, 0)
OP3_STUB(tx_isp_riscv_run, 0)
u64 op3_tx_isp_notify(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 0; }
u64 op3_tx_isp_module_init(u32 a0, u32 a1, u32 a2, u32 a3) { return tx_isp_module_init(a0, a1, a2, a3); }
u64 op3_tx_isp_module_deinit(u32 a0, u32 a1, u32 a2, u32 a3) { return tx_isp_module_deinit(a0, a1, a2, a3); }
u64 op3_tx_isp_unregister_platforms(u32 a0, u32 a1, u32 a2, u32 a3) { return tx_isp_unregister_platforms(a0, a1, a2, a3); }
u64 op3_tx_isp_create_graph_and_nodes(u32 a0, u32 a1, u32 a2, u32 a3) { return tx_isp_create_graph_and_nodes(a0, a1, a2, a3); }

u32 t32_tx_os_kmalloc(u32 n, u32 f) { return km_rec(n, f); }
void t32_tx_os_kfree(u32 p) { op3_call("private_kfree", 1, p, 0, 0, 0); }
void t32_tx_os_memset(u32 a, u32 c, u32 n) { memset(T32O_HOST(a), (int)c, n); }
int t32_tx_os_platform_device_register(u32 p) { return (int)reg_rec(p); }
int t32_tx_os_platform_driver_register(u32 p) { (void)p; return 0; }
void t32_tx_os_platform_device_unregister(u32 p) { op3_call("private_platform_device_unregister", 1, p, 0, 0, 0); }
void t32_tx_os_platform_driver_unregister(u32 p) { (void)p; }
u32 t32_tx_os_proc_create_data(const char *n, u32 m, u32 p, u32 f, u32 d) { (void)n; (void)m; (void)p; (void)f; (void)d; return 0; }
void t32_tx_os_spin_lock_init(u32 l) { op3_call("private_spin_lock_init", 1, l, 0, 0, 0); }
void t32_tx_os_set_drvdata(u32 p, u32 d) { op3_call("private_platform_set_drvdata", 2, p, d, 0, 0); }
u32 t32_tx_os_jz_proc_mkdir(const char *n) { op3_call("private_jz_proc_mkdir", 1, fnv(n), 0, 0, 0); return proc_v; }
void t32_tx_os_proc_remove(u32 p) { op3_call("private_proc_remove", 1, p, 0, 0, 0); }
void t32_tx_os_isp_mem_init(void) { op3_call("isp_mem_init", 0, 0, 0, 0, 0); }
int t32_tx_os_add_sensor_chose(void) { return (int)op3_add_sensor_chose(0, 0, 0, 0); }
void t32_tx_os_riscv_probe(void) { op3_call("tx_isp_riscv_probe", 0, 0, 0, 0, 0); }
int t32_tx_os_riscv_run(void) { return (int)op3_call("tx_isp_riscv_run", 0, 0, 0, 0, 0); }
u32 t32_tx_os_get_drvdata(u32 p) { return drv_rec(p); }
int t32_tx_os_misc_register(u32 m) { return (int)misc_rec(m); }
void t32_tx_os_misc_deregister(u32 m) { op3_call("private_misc_deregister", 1, m, 0, 0, 0); }
u32 t32_tx_os_proc_create_data_u(u32 n, u32 m, u32 p, u32 f, u32 d) { return proc_rec(n, m, p, f, d); }
void t32_tx_os_complete(u32 d) { (void)d; }
int t32_tx_os_unlocked_ioctl(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 0; }
void t32_tx_os_clk_prepare_enable(u32 c) { (void)c; }
void t32_tx_os_clk_disable_unprepare(u32 c) { (void)c; }
int t32_tx_os_riscv_stop(void) { return 0; }
void t32_tx_os_enable_irq(u32 i) { (void)i; }
void t32_tx_os_disable_irq(u32 i) { (void)i; }
void t32_tx_os_free_irq(u32 i, u32 d) { (void)i; (void)d; }
u32 t32_tx_os_spin_lock_irqsave(u32 l) { return l; }
void t32_tx_os_spin_unlock_irqrestore(u32 l, u32 f) { (void)l; (void)f; }
void t32_tx_os_vic_intp_enable(u32 i) { (void)i; }
void t32_tx_os_vic_intp_disable(u32 i) { (void)i; }

static u8 tmpl[ARENA];
static void sw(u32 off, u32 v) { *(u32 *)(tmpl + off) = v; }
static const char *const dname[7] = { "isp-w00", "isp-w01", "isp-w02", "isp-m0", "isp-w03", "isp-w04", "isp-m1" };
static u32 drv0[7][16];

static void build(u32 base)
{
	u32 k, m = op3_rnd(), n = (m & 0xf) == 0 ? 16 + op3_rnd() % 3 : op3_rnd() % 7;

	for (k = 0; k < ARENA; k += 4)
		sw(k, op3_rnd());
	sw(O_PDEV + T32_PDEV_DATA, (m & 0x30) == 0 ? 0 : base + O_PD);
	sw(O_PDEV + 0, base + O_STR + 0x200);
	strcpy((char *)tmpl + O_STR + 0x200, "tx-isp-pdev");
	tmpl[O_PD + 4] = (u8)n;
	sw(O_PD + 8, base + O_DEVT);
	for (k = 0; k < 7; k++)
		strcpy((char *)tmpl + O_STR + 16 * k, dname[k]);
	for (k = 0; k < 16; k++)
		sw(O_DEVT + 4 * k, base + O_MOD(k % 6));
	strcpy((char *)tmpl + O_STR + 0x100, "isp-none");
	for (k = 0; k < 6; k++)
		sw(O_MOD(k), base + O_STR + ((op3_rnd() & 3) ? 16 * (op3_rnd() % 7) : 0x100));
	for (k = 0; k < 7; k++) {
		u32 j;

		for (j = 0; j < 16; j++)
			drv0[k][j] = op3_rnd();
		drv0[k][0] = (op3_rnd() & 1) ? T32O_PTR(dprobe_rec) : 0;
		drv0[k][1] = T32O_PTR(dremove_rec);
		drv0[k][5] = base + O_STR + 16 * k;
		op3_t32_data_bed60[k] = base + O_DRV(k);
	}
	for (k = 0; k < 4; k++) {	/* graph drvdata: type 0 (no links), no misc / proc */
		tmpl[O_SD(k) + 0] = (u8)(op3_rnd() % 3 == 0 ? 3 : 0);
		sw(O_SD(k) + T32_SD_MISC_FOPS, (op3_rnd() & 3) ? 0 : op3_rnd());
		sw(O_SD(k) + T32_SD_PROC_FOPS, (op3_rnd() & 1) ? 0 : op3_rnd());
		sw(O_SD(k) + T32_SD_NAME, base + O_STR);
		sw(O_SNS + 40 * k + 32, k == 0 ? T32O_PTR(sns0) : k == 1 ? T32O_PTR(sns1) : k == 2 ? T32O_PTR(sns2) : T32O_PTR(sns3));
		sns_ret[k] = (op3_rnd() & 7) ? 0 : (u32)-(int)(op3_rnd() % 9 + 1);
	}
	for (k = 0; k < 8; k++) {
		reg_ret[k] = (op3_rnd() & 15) ? 0 : op3_rnd();
		misc_ret[k] = (op3_rnd() & 15) ? op3_rnd() & 0xff : (u32)-(int)(op3_rnd() % 9 + 1);
		drv_map[k] = op3_rnd();
	}
	km_fail = (op3_rnd() & 31) == 0;
	proc_v = (op3_rnd() & 15) ? op3_rnd() | 1 : 0;
	hook_ret = (op3_rnd() & 7) ? 0 : (u32)-(int)(op3_rnd() % 9 + 1);
	chose_ret = (op3_rnd() & 7) ? 0 : (u32)-(int)(op3_rnd() % 9 + 1);
}

static void setup(void)
{
	u32 k;

	t32o_reset();
	memset(&op3_log, 0, sizeof(op3_log));
	arena = t32o_alloc(ARENA);
	memcpy(arena, tmpl, ARENA);
	n_reg = n_misc = 0;
	for (k = 0; k < 7; k++)
		memcpy(T32O_HOST(A(O_DRV(k))), drv0[k], sizeof(drv0[k]));
}

static struct t32_tx_isp_env env;
static u64 ours(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4) { (void)a1; (void)a2; (void)a3; (void)a4; return (u32)t32_tx_isp_probe(&env, a0); }

static void canon(u8 *w)
{
	u32 k;

	for (k = 0; k < ARENA; k += 4) {
		u32 *p = (u32 *)(w + k);

		if (*p == T32_TX_A(t32_tx_isp_notify))
			*p = T32O_PTR(op3_tx_isp_notify);
		else if (*p == T32_TX_A("tx-isp"))
			*p = T32O_PTR(op3_t32_data_c7630);
		else if (*p == T32_TX_A("H20260618a"))
			*p = T32O_PTR(op3_t32_data_c76a4);
	}
}

struct snap {
	struct op3_log log;
	u8 w[ARENA];
	u32 g[2], ret;
};
static struct snap sv, so;

int main(void)
{
	unsigned int i;
	u32 base, d9008, nsns, one, g0[2], hook_on;

	op3_mask_fmt = op3_t32_data_c75d4;
	op3_mask_args = 1u << 2;
	op3_mask_fmt2 = op3_t32_data_c7708;
	op3_mask_args2 = 0x20000u;
	setup();
	base = A(0);
	for (i = 0; i < NCASE; i++) {
		build(base);
		d9008 = op3_rnd() % 4;
		hook_on = op3_rnd() & 1;
		nsns = (op3_rnd() & 1) ? op3_rnd() % 5 : 0;
		one = op3_rnd();
		g0[0] = op3_rnd(); g0[1] = op3_rnd();
		op3_t32_data_ce22c[0] = op3_rnd();

		setup();
		op3_t32_data_d9008 = d9008; op3_t32_data_f3760 = nsns; op3_t32_data_f3764 = one;
		op3_t32_data_f3834 = hook_on ? T32O_PTR(hook_rec) : 0;
		memcpy(op3_t32_data_f384c, arena + O_SNS, sizeof(op3_t32_data_f384c));
		op3_t32_data_da784 = g0[0]; op3_t32_data_f35c0 = g0[1];
		sv.ret = OP3_VC(tx_isp_probe, base + O_PDEV, 0, 0, 0, 0);
		sv.log = op3_log;
		memcpy(sv.w, arena, ARENA);
		sv.g[0] = op3_t32_data_da784; sv.g[1] = op3_t32_data_f35c0;

		setup();
		env.fast_start = d9008; env.drivers = T32O_PTR(op3_t32_data_bed60); env.owner = T32O_PTR(op3_t32_data_ce22c);
		env.fops = T32O_PTR(op3_t32_data_cf6d8); env.nsensor = nsns; env.sensors = T32O_PTR(op3_t32_data_f384c);
		env.sensor_one = one; env.start_hook = hook_on ? T32O_PTR(hook_rec) : 0;
		env.isp = g0[0]; env.fast_isp = g0[1];
		t32_tx_g_d9008 = d9008;
		so.ret = OP3_VC(ours, base + O_PDEV, 0, 0, 0, 0);
		so.log = op3_log;
		memcpy(so.w, arena, ARENA);
		canon(so.w);
		so.g[0] = env.isp; so.g[1] = env.fast_isp;

		if (op3_log_cmp("tx_isp_probe", &sv.log, &so.log))
			return 1;
		if (sv.ret != so.ret || memcmp(sv.w, so.w, ARENA) || memcmp(sv.g, so.g, sizeof(sv.g))) {
			u32 j;

			for (j = 0; j < ARENA && sv.w[j] == so.w[j]; j++)
				;
			fprintf(stderr, "tx_isp_probe: ret %x/%x, first world diff 0x%x, g %x/%x %x/%x\n", sv.ret, so.ret, j,
				sv.g[0], so.g[0], sv.g[1], so.g[1]);
			return 1;
		}
	}
	printf("t32_txisp_oracle: %u cases equal\n", NCASE);
	return 0;
}
