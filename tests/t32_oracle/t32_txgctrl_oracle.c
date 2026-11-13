// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: core get-control (driver/t32/tx_isp_t32_txgctrl.h)
 * against the vendor tx_isp_core_ops_g_ctrl (Matt Davis' recovered C,
 * PR #15 @ 2e2e06bc, symbols renamed op3_* by op3_ren.sh; the thread_info
 * of the inlined access_ok is an oracle object (op3_gctrl_fix.sh); the
 * five attribute helpers of tx_isp_t32_txattr.h run the vendor functions,
 * the AE / AWB / CCM helpers are recording hooks).
 *
 * Per case one control id (every handled id and random others), input
 * 0..3, a random ISP / core / sensor world, scripted getter / copy /
 * helper / vmalloc results, getters fill their buffers from a seed.  The
 * recovered C keeps a2 across __copy_user (the real call returns the left
 * count there), so the copy hook reports the whole block as left on both
 * sides.  Compared: the event log (copies with content hashes), every
 * world byte, the return value.
 * Exit 0 = all equal, 1 = mismatch.
 */
#include "t32_op3_glue.h"
#include "tx_isp_t32_txgctrl.h"

#define NCASE	40000
#define ARENA	0x9000u
#define O_ISP	0x0000u
#define O_SD	0x0200u
#define O_CORE	0x0400u		/* up to +1200 and 104 per input */
#define O_SNS	0x0a00u
#define O_CTRL	0x0c00u
#define O_USER	0x1000u		/* 12288 */
#define O_VM	0x4000u		/* vmalloc */

u8 op3_fake_ti[64] __attribute__((aligned(4)));
static const char *const fmts[] = { 0 };
#define S(n, v) char op3_t32_data_##n[] = v;
S(bdb5c, "tx_isp_bcsh_rgb_offset_g_attr") S(bdb7c, "tx_isp_ae_g_flicker_flag") S(bdb98, "tx_isp_g_csccr_attr")
S(bdbc8, "tx_isp_awb_g_ct_trend_offset") S(bdbe8, "tx_isp_wdr_g_mode") S(bdbfc, "tx_isp_faceawb_g_attr")
S(bdc14, "tx_isp_face_ae_luma_g_attr") S(bdc30, "tx_isp_face_ae_weiget_g_attr") S(bdc50, "tx_isp_face_ae_g_attr")
S(bdc68, "tx_isp_max_fps_g_attr") S(bdc80, "tx_isp_double_g_hb") S(bdce4, "tx_isp_awb_global_statis_g_attr")
S(bdd20, "tx_isp_af_metrics_g_attr") S(bdd8c, "tx_isp_af_statis_g_attr") S(bdda4, "tx_isp_ae_statis_g_attr")
S(bddf0, "tx_isp_awb_statis_g_attr") S(bde58, "tx_isp_draw_block_g_attr") S(bde74, "tx_isp_osd_block_g_attr")
S(bde8c, "tx_isp_osd_g_attr") S(bdea0, "tx_isp_mask_block_g_attr") S(bdebc, "tx_isp_wait_frame_done")
S(bded4, "tx_isp_sensor_g_attr") S(bdf04, "tx_isp_flicker_g_attr") S(bdf4c, "tx_isp_csc_g_attr")
S(bdf60, "tx_isp_module_g_attr") S(c4ad4, "[ %s:%d ] copy error!!!\n") S(c4b2c, "[ %s:%d ] get control failed!!!\n")
S(c4ce0, "copy error!!!\n") S(c5074, "The mirr status of ch%d is abnormal.") S(c509c, "The flip status of ch%d is abnormal.")
S(c50c4, "[ %s:%d ] get face ae failed!!!\n") S(c50e8, "[ %s:%d ] get face ae weight failed!!!\n")
S(c5110, "[ %s:%d ] get face ae luma failed!!!\n") S(c5138, "[ %s:%d ] get awb face attr failed!!!\n")
S(c5160, "[ %s:%d ] Non-wdr mode, get wdr output failed!!!\n") S(c5194, "[ %s:%d ] get csccr failed!!!\n")
S(c51b4, "[ %s:%d ] get bcsh rgb offset failed!!!\n")
/* the attribute helpers' own strings (data.c, see t32_txattr_oracle.c) */
S(c4fcc, "[ %s:%d ] get gamma failed!!!\n") S(c4fec, "[ %s:%d ] get failed!!!\n") S(bdf1c, "tx_isp_gamma_g_attr")
S(bdeec, "tx_isp_autozoom_g_attr") S(bde40, "tx_isp_af_weight_g_attr") S(bdd58, "tx_isp_statis_config_g_attr")
S(bdd3c, "tx_isp_module_ratio_g_attr")
/* data.c line numbers: the strings above are data.c:762..1019 (bd*) and 7898..8338 (c4* / c5*) */

u64 tx_isp_core_ops_g_ctrl(u32, u32, u32, u32);
u64 tx_isp_gamma_g_attr_isra_41(u32, u32, u32, u32);
u64 tx_isp_autozoom_g_attr_isra_43(u32, u32, u32, u32);
u64 tx_isp_af_weight_g_attr_isra_49(u32, u32, u32, u32);
u64 tx_isp_statis_config_g_attr_isra_56(u32, u32, u32, u32);
u64 tx_isp_module_ratio_g_attr_isra_57(u32, u32, u32, u32);

static u8 *arena;
static u32 A(u32 off) { return T32O_PTR(arena) + off; }
static u32 fnvb(const u8 *p, u32 n)
{
	u32 h = 2166136261u, i;

	for (i = 0; i < n; i++)
		h = (h ^ p[i]) * 16777619u;
	return h;
}

static u32 seed, ret_tab[8], n_ret, ctu_ret, vm_ok, flip_v[3], n_flip, limit;
static u32 next_ret(void) { return ret_tab[n_ret++ & 7]; }

static u32 get_rec(const char *name, u32 ch, void *buf)
{
	u32 s = seed ^ fnvb((const u8 *)name, (u32)strlen(name)), i;

	op3_call(name, 1, ch, 0, 0, 0);
	for (i = 0; i < 92; i++) {	/* the largest stack block the vendor copies (csc) */
		s = s * 1103515245u + 12345u;
		((u8 *)buf)[i] = (u8)(s >> 16);
	}
	return next_ret();
}
static u32 ctu_rec(u32 u, const void *src, u32 n)
{
	op3_call("private_copy_to_user", 3, u, n, fnvb(src, n), 0);
	memcpy(T32O_HOST(u), src, n);
	return ctu_ret;
}
static u32 cu_rec(void *d, const void *s, u32 n)
{
	op3_call("__copy_user", 2, T32O_PTR(d) >= A(O_USER) && T32O_PTR(d) < A(O_USER) + 0x3000 ? 1 : 0, n, 0, 0);
	memcpy(d, s, n);
	return n;	/* the recovered C keeps a2: the whole block counts as left */
}
static u32 helper_rec(const char *name, u32 ch, u32 arg) { op3_call(name, 2, ch, arg, 0, 0); return next_ret(); }
static u32 vm_rec(u32 n) { op3_call("private_vmalloc", 1, n, 0, 0, 0); return vm_ok ? A(O_VM) : 0; }
static u32 flip_rec(u32 ch, u32 *f) { op3_call("tisp_g_hv_flip", 2, ch, *f & 3, 0, 0); *f = flip_v[n_flip++ % 3]; return 0; }
static u32 wait_rec(u32 ch, u32 w, u32 *c) { op3_call("isp_frame_done_wait", 2, ch, w, 0, 0); c[0] = seed; c[1] = ~seed; return 0; }
static u32 ls_rec(u16 *b) { op3_call("lib_tisp_api_get_line_space", 0, 0, 0, 0, 0); b[0] = (u16)seed; return 0; }
static u32 fps_rec(u32 ch, u32 arg) { op3_call("tisp_get_fps", 2, ch, arg, 0, 0); *(u32 *)T32O_HOST(arg) = seed; return 0; }

/* vendor stubs */
#define G(n) u64 op3_##n(u32 a0, u32 a1, u32 a2, u32 a3); \
	u64 op3_##n(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return get_rec(#n, a0, T32O_HOST(a1)); }
G(tisp_g_awb_statis_attr) G(tisp_g_awb_global_statis_attr) G(tisp_awb_face_get) G(tisp_g_awb_ct_trend_offset)
G(tisp_g_ae_statis_attr) G(tisp_g_antiflick) G(tisp_g_ae_flicker_flag) G(tisp_g_face_ae_luma) G(tisp_g_face_ae_weiget)
G(tisp_g_af_statis_attr) G(tisp_g_af_metric_attr) G(tisp_ae_face_get) G(tisp_get_wdr_output_mode)
G(tisp_g_module_control) G(tisp_g_mscaler_mask_block_attr) G(tisp_get_csc_attr) G(tisp_g_csccr_mode)
G(tisp_g_coefft_wb) G(tisp_g_draw_block_attr) G(tisp_g_osd_attr) G(tisp_g_osd_block_attr)
G(tisp_g_Gamma) G(tisp_g_autozoom_control) G(tisp_g_af_weight_attr) G(tisp_g_statis_config_attr) G(tisp_g_module_ratio_attr)
#define H(n) u64 op3_##n(u32 a0, u32 a1, u32 a2, u32 a3); \
	u64 op3_##n(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return helper_rec(#n, a0, a1); }
H(tx_isp_awb_attr_g_attr_isra_16) H(tx_isp_awb_weight_g_attr_isra_50) H(tx_isp_awb_only_read_attr_isra_17)
H(tx_isp_ae_weight_g_attr_isra_55) H(tx_isp_ae_expoinfo_g_attr_isra_52) H(tx_isp_ae_scence_g_attr_isra_26)
H(tx_isp_ae_explist_g_attr_isra_62) H(tx_isp_ae_only_read_g_attr_isra_59) H(tx_isp_ae_target_g_attr_isra_61)
H(tx_isp_ae_hlc_or_blc_g_attr_isra_27) H(tiziano_isp_ccm_attr_g_ctrl_isra_40)
u64 op3_private_copy_to_user(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; return ctu_rec(a0, T32O_HOST(a1), a2); }
u64 op3___copy_user(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; cu_rec(T32O_HOST(a0), T32O_HOST(a1), a2); return 0; }
u64 op3_private_vmalloc(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; return vm_rec(a0); }
OP3_STUB(private_vfree, 1)
u64 op3_memset(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; memset(T32O_HOST(a0), (int)a1, a2); return a0; }
u64 op3_tisp_g_hv_flip(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return flip_rec(a0, T32O_HOST(a1)); }
u64 op3_isp_frame_done_wait(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; return wait_rec(a0, a1, T32O_HOST(a2)); }
u64 op3_lib_tisp_api_get_line_space(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; return ls_rec(T32O_HOST(a0)); }
u64 op3_tisp_get_fps(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return fps_rec(a0, a1); }
u64 op3_tx_isp_gamma_g_attr_isra_41(u32 a0, u32 a1, u32 a2, u32 a3) { return tx_isp_gamma_g_attr_isra_41(a0, a1, a2, a3); }
u64 op3_tx_isp_autozoom_g_attr_isra_43(u32 a0, u32 a1, u32 a2, u32 a3) { return tx_isp_autozoom_g_attr_isra_43(a0, a1, a2, a3); }
u64 op3_tx_isp_af_weight_g_attr_isra_49(u32 a0, u32 a1, u32 a2, u32 a3) { return tx_isp_af_weight_g_attr_isra_49(a0, a1, a2, a3); }
u64 op3_tx_isp_statis_config_g_attr_isra_56(u32 a0, u32 a1, u32 a2, u32 a3) { return tx_isp_statis_config_g_attr_isra_56(a0, a1, a2, a3); }
u64 op3_tx_isp_module_ratio_g_attr_isra_57(u32 a0, u32 a1, u32 a2, u32 a3) { return tx_isp_module_ratio_g_attr_isra_57(a0, a1, a2, a3); }

/* ours */
#define GO(ours, n) int ours(u32 ch, void *buf) { return (int)get_rec(#n, ch, buf); }
GO(t32_tx_os_tisp_g_awb_statis_attr, tisp_g_awb_statis_attr) GO(t32_tx_os_tisp_g_awb_global_statis_attr, tisp_g_awb_global_statis_attr)
GO(t32_tx_os_tisp_awb_face_get, tisp_awb_face_get) GO(t32_tx_os_tisp_g_awb_ct_trend_offset, tisp_g_awb_ct_trend_offset)
GO(t32_tx_os_tisp_g_ae_statis_attr, tisp_g_ae_statis_attr) GO(t32_tx_os_tisp_g_antiflick, tisp_g_antiflick)
GO(t32_tx_os_tisp_g_ae_flicker_flag, tisp_g_ae_flicker_flag) GO(t32_tx_os_tisp_g_face_ae_luma, tisp_g_face_ae_luma)
GO(t32_tx_os_tisp_g_face_ae_weiget, tisp_g_face_ae_weiget) GO(t32_tx_os_tisp_g_af_statis_attr, tisp_g_af_statis_attr)
GO(t32_tx_os_tisp_g_af_metric_attr, tisp_g_af_metric_attr) GO(t32_tx_os_tisp_ae_face_get, tisp_ae_face_get)
GO(t32_tx_os_tisp_get_wdr_output_mode, tisp_get_wdr_output_mode) GO(t32_tx_os_tisp_g_module_control, tisp_g_module_control)
GO(t32_tx_os_tisp_g_mscaler_mask_block_attr, tisp_g_mscaler_mask_block_attr) GO(t32_tx_os_tisp_get_csc_attr, tisp_get_csc_attr)
GO(t32_tx_os_tisp_g_csccr_mode, tisp_g_csccr_mode) GO(t32_tx_os_tisp_g_coefft_wb, tisp_g_coefft_wb)
GO(t32_tx_os_tisp_g_draw_block_attr, tisp_g_draw_block_attr) GO(t32_tx_os_tisp_g_osd_attr, tisp_g_osd_attr)
GO(t32_tx_os_tisp_g_osd_block_attr, tisp_g_osd_block_attr)
GO(t32_tx_os_tisp_g_gamma, tisp_g_Gamma) GO(t32_tx_os_tisp_g_autozoom, tisp_g_autozoom_control)
GO(t32_tx_os_tisp_g_af_weight, tisp_g_af_weight_attr) GO(t32_tx_os_tisp_g_statis_config, tisp_g_statis_config_attr)
GO(t32_tx_os_tisp_g_module_ratio, tisp_g_module_ratio_attr)
#define HO(ours, n) int ours(u32 ch, u32 arg) { return (int)helper_rec(#n, ch, arg); }
HO(t32_tx_os_awb_attr_g_attr, tx_isp_awb_attr_g_attr_isra_16) HO(t32_tx_os_awb_weight_g_attr, tx_isp_awb_weight_g_attr_isra_50)
HO(t32_tx_os_awb_only_read_attr, tx_isp_awb_only_read_attr_isra_17) HO(t32_tx_os_ae_weight_g_attr, tx_isp_ae_weight_g_attr_isra_55)
HO(t32_tx_os_ae_expoinfo_g_attr, tx_isp_ae_expoinfo_g_attr_isra_52) HO(t32_tx_os_ae_scence_g_attr, tx_isp_ae_scence_g_attr_isra_26)
HO(t32_tx_os_ae_explist_g_attr, tx_isp_ae_explist_g_attr_isra_62) HO(t32_tx_os_ae_only_read_g_attr, tx_isp_ae_only_read_g_attr_isra_59)
HO(t32_tx_os_ae_target_g_attr, tx_isp_ae_target_g_attr_isra_61) HO(t32_tx_os_ae_hlc_or_blc_g_attr, tx_isp_ae_hlc_or_blc_g_attr_isra_27)
HO(t32_tx_os_ccm_attr_g_ctrl, tiziano_isp_ccm_attr_g_ctrl_isra_40)
u32 t32_tx_os_copy_to_user(u32 u, const void *s, u32 n) { return ctu_rec(u, s, n); }
u32 t32_tx_os_copy_from_user(void *d, u32 u, u32 n) { memcpy(d, T32O_HOST(u), n); return 0; }
u32 t32_tx_os_vmalloc(u32 n) { return vm_rec(n); }
void t32_tx_os_vfree(u32 a) { op3_call("private_vfree", 1, a, 0, 0, 0); }
void t32_tx_os_memset(u32 a, u32 c, u32 n) { memset(T32O_HOST(a), (int)c, n); }
int t32_tx_os_tisp_g_hv_flip(u32 ch, void *f) { return (int)flip_rec(ch, f); }
int t32_tx_os_isp_frame_done_wait(u32 ch, u32 w, void *c) { return (int)wait_rec(ch, w, c); }
int t32_tx_os_lib_tisp_api_get_line_space(void *b) { return (int)ls_rec(b); }
int t32_tx_os_tisp_get_fps(u32 ch, u32 arg) { return (int)fps_rec(ch, arg); }
u32 t32_tx_os_thread_limit(void) { return limit; }
u32 t32_tx_os_copy_user(void *d, const void *s, u32 n) { return cu_rec(d, s, n); }
int t32_tx_os_tisp_s_gamma(u32 c, void *b) { (void)c; (void)b; return 0; }
int t32_tx_os_tisp_s_module_ratio(u32 c, void *b) { (void)c; (void)b; return 0; }
int t32_tx_os_tisp_s_af_weight(u32 c, void *b) { (void)c; (void)b; return 0; }
int t32_tx_os_tisp_s_statis_config(u32 c, void *b) { (void)c; (void)b; return 0; }

static const u32 ids[] = { 0x00, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x17, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
	0x28, 0x29, 0x2a, 0x2b, 0x2d, 0x2e, 0x31, 0x32, 0x33, 0x34, 0x35, 0x54, 0x70, 0x71, 0x72, 0x73, 0x74, 0x77, 0x78,
	0x80, 0x91, 0x96, 0x97, 0x9b, 0xa0, 0xa1, 0xa2, 0xa4, 0xa7 };

static u8 tmpl[ARENA];
static void sw(u32 off, u32 v) { *(u32 *)(tmpl + off) = v; }

static void setup(void)
{
	t32o_reset();
	memset(&op3_log, 0, sizeof(op3_log));
	arena = t32o_alloc(ARENA);
	memcpy(arena, tmpl, ARENA);
	n_ret = 0;
	n_flip = 0;
	*(u32 *)(op3_fake_ti + 24) = limit;
}

static u64 ours(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4) { (void)a3; (void)a4; return (u32)t32_tx_core_ops_g_ctrl(a0, a1, a2); }

struct snap {
	struct op3_log log;
	u8 w[ARENA];
	u32 ret;
};
static struct snap sv, so;

int main(void)
{
	unsigned int i, k;
	u32 base;

	(void)fmts;
	setup();
	base = A(0);
	for (i = 0; i < NCASE; i++) {
		u32 m = op3_rnd(), ch = op3_rnd() % 4, id;

		id = (m & 7) == 0 ? op3_rnd() % 0x100 : ids[op3_rnd() % (sizeof(ids) / sizeof(ids[0]))];
		if ((m & 0xff) == 8)
			id = op3_rnd();
		else
			id += T32_GC_BASE;
		for (k = 0; k < ARENA; k += 4)
			sw(k, op3_rnd());
		sw(O_ISP, base + O_SD);
		sw(O_SD + 280, base + O_CORE);
		for (k = 0; k < 4; k++) {
			sw(O_CORE + 376 + 104 * k, base + O_SNS);
			sw(O_CORE + 4 * (k + 294) + 4, (op3_rnd() & 1) ? 1 : op3_rnd() % 3);
		}
		sw(O_CTRL, id);
		sw(O_CTRL + 4, base + O_USER);
		seed = op3_rnd();
		for (k = 0; k < 8; k++)
			ret_tab[k] = (op3_rnd() & 3) ? 0 : op3_rnd();
		ctu_ret = (op3_rnd() & 7) ? 0 : op3_rnd();
		vm_ok = 1;
		for (k = 0; k < 3; k++)
			flip_v[k] = op3_rnd();
		limit = (op3_rnd() & 1) ? 0 : 0x80000000u;
		if (id == T32_GC_BASE + 0x91 && limit && (m & 0x100))
			sw(O_CTRL + 4, 0x7ffffff0u);	/* crosses the limit only with the end (+24) */

		setup();
		sv.ret = OP3_VC(tx_isp_core_ops_g_ctrl, ch, base + O_ISP, base + O_CTRL, 0, 0);
		sv.log = op3_log;
		memcpy(sv.w, arena, ARENA);
		setup();
		so.ret = OP3_VC(ours, ch, base + O_ISP, base + O_CTRL, 0, 0);
		so.log = op3_log;
		memcpy(so.w, arena, ARENA);
		if (op3_log_cmp("core_ops_g_ctrl", &sv.log, &so.log)) {
			fprintf(stderr, "  id %x ch %u\n", id, ch);
			return 1;
		}
		if (sv.ret != so.ret || memcmp(sv.w, so.w, ARENA)) {
			u32 j;

			for (j = 0; j < ARENA && sv.w[j] == so.w[j]; j++)
				;
			fprintf(stderr, "core_ops_g_ctrl: id %x ret %x/%x, first world diff 0x%x\n", id, sv.ret, so.ret, j);
			return 1;
		}
	}
	printf("t32_txgctrl_oracle: %u cases equal (%u control ids)\n", NCASE, (unsigned int)(sizeof(ids) / sizeof(ids[0])));
	return 0;
}
