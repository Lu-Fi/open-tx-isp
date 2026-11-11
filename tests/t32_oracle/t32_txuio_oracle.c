// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: ISP device ioctl dispatcher (driver/t32/tx_isp_t32_txuioctl.h)
 * against the vendor tx_isp_unlocked_ioctl (Matt Davis' recovered C, PR #15
 * @ 2e2e06bc, symbols renamed op3_* by op3_ren.sh).
 *
 * The ioctl helpers (proven by t32_txioctl_oracle) are recorded hooks on
 * both sides: our header's calls are mapped to the recorders by macros.
 * Per case one command (every ported one, random unknown ones), file 0 or a
 * file, a random ISP device / 16 subdevice world with recording core ioctl
 * callbacks, scripted results.  Compared: the event log, every world byte,
 * the return value.
 * Exit 0 = all equal, 1 = mismatch.
 */
#include "t32_op3_glue.h"
#include "tx_isp_t32_txioctl.h"

#define NCASE	60000
#define ARENA	0x4000u
#define O_FILE	0x0000u
#define O_ISP	0x0100u
#define NSD	6
#define O_SD(i)	(0x0400u + 0x200u * (i))
#define O_USER	0x1400u

static u8 *arena;
static u32 A(u32 off) { return T32O_PTR(arena) + off; }

static u32 rtab[8], n_r;
static u32 next_r(void) { return rtab[n_r++ & 7]; }

/* the ioctl helpers: recorded on both sides */
static u32 hrec(const char *name, u32 a0, u32 a1) { op3_call(name, 2, a0, a1, 0, 0); return next_r(); }
#define t32_tx_driver_version(u)		((int)hrec("tx_isp_driver_version_isra_11", u, 0))
#define t32_tx_sensor_register_sensor(i, a)	((int)hrec("tx_isp_sensor_register_sensor", i, a))
#define t32_tx_sensor_release_sensor(i, a)	((int)hrec("tx_isp_sensor_release_sensor", i, a))
#define t32_tx_video_s_stream(i, a)		((int)hrec("tx_isp_video_s_stream", i, a))
#define t32_tx_video_link_stream(i, a)		((int)hrec("tx_isp_video_link_stream", i, a))
#define t32_tx_multisensor_mode(i, a)		((int)hrec("tx_isp_multisensor_mode", i, a))
#define t32_tx_set_ae_algo_open(i, a)		((int)hrec("tx_isp_set_ae_algo_open", i, a))
#define t32_tx_set_default_bin_path(i, a)	((int)hrec("tx_isp_set_default_bin_path_isra_14", i, a))
#define t32_tx_get_default_bin_path(i, a)	((int)hrec("tx_isp_get_default_bin_path_isra_17", i, a))
#include "tx_isp_t32_txuioctl.h"

#define HV(n) u64 op3_##n(u32 a0, u32 a1, u32 a2, u32 a3); \
	u64 op3_##n(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return hrec(#n, a0, a1); }
u64 op3_tx_isp_driver_version_isra_11(u32 a0, u32 a1, u32 a2, u32 a3);
u64 op3_tx_isp_driver_version_isra_11(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; return hrec("tx_isp_driver_version_isra_11", a0, 0); }
HV(tx_isp_sensor_register_sensor) HV(tx_isp_sensor_release_sensor) HV(tx_isp_video_s_stream) HV(tx_isp_video_link_stream)
HV(tx_isp_multisensor_mode) HV(tx_isp_set_ae_algo_open) HV(tx_isp_set_default_bin_path_isra_14)
HV(tx_isp_get_default_bin_path_isra_17)

/* the subdevices' core ioctl */
static u32 sd_index(u32 sd)
{
	u32 i;

	for (i = 0; i < NSD; i++)
		if (sd == A(O_SD(i)))
			return i;
	return NSD;
}
static u32 core_tab[NSD + 1];
static u32 cb_core(u32 sd, u32 cmd, u32 arg) { op3_call("core_ioctl", 3, sd, cmd, arg, 0); return core_tab[sd_index(sd)]; }

/* not yet exercised externals */
OP3_STUB(find_subdev_link_pad, 2) OP3_STUB(finish_wait, 2) OP3_STUB(get_isp_cache_line, 0) OP3_STUB(get_isp_memopt, 0)
OP3_STUB(lib_tisp_api_s_runningMode, 1) OP3_STUB(lib_tisp_get_isp_ipc_memory_space, 1)
OP3_STUB(lib_tisp_get_isp_memory_space, 1) OP3_STUB(lib_tisp_set_isp_ipc_memory_space, 2)
OP3_STUB(lib_tisp_set_isp_memory_space, 2) OP3_STUB(memcpy, 3) OP3_STUB(memset, 3) OP3_STUB(prepare_to_wait_event, 3)
OP3_STUB(private_copy_from_user, 3) OP3_STUB(private_copy_to_user, 3) OP3_STUB(private_init_completion, 1)
OP3_STUB(private_kfree, 1) OP3_STUB(private_kmalloc, 2) OP3_STUB(private_wait_for_completion_timeout, 2)
OP3_STUB(schedule_timeout, 1) OP3_STUB(subdev_video_destroy_link, 1) OP3_STUB(tisp_g_ae_scence_attr, 2)
OP3_STUB(tisp_get_frame_drop, 3) OP3_STUB(tisp_s_ae_scence_attr, 2) OP3_STUB(tisp_set_frame_drop, 3)
OP3_STUB(tisp_s_wdr_en, 1) OP3_STUB(tx_isp_default_bin_isra_23, 2) OP3_STUB(tx_isp_vic_save_raw, 2)
OP3_STUB(tx_isp_video_link_destroy_isra_4, 1)
u8 op3_mips_dma_map_ops[64], op3_t32_data_bed7c[256];
u32 op3_t32_data_d9014, op3_t32_data_da784, t32_tx_isp_dev;
#define S(n, v) char op3_t32_data_##n[] = v;
S(be720, "tx_isp_memory_free") S(be734, "tx_isp_memory_malloc") S(be74c, "tx_isp_set_wdr_enable")
S(be778, "tx_isp_set_tiziano_par") S(be790, "tx_isp_gpio_state_switch_trige") S(be7b0, "tx_isp_gpio_state_switch")
S(be7cc, "tx_isp_get_isp_ipc_mem") S(be7e4, "tx_isp_set_isp_ipc_mem") S(be7fc, "tx_isp_get_ae_start_ev")
S(be814, "tx_isp_set_ae_start_ev") S(be82c, "tx_isp_get_raw_control") S(be844, "tx_isp_get_pm_all_done")
S(be85c, "tx_isp_get_pm_control") S(be874, "tx_isp_set_pm_control") S(be88c, "tx_isp_start_night_mode")
S(be8a4, "tx_isp_get_frame_drop_once") S(be8c0, "tx_isp_set_frame_drop_once") S(be8dc, "tx_isp_get_frame_drop")
S(be8f4, "tx_isp_set_frame_drop") S(be944, "tx_isp_set_awb_algo_close") S(be960, "tx_isp_set_awb_algo_open")
S(be97c, "tx_isp_set_ae_algo_close") S(be9c8, "tx_isp_wdr_get_buf") S(be9dc, "tx_isp_wdr_set_buf")
S(be9f0, "tx_isp_get_isp_mem") S(bea04, "tx_isp_set_isp_mem") S(bea18, "tx_isp_sensor_g_register")
S(bea34, "tx_isp_sensor_s_register") S(bea6c, "subdev_video_setup_link") S(bea9c, "tx_isp_video_link_setup")
S(beb0c, "tx_isp_sensor_set_input") S(beb24, "tx_isp_sensor_get_input") S(beb3c, "tx_isp_sensor_enum_input")
S(beb58, "tx_isp_bypass_all") S(beb84, "tx_isp_unlocked_ioctl") S(c6ae4, "[ %s:%d ] copy from user error\n")
S(c6b04, "[ %s:%d ] Ae Algo Function register version error\n") S(c6b38, "[ %s:%d ] copy to user error\n")
S(c6e38, "[ %s:%d ] link(%d) is invalid!\n") S(c6e58, "[ %s:%d ] The link type is mismatch!\n")
S(c6e80, "[ %s:%d ] Please stop active links firstly!\n")
S(c6eb0, "[ %s:%d ] Not the wdr mode, do not need to alloc buf!!!\n")
S(c6eec, "[ %s:%d ] tsize is %d, buf info size is %d, paddr is 0x%x\n") S(c6f28, "[ %s:%d ] buf size too small\n")
S(c6f48, "[ %s:%d ] tsize is %d\n") S(c6f60, "FPS over 32 in the numerator or denominator!!! Setup failed!!!\n")
S(c6fa0, "set frame drop chn:%d lsize:%d fmark:0x%x\n") S(c6fcc, "set frame drop chn:%d disable\n")
S(c6fec, "[%s %d] aov_mode is %d\n") S(c7004, "\n[%s %d]isp already is normal mode !!!\n")
S(c702c, "[%s %d] switch is normal\n") S(c7048, "Wait switch mode failed\n")
S(c7064, "\n[%s %d]isp already is aov mode !!!\n")
S(c708c, "\nvinum is %d, width is %d, height is %d, imagesize is %d\n, snap num is %d, buf size is %d, wdrmode %d, snaprawmode %d\n")
S(c7104, "\n [%s %d] Can't output the width(%d)!\n") S(c712c, "\n[%s %d] The node is busy!\n")
S(c7148, "[ %s:%d ] get ae scence failed!!!\n") S(c716c, "[ %s:%d ] set ae start ev failed!!!\n")
S(c7194, "Failed to copy from user\n") S(c71b0, "[ %s:%d ] set gpio state switch failed!!!\n")
S(c71dc, "[ %s:%d ] set gpio state switch trigger failed!!!\n")
S(c7210, "[ISP] sensor id = %d, Multi-sensors don't support WDR mode !!!\n")
S(c7250, "[ISP] Mode(%d) don't support WDR mode !!!\n") S(c727c, "Get isp driver running mode failed\n")
S(c72a0, "[ %s:%d ] Failed to allocate memory\n") S(c72c8, "[ %s:%d ] The memory cannot be empty\n")
S(c72f0, "[ %s:%d ] Unable to apply for manageable memory\n") S(c7324, "[ %s:%d ] Unmanageable memory\n")
/* data.c:1515..1796 (names), 9950..10478 (formats) */

u64 tx_isp_unlocked_ioctl(u32, u32, u32, u32);

/* every command the vendor handles, and the ones ported so far */
static const u32 vend_cmds[] = {
	0x80045401, 0x80085403, 0x80085407, 0x80085408, 0x80085409, 0x8008540a, 0x80085418, 0x800c540f, 0x800c5410,
	0x800c5411, 0x800c5412, 0x800c5438, 0x800c5439, 0x800c543a, 0x800c543b, 0x8040540e, 0x80745405, 0x80745406,
	0xc0045402, 0xc0045415, 0xc004541b, 0xc004541c, 0xc004541d, 0xc004541e, 0xc0045423, 0xc0045424, 0xc0045425,
	0xc0045426, 0xc004542a, 0xc004542b, 0xc004542c, 0xc004542d, 0xc004542e, 0xc0045431, 0xc0045432,
	0xc0045433, 0xc0045434, 0xc0045436, 0xc0045437, 0xc004543c, 0xc004543d, 0xc004548e, 0xc004548f, 0xc00454b8,
	0xc00454ba, 0xc00454c8, 0xc00454c9, 0xc00454ca, 0xc00454cb, 0xc00454cc, 0xc00454cd, 0xc00454ce, 0xc0085404,
	0xc008540b, 0xc008540c, 0xc040540d
};
static const u32 cmds[] = {
	/* helpers */
	T32_UIO_VERSION, T32_UIO_SENSOR_REG, T32_UIO_SENSOR_REL, T32_UIO_S_STREAM, T32_UIO_S_STREAM2,
	T32_UIO_LINK_STREAM, T32_UIO_LINK_STREAM2, T32_UIO_MULTISENSOR, T32_UIO_AE_ALGO, T32_UIO_SET_BINPATH,
	T32_UIO_GET_BINPATH,
	/* core broadcasts */
	T32_UIO_CORE_16, T32_UIO_CORE_17, T32_UIO_CORE_1A, T32_UIO_CORE_SUM,
	/* default neighbours inside the vendor ranges */
	0xc0045427, 0xc0045428, 0xc0045429, 0
};
#define NV	(sizeof(vend_cmds) / sizeof(vend_cmds[0]))
#define NC	(sizeof(cmds) / sizeof(cmds[0]))

static int handled(u32 c)
{
	unsigned int k;

	for (k = 0; k < NV; k++)
		if (vend_cmds[k] == c)
			return 1;
	return 0;
}

static u8 tmpl[ARENA];
static void sw(u32 off, u32 v) { *(u32 *)(tmpl + off) = v; }

static void build(u32 base)
{
	u32 k, i;

	for (k = 0; k < ARENA; k += 4)
		sw(k, op3_rnd());
	sw(O_FILE + T32_FILE_PRIV, base + O_ISP + T32_FILE_PRIV_ADJ);
	for (k = 0; k < T32_DEV_SD_N; k++)
		sw(O_ISP + T32_DEV_SD + 4 * k, (op3_rnd() & 1) ? base + O_SD(op3_rnd() % NSD) : 0);
	for (i = 0; i < NSD; i++) {
		u32 s = O_SD(i), r = op3_rnd();

		sw(s + T32_SD_OPS, base + s + 0x110);
		sw(s + 0x110 + T32_OPS_CORE, (r & 3) ? base + s + 0x130 : 0);
		sw(s + 0x130 + T32_CORE_IOCTL, (r & 4) ? T32O_PTR(cb_core) : 0);
	}
	for (i = 0; i <= NSD; i++) {
		u32 r = op3_rnd() & 7;

		core_tab[i] = r < 4 ? 0 : r < 6 ? (u32)-T32_ENOIOCTLCMD : op3_rnd();
	}
	for (k = 0; k < 8; k++)
		rtab[k] = (op3_rnd() & 3) ? 0 : op3_rnd();
}

static void setup(void)
{
	t32o_reset();
	memset(&op3_log, 0, sizeof(op3_log));
	arena = t32o_alloc(ARENA);
	memcpy(arena, tmpl, ARENA);
	n_r = 0;
}

static u64 ours(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4) { (void)a3; (void)a4; return (u32)t32_tx_isp_unlocked_ioctl(a0, a1, a2); }

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
		u32 m = op3_rnd(), cmd, file, arg;

		cmd = cmds[op3_rnd() % NC];
		if ((m & 7) == 0) {
			do
				cmd = (m & 8) ? (0xc0045400u | (op3_rnd() & 0xff)) : op3_rnd();
			while (handled(cmd));
		}
		file = (m & 0x10) ? base + O_FILE : 0;
		arg = (m & 0x20) ? base + O_USER : op3_rnd();
		build(base);

		setup();
		op3_t32_data_da784 = base + O_ISP;
		sv.ret = OP3_VC(tx_isp_unlocked_ioctl, file, cmd, arg, op3_rnd(), 0);
		sv.log = op3_log;
		memcpy(sv.w, arena, ARENA);
		setup();
		t32_tx_isp_dev = base + O_ISP;
		so.ret = OP3_VC(ours, file, cmd, arg, 0, 0);
		so.log = op3_log;
		memcpy(so.w, arena, ARENA);
		if (op3_log_cmp("unlocked_ioctl", &sv.log, &so.log)) {
			fprintf(stderr, "  cmd %x file %x\n", cmd, file);
			return 1;
		}
		if (sv.ret != so.ret || memcmp(sv.w, so.w, ARENA)) {
			u32 j;

			for (j = 0; j < ARENA && sv.w[j] == so.w[j]; j++)
				;
			fprintf(stderr, "unlocked_ioctl: cmd %x ret %x/%x, first world diff 0x%x\n", cmd, sv.ret, so.ret, j);
			return 1;
		}
	}
	printf("t32_txuio_oracle: %u cases equal (%u commands)\n", NCASE, (unsigned int)NC);
	return 0;
}
