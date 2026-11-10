// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: core glue (driver/t32/tx_isp_t32_txcore.h) against the
 * vendor tx_isp_release_device, _module_deinit, _reg_set,
 * _send_event_to_remote, _unlocked_ioctl_first, _notify, _module_init,
 * _open, _complete, _prepare, _resume_early, _resume, _suspend,
 * _unregister_platforms, _exit (Matt Davis' recovered C, PR #15 @ 2e2e06bc,
 * symbols renamed op3_* by op3_ren.sh).
 *
 * Per case one random world (device with its subdevice table, 20 subdevices
 * with ops / core / video / PM tables, clocks, names, a platform table, a
 * pad pair, a platform device, a register block, a file) is built at the
 * same address for both sides; callbacks are recorders that return a
 * scripted value per (slot, subdevice); 0 / ERR_PTR / valid table entries.
 * Compared: the event log (hooks, callbacks with their arguments, prints
 * with the subdevice names), every byte of the world, the return value
 * (void functions: none).  The notify pointer module_init stores is
 * compared through a map (vendor tx_isp_notify <-> our t32_tx_isp_notify).
 * Exit 0 = all equal, 1 = mismatch.
 */
#include "t32_op3_glue.h"
#include "tx_isp_t32_txcore.h"

#define NCASE	30000
#define ARENA	0x5000u
#define NSD	20u
#define O_SD(i)	(0x1000u + 0x200u * (i))
#define O_PLAT	0x4000u
#define O_PDRV(i) (0x4100u + 16u * (i))
#define O_PAD	0x4400u
#define O_REM	0x4440u
#define O_PDEV	0x4500u
#define O_PDATA	0x4600u
#define O_REG	0x4800u
#define O_FILE	0x4a00u

u32 op3_t32_data_da784, op3_t32_data_d9008;
u32 t32_tx_isp_dev, t32_tx_g_d9008;
u8 op3_t32_data_cf620[64], op3_t32_data_ce280[64];	/* platform driver / device objects (addresses compared) */
char op3_t32_data_c6c4c[] = "[%s %d] Enter the complete... \n";	/* data.c:10040 */
char op3_t32_data_be654[] = "tx_isp_complete";			/* data.c:1464 */
char op3_t32_data_c6c6c[] = "[%s %d] Enter the prepare... \n";	/* data.c:10048 */
char op3_t32_data_be644[] = "tx_isp_prepare";				/* data.c:1460 */
char op3_t32_data_c6cac[] = "[ %s:%d ] prepare %s\n";		/* data.c:10064 */
char op3_t32_data_c6c8c[] = "[ %s:%d ] Failed to prepare %s\n";	/* data.c:10056 */
char op3_t32_data_c6c00[] = "[%s %d] Enter the resume early... \n";	/* data.c:10021 */
char op3_t32_data_be684[] = "tx_isp_resume_early";			/* data.c:1476 */
char op3_t32_data_c6c24[] = "[ %s:%d ] Failed to resume early %s\n";	/* data.c:10030 */
char op3_t32_data_c6cdc[] = "[%s %d] Enter the resume ... \n";	/* data.c:10076 */
char op3_t32_data_be674[] = "tx_isp_resume";				/* data.c:1472 */
char op3_t32_data_c6cfc[] = "[ %s:%d ] Failed to suspend %s\n";	/* data.c:10084 */
char op3_t32_data_c6d1c[] = "[%s %d] Enter the suspend ... \n";	/* data.c:10092 */
char op3_t32_data_be664[] = "tx_isp_suspend";				/* data.c:1468 */
char op3_t32_data_c7344[] = "[ %s:%d ] the parameters are invalid!\n";	/* data.c:10486 */
char op3_t32_data_be70c[] = "tx_isp_module_init";			/* data.c:1510 */

u64 tx_isp_release_device(u32, u32, u32, u32);
u64 tx_isp_module_deinit(u32, u32, u32, u32);
u64 tx_isp_reg_set(u32, u32, u32, u32, u32);
u64 tx_isp_send_event_to_remote(u32, u32, u32, u32);
u64 tx_isp_unlocked_ioctl_first(u32, u32, u32, u32);
u64 tx_isp_notify(u32, u32, u32, u32);
u64 tx_isp_module_init(u32, u32, u32, u32);
u64 tx_isp_open(u32, u32, u32, u32);
u64 tx_isp_complete(u32, u32, u32, u32);
u64 tx_isp_prepare(u32, u32, u32, u32);
u64 tx_isp_resume_early(u32, u32, u32, u32);
u64 tx_isp_resume(u32, u32, u32, u32);
u64 tx_isp_suspend(u32, u32, u32, u32);
u64 tx_isp_unregister_platforms(u32, u32, u32, u32);
u64 tx_isp_exit(u32, u32, u32, u32);

static u8 *arena;
static u32 A(u32 off) { return T32O_PTR(arena) + off; }

/* ---- callbacks: scripted result per (slot, subdevice) ---- */
enum { S_CNOT, S_VNOT, S_OPEN, S_PREP, S_SUSP, S_REAR, S_RES, S_COMP, S_PREM, S_REMEV, S_N };
static u32 rtab[S_N][NSD + 1];
static const char *const sname[S_N] = {
	"core_notify", "video_notify", "pm_open", "pm_prepare", "pm_suspend", "pm_resume_early",
	"pm_resume", "pm_complete", "plat_remove", "remote_event"
};

static u32 sd_index(u32 sd)
{
	u32 i;

	for (i = 0; i < NSD; i++)
		if (sd == A(O_SD(i)))
			return i;
	return NSD;
}

static u32 cb(int slot, unsigned int n, u32 a0, u32 a1, u32 a2)
{
	op3_call(sname[slot], n, a0, a1, a2, 0);
	return rtab[slot][sd_index(a0)];
}

static u32 cb_cnot(u32 a0, u32 a1, u32 a2) { return cb(S_CNOT, 3, a0, a1, a2); }
static u32 cb_vnot(u32 a0, u32 a1, u32 a2) { return cb(S_VNOT, 3, a0, a1, a2); }
static u32 cb_open(u32 a0, u32 a1) { return cb(S_OPEN, 2, a0, a1, 0); }
static u32 cb_prep(u32 a0) { return cb(S_PREP, 1, a0, 0, 0); }
static u32 cb_susp(u32 a0) { return cb(S_SUSP, 1, a0, 0, 0); }
static u32 cb_rear(u32 a0) { return cb(S_REAR, 1, a0, 0, 0); }
static u32 cb_res(u32 a0) { return cb(S_RES, 1, a0, 0, 0); }
static u32 cb_comp(u32 a0) { return cb(S_COMP, 1, a0, 0, 0); }
static u32 cb_prem(u32 a0) { return cb(S_PREM, 1, a0, 0, 0); }
static u32 cb_remev(u32 a0, u32 a1, u32 a2) { return cb(S_REMEV, 3, a0, a1, a2); }

/* ---- hooks ---- */
static u32 ioctl_ret;
OP3_STUB(private_complete, 1)
OP3_STUB(private_clk_prepare_enable, 1)
OP3_STUB(private_clk_disable_unprepare, 1)
OP3_STUB(private_platform_device_unregister, 1)
OP3_STUB(private_platform_driver_unregister, 1)
OP3_STUB(tx_isp_riscv_run, 0)
OP3_STUB(tx_isp_riscv_stop, 0)
u64 op3_private_platform_get_drvdata(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	op3_call("private_platform_get_drvdata", 1, a0, 0, 0, 0);
	return A(0);
}
u64 op3_tx_isp_unlocked_ioctl(u32 a0, u32 a1, u32 a2, u32 a3)
{
	op3_call("tx_isp_unlocked_ioctl", 4, a0, a1, a2, a3);
	return ioctl_ret;
}
u64 op3_memcpy(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; memcpy(T32O_HOST(a0), T32O_HOST(a1), a2); return a0; }
u64 op3_memset(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; memset(T32O_HOST(a0), (int)a1, a2); return a0; }
u64 op3_tx_isp_notify(u32 a0, u32 a1, u32 a2, u32 a3) { return tx_isp_notify(a0, a1, a2, a3); }

void t32_tx_os_complete(u32 d) { op3_call("private_complete", 1, d, 0, 0, 0); }
u32 t32_tx_os_get_drvdata(u32 d) { return (u32)op3_private_platform_get_drvdata(d, 0, 0, 0); }
int t32_tx_os_unlocked_ioctl(u32 a0, u32 a1, u32 a2, u32 a3) { return (int)op3_tx_isp_unlocked_ioctl(a0, a1, a2, a3); }
void t32_tx_os_platform_driver_unregister(u32 d) { op3_call("private_platform_driver_unregister", 1, d, 0, 0, 0); }
void t32_tx_os_platform_device_unregister(u32 d) { op3_call("private_platform_device_unregister", 1, d, 0, 0, 0); }
void t32_tx_os_clk_prepare_enable(u32 c) { op3_call("private_clk_prepare_enable", 1, c, 0, 0, 0); }
void t32_tx_os_clk_disable_unprepare(u32 c) { op3_call("private_clk_disable_unprepare", 1, c, 0, 0, 0); }
int t32_tx_os_riscv_run(void) { return (int)op3_call("tx_isp_riscv_run", 0, 0, 0, 0, 0); }
int t32_tx_os_riscv_stop(void) { return (int)op3_call("tx_isp_riscv_stop", 0, 0, 0, 0, 0); }

/* ---- world ---- */
static u8 tmpl[ARENA];
static u32 tw(u32 off) { return *(u32 *)(tmpl + off); }
static void sw(u32 off, u32 v) { *(u32 *)(tmpl + off) = v; }

static u32 rfn(u32 m, void *fn) { return (m & 3) ? T32O_PTR(fn) : 0; }

static u32 small_or(u32 m)
{
	switch (m & 3) {
	case 0: return 0;
	case 1: return 1;
	case 2: return op3_rnd() & 7;
	default: return op3_rnd();
	}
}

/* pointers in the template are arena offsets + base: the arena has the same address on both sides */
static void build(u32 base)
{
	u32 i, k;

	for (k = 0; k < ARENA; k += 4)
		sw(k, op3_rnd());
	sw(T32_DEV_DONE_WAIT, small_or(op3_rnd()));
	sw(T32_DEV_OPENS, (op3_rnd() & 1) ? 0 : small_or(op3_rnd()));
	for (k = 72; k <= 148; k += 4) {
		u32 m = op3_rnd();

		sw(k, (m & 7) == 0 ? 0 : (m & 7) == 1 ? (u32)-T32_MAX_ERRNO + (op3_rnd() % T32_MAX_ERRNO)
			: base + O_SD(op3_rnd() % NSD));
	}
	for (i = 0; i < NSD; i++) {
		u32 sd = O_SD(i), ops = sd + 0x120, core = sd + 0x140, video = sd + 0x168, pm = sd + 0x190;
		u32 clks = sd + 0x1b0, name = sd + 0x1d0, m = op3_rnd();

		sw(sd + T32_SD_NAME, base + name);
		snprintf((char *)tmpl + name, 16, "sd%u", i);
		sw(sd + T32_SD_CLKS, base + clks);
		sw(sd + T32_SD_NCLK, (m & 0x30) ? op3_rnd() % 6 : ((m & 0x40) ? 0xffffffffu : 0));
		sw(sd + T32_SD_OPS, base + ops);
		sw(ops + T32_OPS_CORE, (m & 1) ? base + core : 0);
		sw(ops + T32_OPS_VIDEO, (m & 2) ? base + video : 0);
		sw(ops + T32_OPS_INTERNAL, (m & 4) ? 0 : base + pm);
		sw(core + T32_CORE_NOTIFY, rfn(op3_rnd(), (void *)cb_cnot));
		sw(video + T32_VIDEO_NOTIFY, rfn(op3_rnd(), (void *)cb_vnot));
		sw(pm + T32_PM_OPEN, rfn(op3_rnd(), (void *)cb_open));
		sw(pm + T32_PM_PREPARE, rfn(op3_rnd(), (void *)cb_prep));
		sw(pm + T32_PM_SUSPEND, rfn(op3_rnd(), (void *)cb_susp));
		sw(pm + T32_PM_RESUME_EARLY, rfn(op3_rnd(), (void *)cb_rear));
		sw(pm + T32_PM_RESUME, rfn(op3_rnd(), (void *)cb_res));
		sw(pm + T32_PM_COMPLETE, rfn(op3_rnd(), (void *)cb_comp));
	}
	for (i = 0; i < T32_PLAT_N; i++) {
		u32 m = op3_rnd();

		sw(O_PLAT + 8 * i, (m & 1) ? 0 : op3_rnd());
		sw(O_PLAT + 8 * i + 4, (m & 2) ? 0 : base + O_PDRV(i));
		sw(O_PDRV(i) + 4, T32O_PTR(cb_prem));
	}
	{
		u32 m = op3_rnd();

		sw(O_PAD + 12, (m & 3) ? base + O_REM : 0);
		sw(O_REM + 28, (m & 4) ? 0 : T32O_PTR(cb_remev));
		sw(O_PDEV + T32_PDEV_DATA, (m & 8) ? 0 : base + O_PDATA);
		sw(O_SD(1) + T32_SD_REGS, base + O_REG);
		sw(O_FILE + 120, base);
	}
	for (i = 0; i < S_N; i++)
		for (k = 0; k <= NSD; k++) {
			u32 m = op3_rnd() & 7;

			rtab[i][k] = m < 4 ? 0 : m < 6 ? (u32)-T32_ENOIOCTLCMD : op3_rnd();
		}
}

static void setup(void)
{
	t32o_reset();
	memset(&op3_log, 0, sizeof(op3_log));
	arena = t32o_alloc(ARENA);
	memcpy(arena, tmpl, ARENA);
}

enum { F_REL, F_DEINIT, F_REGSET, F_SEND, F_IOF, F_NOTIFY, F_INIT, F_OPEN, F_COMP, F_PREP, F_REAR, F_RES, F_SUSP,
       F_UNREG, F_EXIT, F_N };
static const char *const fname[F_N] = {
	"release_device", "module_deinit", "reg_set", "send_event_to_remote", "unlocked_ioctl_first", "notify",
	"module_init", "open", "complete", "prepare", "resume_early", "resume", "suspend", "unregister_platforms", "exit"
};
static const int has_ret[F_N] = { 1, 0, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 0 };

struct snap {
	struct op3_log log;
	u8 w[ARENA];
	u32 ret;
};
static struct snap sv, so;
static u32 x[5];

static void take(struct snap *s, u32 ret, int ours)
{
	u32 k;

	s->log = op3_log;
	memcpy(s->w, arena, ARENA);
	s->ret = ret;
	if (ours)
		for (k = 0; k < ARENA; k += 4)
			if (*(u32 *)(s->w + k) == T32_TX_A(t32_tx_isp_notify))
				*(u32 *)(s->w + k) = T32O_PTR(op3_tx_isp_notify);
}

static int run_case(int f, int d9008)
{
	u32 r = 0, sdx = x[0] & 1 ? A(O_SD(0)) : 0;

	setup();
	op3_t32_data_da784 = A(0);
	op3_t32_data_d9008 = (u32)d9008;
	switch (f) {
	case F_REL: r = OP3_VC(tx_isp_release_device, 0, 0, 0, 0, 0); break;
	case F_DEINIT: (void)OP3_VC(tx_isp_module_deinit, sdx, 0, 0, 0, 0); break;
	case F_REGSET: r = OP3_VC(tx_isp_reg_set, A(O_SD(1)), x[1], x[2], x[3], x[4]); break;
	case F_SEND: r = OP3_VC(tx_isp_send_event_to_remote, (x[0] & 6) ? A(O_PAD) : 0, x[1], x[2], x[3], 0); break;
	case F_IOF: r = OP3_VC(tx_isp_unlocked_ioctl_first, x[1], x[2], x[3], x[4], 0); break;
	case F_NOTIFY: r = OP3_VC(tx_isp_notify, x[4], x[1], x[2], 0, 0); break;
	case F_INIT: r = OP3_VC(tx_isp_module_init, A(O_PDEV), (x[0] & 6) ? A(O_SD(2)) : 0, 0, 0, 0); break;
	case F_OPEN: r = OP3_VC(tx_isp_open, x[1], A(O_FILE), 0, 0, 0); break;
	case F_COMP: (void)OP3_VC(tx_isp_complete, A(16), 0, 0, 0, 0); break;
	case F_PREP: r = OP3_VC(tx_isp_prepare, A(16), 0, 0, 0, 0); break;
	case F_REAR: r = OP3_VC(tx_isp_resume_early, A(16), 0, 0, 0, 0); break;
	case F_RES: r = OP3_VC(tx_isp_resume, A(16), 0, 0, 0, 0); break;
	case F_SUSP: r = OP3_VC(tx_isp_suspend, A(16), 0, 0, 0, 0); break;
	case F_UNREG: (void)OP3_VC(tx_isp_unregister_platforms, A(O_PLAT), 0, 0, 0, 0); break;
	case F_EXIT: (void)OP3_VC(tx_isp_exit, 0, 0, 0, 0, 0); break;
	}
	take(&sv, r, 0);

	setup();
	t32_tx_isp_dev = A(0);
	t32_tx_g_d9008 = (u32)d9008;
	r = 0;
	switch (f) {
	case F_REL: r = (u32)t32_tx_isp_release_device(); break;
	case F_DEINIT: t32_tx_isp_module_deinit(sdx); break;
	case F_REGSET: r = (u32)t32_tx_isp_reg_set(A(O_SD(1)), x[1], x[2], x[3], x[4]); break;
	case F_SEND: r = (u32)t32_tx_isp_send_event_to_remote((x[0] & 6) ? A(O_PAD) : 0, x[1], x[2]); break;
	case F_IOF: r = (u32)t32_tx_isp_unlocked_ioctl_first(x[1], x[2], x[4]); break;
	case F_NOTIFY: r = (u32)t32_tx_isp_notify(x[4], x[1], x[2]); break;
	case F_INIT: r = (u32)t32_tx_isp_module_init(A(O_PDEV), (x[0] & 6) ? A(O_SD(2)) : 0); break;
	case F_OPEN: r = (u32)t32_tx_isp_open(x[1], A(O_FILE)); break;
	case F_COMP: t32_tx_isp_complete(A(16)); break;
	case F_PREP: r = (u32)t32_tx_isp_prepare(A(16)); break;
	case F_REAR: r = (u32)t32_tx_isp_resume_early(A(16)); break;
	case F_RES: r = (u32)t32_tx_isp_resume(A(16)); break;
	case F_SUSP: r = (u32)t32_tx_isp_suspend(A(16)); break;
	case F_UNREG: t32_tx_isp_unregister_platforms(A(O_PLAT)); break;
	case F_EXIT: t32_tx_isp_exit(T32O_PTR(op3_t32_data_cf620), T32O_PTR(op3_t32_data_ce280)); break;
	}
	take(&so, r, 1);

	if (op3_log_cmp(fname[f], &sv.log, &so.log))
		return 1;
	if ((has_ret[f] && sv.ret != so.ret) || memcmp(sv.w, so.w, ARENA)) {
		u32 k;

		for (k = 0; k < ARENA && sv.w[k] == so.w[k]; k++)
			;
		fprintf(stderr, "%s: ret %x/%x, first world byte diff at 0x%x\n", fname[f], sv.ret, so.ret, k);
		return 1;
	}
	return 0;
}

int main(void)
{
	unsigned int i, cnt[F_N] = { 0 };
	u32 base;

	setup();
	base = A(0);
	for (i = 0; i < NCASE; i++) {
		int f = (int)(i % F_N);
		u32 m = op3_rnd(), ev;

		build(base);
		x[0] = op3_rnd();
		x[1] = op3_rnd();
		x[2] = op3_rnd();
		x[3] = op3_rnd();
		x[4] = op3_rnd();
		if (f == F_REGSET) {
			x[1] = (op3_rnd() % 64) * 4;		/* register offset in the block */
			x[2] = op3_rnd() % 34;			/* lo */
			x[3] = (m & 1) ? x[2] + op3_rnd() % 8 : op3_rnd() % 40;	/* hi (hi < lo - 1: no mask) */
		}
		if (f == F_NOTIFY) {
			static const u32 cls[6] = { 0x1000000, 0x2000000, 0x5000000, 0x3000000, 0, 0xff000000 };

			ev = cls[op3_rnd() % 6] | (op3_rnd() & 0xffffff);
			x[1] = (m & 0x10) ? T32_EV_DONE : ev;
		}
		if (f == F_NOTIFY || f == F_OPEN) {	/* the vendor walks of notify / open do not test ERR_PTR (crash): none */
			u32 k;

			for (k = 72; k <= 148; k += 4)
				if (T32_IS_ERR(tw(k)))
					sw(k, 0);
		}
		ioctl_ret = op3_rnd();
		if (run_case(f, (m & 0x100) != 0))
			return 1;
		cnt[f]++;
	}
	printf("t32_txcore_oracle: %u cases equal (%u functions)\n", NCASE, F_N);
	return 0;
}
