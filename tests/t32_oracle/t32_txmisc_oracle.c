// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: subdevice teardown / sensor flip
 * (driver/t32/tx_isp_t32_txmisc.h) against the vendor tx_isp_subdev_deinit
 * and tx_isp_core_shvflip (Matt Davis' recovered C, PR #15 @ 2e2e06bc,
 * symbols renamed op3_* by op3_ren.sh; subdev_deinit's tx_isp_free_irq /
 * tx_isp_module_deinit calls run the vendor functions, ours our ports).
 *
 * Per case a random subdevice (misc flag, buffers, mappings, resources,
 * irq info with 0..4 irqs) or core (input 0..3, flip mode, scripted sensor
 * ioctl reply msg[1], core ioctl callback or 0).  Compared: the event log
 * (hooks and callbacks with their argument words), every world byte, the
 * shvflip return value.
 * Exit 0 = all equal, 1 = mismatch.
 */
#include "t32_op3_glue.h"
#include "tx_isp_t32_txmisc.h"

#define NCASE	30000
#define ARENA	0x1000u
#define O_SD	0x000u
#define O_CORE	0x200u		/* core device: 4 inputs from +1344 */
#define O_RES	0xe00u

u32 op3_t32_data_f3310, t32_tx_core_sd;
u32 t32_tx_isp_dev, t32_tx_g_d9008, t32_tx_stream_count[4];
void t32_tx_isp_irq_handle(void) {}
void t32_tx_isp_irq_thread_handle(void) {}

u64 tx_isp_subdev_deinit(u32, u32, u32, u32);
u64 tx_isp_core_shvflip(u32, u32, u32, u32);
u64 tx_isp_free_irq(u32, u32, u32, u32);
u64 tx_isp_module_deinit(u32, u32, u32, u32);

static u8 *arena;
static u32 A(u32 off) { return T32O_PTR(arena) + off; }
static u32 w32(u32 p, u32 k) { return ((const u32 *)T32O_HOST(p))[k]; }
static u32 reply, fn_ret;

static u32 ioctl_rec(u32 sd, u32 cmd, u32 arg)
{
	u32 msg = w32(arg, 1);

	op3_call("ispcore_sensor_ops_ioctl", 4, sd, cmd, w32(arg, 0), w32(msg, 0));
	((u32 *)T32O_HOST(msg))[1] = reply;
	return 0;
}
static u32 cb_core(u32 sd, u32 cmd, u32 b) { op3_call("core_ioctl", 4, sd, cmd, w32(b, 0), w32(b, 1)); return fn_ret; }

OP3_STUB(private_misc_deregister, 1)
OP3_STUB(isp_subdev_release_clks, 1)
OP3_STUB(private_kfree, 1)
OP3_STUB(private_iounmap, 1)
OP3_STUB(private_release_mem_region, 2)
OP3_STUB(private_free_irq, 2)
OP3_STUB(lib_tisp_vic_ctrl, 2)
u64 op3_ispcore_sensor_ops_ioctl(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; return ioctl_rec(a0, a1, a2); }
u64 op3_memset(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; memset(T32O_HOST(a0), (int)a1, a2); return a0; }
u64 op3_tx_isp_free_irq(u32 a0, u32 a1, u32 a2, u32 a3) { return tx_isp_free_irq(a0, a1, a2, a3); }
u64 op3_tx_isp_module_deinit(u32 a0, u32 a1, u32 a2, u32 a3) { return tx_isp_module_deinit(a0, a1, a2, a3); }

void t32_tx_os_misc_deregister(u32 m) { op3_call("private_misc_deregister", 1, m, 0, 0, 0); }
void t32_tx_os_release_clks(u32 sd) { op3_call("isp_subdev_release_clks", 1, sd, 0, 0, 0); }
void t32_tx_os_kfree(u32 p) { op3_call("private_kfree", 1, p, 0, 0, 0); }
void t32_tx_os_iounmap(u32 a) { op3_call("private_iounmap", 1, a, 0, 0, 0); }
void t32_tx_os_release_mem_region(u32 s, u32 n) { op3_call("private_release_mem_region", 2, s, n, 0, 0); }
void t32_tx_os_free_irq(u32 irq, u32 dev) { op3_call("private_free_irq", 2, irq, dev, 0, 0); }
void t32_tx_os_vic_ctrl(u32 a, u32 b) { op3_call("lib_tisp_vic_ctrl", 2, a, b, 0, 0); }
int t32_tx_os_ispcore_sensor_ioctl(u32 sd, u32 cmd, void *arg) { return (int)ioctl_rec(sd, cmd, T32O_PTR(arg)); }

static u8 tmpl[ARENA];
static void sw(u32 off, u32 v) { *(u32 *)(tmpl + off) = v; }

static void build(u32 base)
{
	u32 k, m = op3_rnd();

	for (k = 0; k < ARENA; k += 4)
		sw(k, op3_rnd());
	sw(O_SD + T32_SD_MISC_ON, (m & 1) ? 0 : op3_rnd());
	for (k = 0; k < 2; k++)
		sw(O_SD + T32_SD_BUF(k), (op3_rnd() & 1) ? 0 : op3_rnd());
	for (k = 0; k < 3; k++) {
		sw(O_SD + T32_SD_MAP(k), (op3_rnd() & 1) ? 0 : op3_rnd());
		sw(O_SD + T32_SD_RES(k), (op3_rnd() & 1) ? 0 : base + O_RES + 16 * k);
	}
	sw(O_SD + T32_SD_IRQ + T32_IRQ_COUNT, (m & 6) ? op3_rnd() % 4 : (u32)-1);
	for (k = 0; k < 3; k++)
		sw(O_SD + T32_SD_IRQ + T32_IRQ_NUM(k), (op3_rnd() & 1) ? 0 : op3_rnd());
	sw(O_SD + T32_SD_PRIV_CORE, base + O_CORE);
	sw(O_SD + T32_SD_OPS, base + 0xf00);
	sw(0xf00 + T32_OPS_CORE, (m & 8) ? 0 : base + 0xf40);
	sw(0xf40 + T32_CORE_IOCTL, (m & 0x10) ? 0 : T32O_PTR(cb_core));
	reply = (m & 0x20) ? op3_rnd() : (u32)((int)(op3_rnd() % 5) - 2);
	fn_ret = op3_rnd();
}

static void setup(void)
{
	t32o_reset();
	memset(&op3_log, 0, sizeof(op3_log));
	arena = t32o_alloc(ARENA);
	memcpy(arena, tmpl, ARENA);
}

static int cur_f;
static u64 ours(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4)
{
	(void)a1; (void)a2; (void)a3; (void)a4;
	if (cur_f)
		return (u32)t32_tx_core_shvflip(a0);
	t32_tx_subdev_deinit(a0);
	return 0;
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
		int f = (int)(i & 1);
		u32 a0;

		build(base);
		a0 = f ? op3_rnd() % 4 : base + O_SD;
		setup();
		op3_t32_data_f3310 = base + O_SD;
		sv.ret = OP3_VC(f ? tx_isp_core_shvflip : tx_isp_subdev_deinit, a0, 0, 0, 0, 0);
		sv.log = op3_log;
		memcpy(sv.w, arena, ARENA);
		setup();
		t32_tx_core_sd = base + O_SD;
		cur_f = f;
		so.ret = OP3_VC(ours, a0, 0, 0, 0, 0);
		so.log = op3_log;
		memcpy(so.w, arena, ARENA);
		if (op3_log_cmp(f ? "core_shvflip" : "subdev_deinit", &sv.log, &so.log))
			return 1;
		if ((f && sv.ret != so.ret) || memcmp(sv.w, so.w, ARENA)) {
			fprintf(stderr, "%s: ret %x/%x or world differs\n", f ? "core_shvflip" : "subdev_deinit", sv.ret, so.ret);
			return 1;
		}
	}
	printf("t32_txmisc_oracle: %u cases equal (2 functions)\n", NCASE);
	return 0;
}
