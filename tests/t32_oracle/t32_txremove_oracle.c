// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: remove paths (driver/t32/tx_isp_t32_txremove.h) against
 * the vendor tx_isp_vic_remove, _vin_remove, _csi_remove, _ivdc_remove,
 * _ldc_remove, _fs_remove, _core_remove, tx_isp_remove and
 * tx_isp_frame_chan_deinit (Matt Davis' recovered C, PR #15 @ 2e2e06bc,
 * symbols renamed op3_* by op3_ren.sh; the vendor's calls of
 * tx_isp_frame_chan_deinit / tx_isp_unregister_platforms go to the vendor
 * functions, ours to our ports).
 *
 * Per case one random world: drvdata NULL / ERR_PTR / valid where the
 * vendor tolerates it, frame channels (count -1..5), core state words, LDC /
 * IVDC / FS globals, a platform table; the CSI PHY bases point into a traced
 * window (0xb3000000).  Compared: the event log (hooks with arguments, PHY
 * stores in order, callbacks), every world byte, the globals, the return
 * value (frame_chan_deinit: none).
 * Exit 0 = all equal, 1 = mismatch.
 */
#include "t32_op3_glue.h"
#include "tx_isp_t32_txremove.h"

#define NCASE	30000
#define ARENA	0x4000u
#define O_SD	0x000u
#define O_PRIV	0x400u
#define O_CHAN	0x1000u		/* 5 channels of 816 bytes */
#define O_RES	0x2200u
#define O_PLAT	0x2400u
#define PHYWIN	0xb3000000u

u32 op3_t32_data_d9890, op3_t32_data_d9940, op3_t32_data_d9948, op3_t32_data_d9950;
u32 t32_tx_ivdc_dev, t32_tx_ldc_buf, t32_tx_ldc_misc, t32_tx_fs_dev;
u32 t32_tx_isp_dev, t32_tx_g_d9008;
void t32_tx_isp_irq_handle(void) {}
void t32_tx_isp_irq_thread_handle(void) {}

u64 tx_isp_vic_remove(u32, u32, u32, u32);
u64 tx_isp_vin_remove(u32, u32, u32, u32);
u64 tx_isp_csi_remove(u32, u32, u32, u32);
u64 tx_isp_ivdc_remove(u32, u32, u32, u32);
u64 tx_isp_ldc_remove(u32, u32, u32, u32);
u64 tx_isp_fs_remove(u32, u32, u32, u32);
u64 tx_isp_core_remove(u32, u32, u32, u32);
u64 tx_isp_remove(u32, u32, u32, u32);
u64 tx_isp_frame_chan_deinit(u32, u32, u32, u32);
u64 tx_isp_unregister_platforms(u32, u32, u32, u32);

static u8 *arena;
static u32 A(u32 off) { return T32O_PTR(arena) + off; }
static u32 drv_ret;

OP3_STUB(private_platform_set_drvdata, 2)
OP3_STUB(private_kfree, 1)
OP3_STUB(tx_isp_subdev_deinit, 1)
OP3_STUB(private_misc_deregister, 1)
OP3_STUB(private_iounmap, 1)
OP3_STUB(private_release_mem_region, 2)
OP3_STUB(__vb2_queue_cancel, 1)
OP3_STUB(__vb2_queue_free, 2)
OP3_STUB(tx_isp_riscv_remove, 0)
OP3_STUB(private_proc_remove, 1)
OP3_STUB(isp_core_tuning_deinit, 1)
OP3_STUB(ispcore_slake_module, 1)
OP3_STUB(lib_tisp_deinit, 1)
OP3_STUB(lib_tisp_remove, 0)
OP3_STUB(private_platform_device_unregister, 1)
u64 op3_private_platform_get_drvdata(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	op3_call("private_platform_get_drvdata", 1, a0, 0, 0, 0);
	return drv_ret;
}
u64 op3_tx_isp_frame_chan_deinit(u32 a0, u32 a1, u32 a2, u32 a3) { return tx_isp_frame_chan_deinit(a0, a1, a2, a3); }
u64 op3_tx_isp_unregister_platforms(u32 a0, u32 a1, u32 a2, u32 a3) { return tx_isp_unregister_platforms(a0, a1, a2, a3); }

u32 t32_tx_os_get_drvdata(u32 d) { return (u32)op3_private_platform_get_drvdata(d, 0, 0, 0); }
void t32_tx_os_set_drvdata(u32 p, u32 d) { op3_call("private_platform_set_drvdata", 2, p, d, 0, 0); }
void t32_tx_os_kfree(u32 p) { op3_call("private_kfree", 1, p, 0, 0, 0); }
void t32_tx_os_subdev_deinit(u32 sd) { op3_call("tx_isp_subdev_deinit", 1, sd, 0, 0, 0); }
void t32_tx_os_misc_deregister(u32 m) { op3_call("private_misc_deregister", 1, m, 0, 0, 0); }
void t32_tx_os_iounmap(u32 a) { op3_call("private_iounmap", 1, a, 0, 0, 0); }
void t32_tx_os_release_mem_region(u32 s, u32 n) { op3_call("private_release_mem_region", 2, s, n, 0, 0); }
void t32_tx_os_vb2_queue_cancel(u32 q) { op3_call("__vb2_queue_cancel", 1, q, 0, 0, 0); }
void t32_tx_os_vb2_queue_free(u32 q, u32 a) { op3_call("__vb2_queue_free", 2, q, a, 0, 0); }
void t32_tx_os_riscv_remove(void) { op3_call("tx_isp_riscv_remove", 0, 0, 0, 0, 0); }
void t32_tx_os_proc_remove(u32 p) { op3_call("private_proc_remove", 1, p, 0, 0, 0); }
void t32_tx_os_core_tuning_deinit(u32 t) { op3_call("isp_core_tuning_deinit", 1, t, 0, 0, 0); }
void t32_tx_os_ispcore_slake_module(u32 c) { op3_call("ispcore_slake_module", 1, c, 0, 0, 0); }
void t32_tx_os_tisp_deinit(u32 w) { op3_call("lib_tisp_deinit", 1, w, 0, 0, 0); }
void t32_tx_os_tisp_remove(void) { op3_call("lib_tisp_remove", 0, 0, 0, 0, 0); }
void t32_tx_os_platform_device_unregister(u32 p) { op3_call("private_platform_device_unregister", 1, p, 0, 0, 0); }
static u32 cb_prem(u32 p) { return op3_call("plat_remove", 1, p, 0, 0, 0); }

static u8 tmpl[ARENA];
static u32 g0[4];
static void sw(u32 off, u32 v) { *(u32 *)(tmpl + off) = v; }
static u32 errp(void) { return (u32)-1 - op3_rnd() % 4095; }

enum { F_VIC, F_VIN, F_CSI, F_IVDC, F_LDC, F_FS, F_CORE, F_ISP, F_CHAN, F_N };
static const char *const fname[F_N] = {
	"vic_remove", "vin_remove", "csi_remove", "ivdc_remove", "ldc_remove", "fs_remove", "core_remove",
	"tx_isp_remove", "frame_chan_deinit"
};

static void build(u32 base, int f)
{
	u32 k, m = op3_rnd();

	for (k = 0; k < ARENA; k += 4)
		sw(k, op3_rnd());
	/* drvdata: NULL / ERR_PTR only where the vendor tests it */
	drv_ret = base + O_SD;
	if (f == F_VIC || f == F_VIN)
		drv_ret = (m & 3) == 0 ? 0 : (m & 3) == 1 ? errp() : base + O_SD;
	sw(O_SD + T32_SD_PRIV, base + O_PRIV);
	if (f == F_CSI) {
		sw(O_SD + T32_CSI_PHY0, PHYWIN + (op3_rnd() % 64) * 4);
		sw(O_SD + T32_CSI_PHY1, PHYWIN + 0x200 + (op3_rnd() % 64) * 4);
		sw(O_SD + T32_CSI_RES, base + O_RES);
	}
	sw(O_PRIV + T32_FS_CHANS, (m & 4) ? 0 : base + O_CHAN);
	sw(O_PRIV + T32_FS_NCHAN, (m & 8) ? (u32)-1 : (m >> 4) % 6);
	if (m & 4)
		sw(O_PRIV + T32_FS_NCHAN, 0);	/* no channel array: no channels */
	sw(O_PRIV + T32_CORE_TUNING, (m & 0x80) ? 0 : op3_rnd());
	sw(O_PRIV + T32_CORE_STATE, (m & 0x100) ? op3_rnd() : (u32)((int)(op3_rnd() % 5) - 1));
	sw(O_PRIV + T32_CORE_TISP1, (m & 0x200) ? 1 : op3_rnd() % 3);
	for (k = 0; f == F_ISP && k < T32_PLAT_N; k++) {	/* +160..+287 of the ISP device only */
		u32 r = op3_rnd();

		sw(O_SD + T32_ISP_PLATS + 8 * k, (r & 1) ? 0 : op3_rnd());
		sw(O_SD + T32_ISP_PLATS + 8 * k + 4, (r & 2) ? 0 : base + O_PLAT + 16 * k);
		sw(O_PLAT + 16 * k + 4, T32O_PTR(cb_prem));
	}
	for (k = 0; k < 4; k++)
		g0[k] = (op3_rnd() & 1) ? 0 : op3_rnd();
}

static void setup(void)
{
	t32o_reset();
	memset(&op3_log, 0, sizeof(op3_log));
	arena = t32o_alloc(ARENA);
	memcpy(arena, tmpl, ARENA);
	op3_win_open(1);
	memset((void *)(uintptr_t)PHYWIN, 0x5a, 0x1000);
	op3_win_open(0);
}

struct snap {
	struct op3_log log;
	u8 w[ARENA], win[0x1000];
	u32 g[4], ret;
};
static struct snap sv, so;

static void take(struct snap *s, u32 ret, const u32 *g)
{
	s->log = op3_log;
	memcpy(s->w, arena, ARENA);
	op3_win_open(1);
	memcpy(s->win, (void *)(uintptr_t)PHYWIN, 0x1000);
	op3_win_open(0);
	memcpy(s->g, g, sizeof(s->g));
	s->ret = ret;
}

typedef u64 (*vfn)(u32, u32, u32, u32);
static const vfn vend[F_N] = {
	tx_isp_vic_remove, tx_isp_vin_remove, tx_isp_csi_remove, tx_isp_ivdc_remove, tx_isp_ldc_remove,
	tx_isp_fs_remove, tx_isp_core_remove, tx_isp_remove, tx_isp_frame_chan_deinit
};
static int cur_f;
static u64 ours(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4)
{
	(void)a1; (void)a2; (void)a3; (void)a4;
	switch (cur_f) {
	case F_VIC: return (u32)t32_tx_vic_remove(a0);
	case F_VIN: return (u32)t32_tx_vin_remove(a0);
	case F_CSI: return (u32)t32_tx_csi_remove(a0);
	case F_IVDC: return (u32)t32_tx_ivdc_remove(a0);
	case F_LDC: return (u32)t32_tx_ldc_remove(a0);
	case F_FS: return (u32)t32_tx_fs_remove(a0);
	case F_CORE: return (u32)t32_tx_core_remove(a0);
	case F_ISP: return (u32)t32_tx_isp_remove(a0);
	default: t32_tx_frame_chan_deinit(a0); return 0;
	}
}

int main(void)
{
	unsigned int i;
	u32 base, g[4];

	op3_win_map(PHYWIN, 0x1000);
	setup();
	base = A(0);
	for (i = 0; i < NCASE; i++) {
		int f = (int)(i % F_N);
		u32 m = op3_rnd(), a0 = op3_rnd();

		build(base, f);
		if (f == F_CHAN)
			a0 = (m & 3) == 0 ? 0 : (m & 3) == 1 ? errp() : base + O_CHAN;

		setup();
		op3_t32_data_d9890 = g0[0]; op3_t32_data_d9940 = g0[1]; op3_t32_data_d9948 = g0[2]; op3_t32_data_d9950 = g0[3];
		sv.ret = OP3_VC(vend[f], a0, 0, 0, 0, 0);
		g[0] = op3_t32_data_d9890; g[1] = op3_t32_data_d9940; g[2] = op3_t32_data_d9948; g[3] = op3_t32_data_d9950;
		take(&sv, sv.ret, g);

		setup();
		t32_tx_ivdc_dev = g0[0]; t32_tx_ldc_buf = g0[1]; t32_tx_ldc_misc = g0[2]; t32_tx_fs_dev = g0[3];
		cur_f = f;
		so.ret = OP3_VC(ours, a0, 0, 0, 0, 0);
		g[0] = t32_tx_ivdc_dev; g[1] = t32_tx_ldc_buf; g[2] = t32_tx_ldc_misc; g[3] = t32_tx_fs_dev;
		take(&so, so.ret, g);

		if (op3_log_cmp(fname[f], &sv.log, &so.log))
			return 1;
		if ((f != F_CHAN && sv.ret != so.ret) || memcmp(sv.w, so.w, ARENA) || memcmp(sv.win, so.win, 0x1000) ||
		    memcmp(sv.g, so.g, sizeof(sv.g))) {
			fprintf(stderr, "%s: ret %x/%x, world / window / globals differ\n", fname[f], sv.ret, so.ret);
			return 1;
		}
	}
	printf("t32_txremove_oracle: %u cases equal (%u functions)\n", NCASE, F_N);
	return 0;
}
