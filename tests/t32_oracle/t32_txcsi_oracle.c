// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: CSI glue (driver/t32/tx_isp_t32_txcsi.h) against the
 * vendor tx_isp_csi_suspend_module, _resume_module, _core_ops_ioctl,
 * _activate_subdev, _slake_subdev (Matt Davis' recovered C, PR #15 @
 * 2e2e06bc, symbols renamed op3_* by op3_ren.sh).
 *
 * Per case one random world (subdevice: NULL / ERR_PTR / valid, CSI device
 * pointer NULL / ERR_PTR / valid, input states 0..5, clocks NULL / ERR_PTR /
 * array with 0..5 entries, PHY reset count, scripted PHY results); both
 * sides run on the low stack.  Compared: the event log (locks, clocks,
 * stream / init / PHY calls with their control words, prints), every world
 * byte, the return value.
 * Exit 0 = all equal, 1 = mismatch.
 */
#include "t32_op3_glue.h"
#include "tx_isp_t32_txcsi.h"

#define NCASE	30000
#define ARENA	0x1000u
#define O_CSI	0x400u
#define O_CLK	0x200u

char op3_t32_data_bdae8[] = "tx_isp_csi_suspend_module";		/* data.c:733 */
char op3_t32_data_c4768[] = "\n [%s %d] CSI is null \n";		/* data.c:7679 */
char op3_t32_data_c4780[] = "[%s %d] Enter the suspend ... \n";	/* data.c:7685 */
char op3_t32_data_bdb04[] = "tx_isp_csi_resume_module";		/* data.c:740 */
char op3_t32_data_c4848[] = "[%s %d] Enter the resume ... \n";		/* data.c:7735 */
char op3_t32_data_c4868[] = "\n [%s %d] Fail to csi resume \n";	/* data.c:7743 */
char op3_t32_data_bdad0[] = "tx_isp_csi_pm_streamon";			/* data.c:727 */
char op3_t32_data_c47f4[] = "\n [%s %d] Fail to csi stream on \n";	/* data.c:7714 */
char op3_t32_data_c4818[] = "[%s %d] csi is streamon [csd->phy_rstz %d]!!!\n";	/* data.c:7723 */

u64 tx_isp_csi_suspend_module(u32, u32, u32, u32);
u64 tx_isp_csi_resume_module(u32, u32, u32, u32);
u64 tx_isp_csi_core_ops_ioctl(u32, u32, u32, u32);
u64 tx_isp_csi_activate_subdev(u32, u32, u32, u32);
u64 tx_isp_csi_slake_subdev(u32, u32, u32, u32);

static u8 *arena;
static u32 A(u32 off) { return T32O_PTR(arena) + off; }
static u32 w(u32 p, u32 k) { return ((const u32 *)T32O_HOST(p))[k]; }
static u32 phy_ret, flags_v;

static u32 phy_rec(u32 sd, u32 c) { op3_call("mipi_phy_stream_on", 3, sd, w(c, 0), w(c, 1), 0); return phy_ret; }
static u32 sstream_rec(u32 sd, u32 c) { return op3_call("csi_video_s_stream", 3, sd, w(c, 0), w(c, 1), 0); }
static u32 cinit_rec(u32 sd, u32 c) { return op3_call("csi_core_ops_init", 3, sd, w(c, 0), w(c, 1), 0); }
static u32 lock_rec(u32 l) { op3_call("spin_lock_irqsave", 1, l, 0, 0, 0); return flags_v; }

OP3_STUB(private_spin_unlock_irqrestore, 2)
OP3_STUB(private_mutex_unlock, 1)
OP3_STUB(private_clk_prepare_enable, 1)
OP3_STUB(private_clk_disable_unprepare, 1)
u64 op3_mipi_phy_stream_on_constprop_5(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return phy_rec(a0, a1); }
u64 op3_csi_video_s_stream(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return sstream_rec(a0, a1); }
u64 op3_csi_core_ops_init(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return cinit_rec(a0, a1); }
u64 op3___private_spin_lock_irqsave(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	*(u32 *)T32O_HOST(a1) = lock_rec(a0);
	return 0;
}

int t32_tx_os_mipi_phy_stream_on(u32 sd, void *c) { return (int)phy_rec(sd, T32O_PTR(c)); }
int t32_tx_os_csi_video_s_stream(u32 sd, void *c) { return (int)sstream_rec(sd, T32O_PTR(c)); }
int t32_tx_os_csi_core_ops_init(u32 sd, void *c) { return (int)cinit_rec(sd, T32O_PTR(c)); }
u32 t32_tx_os_spin_lock_irqsave(u32 l) { return lock_rec(l); }
void t32_tx_os_spin_unlock_irqrestore(u32 l, u32 f) { op3_call("private_spin_unlock_irqrestore", 2, l, f, 0, 0); }
void t32_tx_os_mutex_unlock(u32 m) { op3_call("private_mutex_unlock", 1, m, 0, 0, 0); }
void t32_tx_os_clk_prepare_enable(u32 c) { op3_call("private_clk_prepare_enable", 1, c, 0, 0, 0); }
void t32_tx_os_clk_disable_unprepare(u32 c) { op3_call("private_clk_disable_unprepare", 1, c, 0, 0, 0); }

/* unused hooks of the included headers */
void t32_tx_isp_irq_handle(void) {}
void t32_tx_isp_irq_thread_handle(void) {}
u32 t32_tx_isp_dev, t32_tx_g_d9008;

static u8 tmpl[ARENA];
static void sw(u32 off, u32 v) { *(u32 *)(tmpl + off) = v; }
static u32 errp(void) { return (u32)-1 - op3_rnd() % 4095; }

static void build(u32 base)
{
	u32 k, m = op3_rnd();

	for (k = 0; k < ARENA; k += 4)
		sw(k, op3_rnd());
	sw(T32_SD_CSI, (m & 7) == 0 ? 0 : (m & 7) == 1 ? errp() : base + O_CSI);
	sw(T32_SD_CLKS, (m & 0x18) == 0 ? 0 : (m & 0x18) == 8 ? errp() : base + O_CLK);
	sw(T32_SD_NCLK, (m >> 5) % 6);
	for (k = 0; k < T32_CSI_INPUTS; k++)
		sw(O_CSI + T32_CSI_STATE(k), op3_rnd() % 6);
	phy_ret = (m & 0x300) ? 0 : op3_rnd();
	flags_v = op3_rnd();
}

static void setup(void)
{
	t32o_reset();
	memset(&op3_log, 0, sizeof(op3_log));
	arena = t32o_alloc(ARENA);
	memcpy(arena, tmpl, ARENA);
}

enum { F_SUSP, F_RES, F_IOCTL, F_ACT, F_SLAKE, F_N };
static const char *const fname[F_N] = { "suspend_module", "resume_module", "core_ops_ioctl", "activate_subdev", "slake_subdev" };
typedef u64 (*vfn)(u32, u32, u32, u32);
static const vfn vend[F_N] = {
	tx_isp_csi_suspend_module, tx_isp_csi_resume_module, tx_isp_csi_core_ops_ioctl,
	tx_isp_csi_activate_subdev, tx_isp_csi_slake_subdev
};
static int cur_f;
static u64 ours(u32 sd, u32 cmd, u32 a2, u32 a3, u32 a4)
{
	(void)a2; (void)a3; (void)a4;
	switch (cur_f) {
	case F_SUSP: return (u32)t32_tx_csi_suspend_module(sd);
	case F_RES: return (u32)t32_tx_csi_resume_module(sd);
	case F_IOCTL: return (u32)t32_tx_csi_core_ops_ioctl(sd, cmd);
	case F_ACT: return (u32)t32_tx_csi_activate_subdev(sd);
	default: return (u32)t32_tx_csi_slake_subdev(sd);
	}
}

struct snap {
	struct op3_log log;
	u8 w[ARENA];
	u32 ret;
};
static struct snap sv, so;

int main(void)
{
	unsigned int i;
	u32 base;

	setup();
	base = A(0);
	for (i = 0; i < NCASE; i++) {
		int f = (int)(i % F_N);
		u32 m = op3_rnd(), sd, cmd;

		build(base);
		sd = (m & 7) == 0 ? 0 : (m & 7) == 1 ? errp() : base;
		cmd = (m & 0x18) ? T32_CSI_PM_STREAMON : ((m & 0x20) ? T32_CSI_PM_STREAMON ^ (1u << (op3_rnd() & 31)) : op3_rnd());

		setup();
		sv.ret = OP3_VC(vend[f], sd, cmd, op3_rnd(), 0, 0);
		sv.log = op3_log;
		memcpy(sv.w, arena, ARENA);
		setup();
		cur_f = f;
		so.ret = OP3_VC(ours, sd, cmd, 0, 0, 0);
		so.log = op3_log;
		memcpy(so.w, arena, ARENA);

		if (op3_log_cmp(fname[f], &sv.log, &so.log))
			return 1;
		if (sv.ret != so.ret || memcmp(sv.w, so.w, ARENA)) {
			fprintf(stderr, "%s: ret %x/%x or world differs\n", fname[f], sv.ret, so.ret);
			return 1;
		}
	}
	printf("t32_txcsi_oracle: %u cases equal (%u functions)\n", NCASE, F_N);
	return 0;
}
