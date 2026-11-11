// SPDX-License-Identifier: GPL-2.0
/*
 * T32 ISP tuning controls on the live AE / AWB blocks (openimp #296, #234).
 *
 * The vendor flow of the AE / AWB / statistics tuning ids, end to end:
 *
 *   isp_core_tunning_default_ioctl -> tx_isp_core_ops_[sg]_ctrl
 *     (tx_isp_t32_txsctrl.h / txgctrl.h, glue in tx_isp_t32_tunctl.c)
 *   -> the tx_isp_*_attr .isra wrappers (t32_isra_*, tx_isp_t32_stats.h)
 *   -> the tisp_[sg]_* API (t32_g_* / t32_s_* of tx_isp_t32_api_get.h,
 *      api_set.h, api_attr.h, api_stat.h)
 *   -> the AE / AWB block API (t32_aea_* / t32_awb_*, tx_isp_t32_stats.h)
 *
 * All of those are the oracle-proven ports (t32_aeaj / aeak / aeal / aeam,
 * t32_getp / getd / geth, t32_apiset, t32_apiattr, t32_apistat, t32_aeaa ..
 * t32_aeai, t32_awbaa); this file only binds them: the block functions run on
 * the blocks the per-frame AE (tx_isp_t32_ae.c, t32_aec_api_get) and AWB
 * (tx_isp_t32_awbc.c, t32_awbc_api_get) work on, so a Set reaches the image
 * the vendor way: the AE / AWB frames read what the API wrote (tune / buf /
 * state words, the R + 1285 update flag), and a Get answers the live state
 * (statistics, exposure, scene luma, colour gains).
 *
 * The tuning attribute block (vendor tisp_tattr, *t32_data_f0904, 4532 B)
 * is built as tisp_enable_tuning does (t32_enable_tuning, tx_isp_t32_tuning.h)
 * on its first use; tisp_s/g_awb_attr, tisp_s/g_ae_exprinfo_attr and the
 * statistics config keep their per-channel words there.
 *
 * Differences (driver state that the vendor has and we do not):
 *  - Before the per-frame AE / AWB is attached (stream-on) or with its
 *    t32_ported_paths bit off, the block calls have no state: getters answer
 *    zeroed blocks, setters are dropped (the AE scene block is kept as set).
 *    The AE context built at stream-on stays attached (t32_pf_reset runs
 *    before t32_stats_init, openimp #350), so after a stream-on the AE ids
 *    work on the live blocks.
 *  - The AF part of the statistics config is the live AF block's window
 *    attribute (tisp_af_api_get_attr / _set_attr) and its GSM zone bounds go
 *    to the live GSM block (tisp_gsm_api_set), tx_isp_t32_afgsm.c; before
 *    the AF / GSM attach (or with their t32_ported_paths bit off) the AF
 *    words come from tattr and the sets are dropped.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/string.h>

#include "tx_isp_t32.h"
#include "tx_isp_t32_txsctrl.h"
#include "tx_isp_t32_txgctrl.h"
#include "tx_isp_t32_stats.h"
#include "tx_isp_t32_aemath.h"
#include "tx_isp_t32_log2.h"
#include "tx_isp_t32_api_get.h"
#include "tx_isp_t32_api_set.h"
#include "tx_isp_t32_api_attr.h"
#include "tx_isp_t32_api_stat.h"
/* tuning.h names its hooks ioremap / iounmap (struct members), which are macros in the kernel */
#pragma push_macro("ioremap")
#pragma push_macro("iounmap")
#undef ioremap
#undef iounmap
#include "tx_isp_t32_tuning.h"
#pragma pop_macro("iounmap")
#pragma pop_macro("ioremap")

#define TB_ISRA_LOC	512	/* >= the largest working block (AE weight 458 B) */

/* ---- tisp_tattr ---- */

static u8 tb_tattr_buf[T32_TATTR_BYTES];
static bool tb_tattr_ok;

static void *tb_tattr_alloc(u32 n) { return n <= sizeof(tb_tattr_buf) ? tb_tattr_buf : NULL; }
static void tb_tattr_free(void *p) { }
static int tb_tattr_irq(int chn, int irq, void *h) { return 0; }	/* tisp_top_frame_count: not used */
static int tb_tattr_printf(int level, const char *fmt, ...) { return 0; }

static u8 *tb_tattr(void)
{
	static const struct t32_tuning_ops k = {
		.vmalloc = tb_tattr_alloc, .vfree = tb_tattr_free, .isp_printf = tb_tattr_printf,
		.irq_func_set = tb_tattr_irq,
	};
	struct t32_tuning_state st = { 0 };

	if (!tb_tattr_ok && !t32_enable_tuning(&k, &st))
		tb_tattr_ok = true;
	return tb_tattr_buf;
}

/* ---- the AE / AWB block calls on the live contexts ---- */

#define TB_AE(c, none)	struct t32_aea_ctx c; if (!t32_aec_api_get(&c)) { none; }
#define TB_AWB(c, none)	struct t32_awb_ctx c; if (!t32_awbc_api_get(&c)) { none; }

static u32 tb_div32(u32 sh, u32 a, u32 b) { return t32_aem_div32(sh, a, b); }
static u32 tb_mult2_32(u32 sh, u32 a, u32 b) { return t32_aem_mult2_32(sh, a, b); }
static const struct t32_ae_ops tb_ae_ops = { .div32 = tb_div32, .mult2_32 = tb_mult2_32 };

static void tb_msleep(void *p, u32 ms) { t32_aec_api_sleep(ms); }
static u32 tb_log2(void *p, u32 a0, u32 a1, u32 a2) { return t32_log2_fixed_to_fixed(a0, a1, a2); }
static u32 tb_exp2(void *p, u32 a0, u32 a1, u32 a2) { return t32_aem_exp2(a0, a1, a2); }
static void tb_warn(void *p) { isp_printf(2, "tx-isp-t32: AE API: value refused\n"); }

static int tb_zero(void *buf, u32 n) { memset(buf, 0, n); return 0; }

/* AWB */
static int tb_awb_get_attr(u32 ch, void *buf)
{
	TB_AWB(c, return tb_zero(buf, 76))
	t32_awb_get_attr(&c, buf);
	t32_awbc_api_put();
	return 0;
}

static int tb_awb_get_weight(u32 ch, void *buf)
{
	TB_AWB(c, return tb_zero(buf, 1024))
	t32_awb_get_weight(&c, buf);
	t32_awbc_api_put();
	return 0;
}

static int tb_awb_set_weight(u32 ch, const void *buf)
{
	TB_AWB(c, return 0)
	t32_awb_set_weight(&c, buf);
	t32_awbc_api_put();
	return 0;
}

static int tb_awb_get_zone(u32 ch, void *buf)
{
	TB_AWB(c, return tb_zero(buf, 3 * 4096))
	t32_awb_get_zone(&c, buf);
	t32_awbc_api_put();
	return 0;
}

static int tb_awb_get_global_statis(u32 ch, void *buf)
{
	TB_AWB(c, return tb_zero(buf, 16))
	t32_awb_get_global_statis(&c, buf);
	t32_awbc_api_put();
	return 0;
}

static int tb_awb_get_ct_trend_offset(u32 ch, void *buf)
{
	TB_AWB(c, return tb_zero(buf, 28))
	t32_awb_api_get_ct_trend_offset(&c, buf);
	t32_awbc_api_put();
	return 0;
}

static int tb_awb_set_ct_trend_offset(u32 ch, const void *buf)
{
	TB_AWB(c, return 0)
	t32_awb_api_set_ct_trend_offset(&c, buf);
	t32_awbc_api_put();
	return 0;
}

static int tb_awb_set_mode(u32 ch, const u32 *mode3)
{
	TB_AWB(c, return 0)
	t32_awb_set_mode(&c, mode3);
	t32_awbc_api_put();
	return 0;
}

static void tb_awb_gain(void *p, u32 ch, const u32 *in, u32 a2, u32 a3) { t32_awbc_api_set_gain(); }

static int tb_awb_set_start(u32 ch, const void *start)
{
	TB_AWB(c, return 0)
	t32_awb_set_start(&c, ch, start, tb_awb_gain, NULL);
	t32_awbc_api_put();
	return 0;
}

static int tb_awb_set_frz(u32 ch, u32 frz)
{
	TB_AWB(c, return 0)
	t32_awb_set_frz(&c, frz);
	t32_awbc_api_put();
	return 0;
}

static int tb_awb_set_ct(u32 ch, const void *ct)
{
	TB_AWB(c, return 0)
	t32_awb_set_ct(&c, t32_ld32(ct, 0));
	t32_awbc_api_put();
	return 0;
}

static int tb_awb_set_statis_location(u32 ch, const u8 *loc)
{
	u32 w[3];

	TB_AWB(c, return 0)
	memcpy(w, loc, sizeof(w));
	t32_awb_set_statis_localtion(&c, w);
	t32_awbc_api_put();
	return 0;
}

/* AE */
static int tb_ae_get_weight(u32 ch, void *buf)
{
	TB_AE(c, return tb_zero(buf, 458))
	t32_aea_get_weight(&c, buf);
	t32_aec_api_put();
	return 0;
}

static int tb_ae_set_weight(u32 ch, const void *buf)
{
	TB_AE(c, return 0)
	t32_aea_set_weight(&c, buf);
	t32_aec_api_put();
	return 0;
}

/* api_ae_get_exprinfo_attr: the number of 10 ms waits (50 = the AE update flag stayed set) */
static int tb_ae_get_exprinfo(u32 ch, void *buf)
{
	u32 n;

	TB_AE(c, return tb_zero((u8 *)buf + 4, 216))
	n = t32_aea_get_exprinfo_attr(&c, buf, tb_msleep, tb_log2, NULL);
	t32_aec_api_put();
	return n;
}

static int tb_ae_set_exprinfo(u32 ch, const void *buf)
{
	TB_AE(c, return 0)
	t32_aea_set_exprinfo_attr(&c, buf, tb_exp2, NULL);
	t32_aec_api_put();
	return 0;
}

/* without the AE context the scene block is kept as set (AE start EV words 6 / 7 read back) */
static u8 tb_scene[3][40];

static int tb_ae_get_scene(u32 ch, void *buf)
{
	TB_AE(c, memcpy(buf, tb_scene[ch % 3], 40); return 0)
	t32_aea_get_scene_par(&c, buf, &tb_ae_ops);
	t32_aec_api_put();
	return 0;
}

static int tb_ae_set_scene(u32 ch, const void *buf)
{
	TB_AE(c, memcpy(tb_scene[ch % 3], buf, 40); return 0)
	t32_aea_set_scene_par(&c, buf);
	t32_aec_api_put();
	return 0;
}

static int tb_ae_get_exp_list(u32 ch, void *flag, void *list)
{
	TB_AE(c, tb_zero(flag, 4); return tb_zero(list, 320))
	t32_aea_get_exp_list(&c, flag, list);
	t32_aec_api_put();
	return 0;
}

static int tb_ae_set_exp_list(u32 ch, u32 n, const void *list)
{
	TB_AE(c, return 0)
	t32_aea_set_exp_list(&c, t32_aec_api_dcw(), n, list, tb_warn, NULL);
	t32_aec_api_put();
	return 0;
}

static int tb_ae_get_ev_list(u32 ch, void *buf)
{
	TB_AE(c, return tb_zero(buf, 128))
	t32_aea_get_ev_list(&c, buf);
	t32_aec_api_put();
	return 0;
}

static int tb_ae_set_ev_list(u32 ch, u32 n, const void *list)
{
	TB_AE(c, return 0)
	t32_aea_set_ev_list(&c, n, list);
	t32_aec_api_put();
	return 0;
}

static int tb_ae_get_at_list(u32 ch, void *buf)
{
	TB_AE(c, return tb_zero(buf, 128))
	t32_aea_get_at_list(&c, buf);
	t32_aec_api_put();
	return 0;
}

static int tb_ae_set_at_list(u32 ch, u32 n, const void *list)
{
	TB_AE(c, return 0)
	t32_aea_set_at_list(&c, n, list);
	t32_aec_api_put();
	return 0;
}

static int tb_ae_get_bv(u32 ch, void *buf)
{
	u32 bv;

	TB_AE(c, return 0)
	bv = t32_aec_api_bv();
	t32_aec_api_put();
	return bv;
}

static int tb_ae_get_stable(u32 ch, void *buf)
{
	TB_AE(c, return tb_zero(buf, 12))
	t32_aea_get_stable(&c, buf);
	t32_aec_api_put();
	return 0;
}

static int tb_ae_get_ae_statis(u32 ch, void *buf)
{
	TB_AE(c, return tb_zero(buf, 1934))
	t32_aea_get_ae_statis(&c, buf);
	t32_aec_api_put();
	return 0;
}

static int tb_ae_get_flicker_flag(u32 ch, void *buf)
{
	TB_AE(c, return tb_zero(buf, 4))
	t32_wr32(buf, 0, t32_aea_get_flicker_flag(&c));
	t32_aec_api_put();
	return 0;
}

static int tb_ae_get_roi_luma(u32 ch, void *o1, void *o2)
{
	TB_AE(c, tb_zero(o1, 4); return tb_zero(o2, 4))
	t32_aea_get_gobal_roi_luma(&c, o1, o2);
	t32_aec_api_put();
	return 0;
}

static int tb_ae_get_face_tune(u32 ch, void *o1, void *o2, void *o3)
{
	TB_AE(c, tb_zero(o1, 4); tb_zero(o2, 4); return tb_zero(o3, 4))
	t32_aea_get_face_ae_tune_params(&c, o1, o2, o3);
	t32_aec_api_put();
	return 0;
}

/*
 * openimp #350 (beyond vendor, user-approved 2026-11-11): the third word
 * (R + 1540) is the length of the AE smoothing window at R + 1300 (20 words
 * before R + 1380; default 15). The vendor bounds it nowhere; a value above
 * 221 makes the next AE frame (tx_isp_t32_aefu.h, nR) write past the 2184 B
 * state block (kernel oops). t32_aea_set_face_ae_tune_params refuses > 20
 * with -EINVAL; 0..20 behave like the vendor (0 -> 1, return 0).
 */
static int tb_ae_set_face_tune(u32 ch, const void *p0, const void *p1, const void *p2)
{
	int rc;

	TB_AE(c, return 0)
	rc = t32_aea_set_face_ae_tune_params(&c, p0, p1, (u32 *)p2, tb_warn, NULL);	/* the vendor writes 1 back */
	t32_aec_api_put();
	return rc;
}

static int tb_ae_set_hist_bin(u32 ch, const u8 *bins)
{
	TB_AE(c, return 0)
	t32_aea_set_hist_bin(&c, bins);
	t32_aec_api_put();
	return 0;
}

/* tisp_af_api_get_attr on the live AF block; before it: the attribute as the statistics config holds it */
static int tb_af_get_attr(u32 ch, void *buf)
{
	const u8 *a = tb_tattr() + ch * 72 + T32_STAT_ATTR;
	u8 *af = buf;

	if (t32_afgsm_af_get_attr(ch, af))
		return 0;
	memset(af, 0, 24);
	af[8] = a[44];
	af[9] = a[60];
	memcpy(af + 10, a + 62, 8);
	af[18] = a[48];
	af[19] = a[52];
	af[20] = a[56];
	af[21] = a[57];
	memcpy(af + 22, a + 70, 2);
	return 0;
}

static int tb_af_set_attr(u32 ch, const u8 *af) { t32_afgsm_af_set_attr(ch, af); return 0; }
static int tb_gsm_api_set(u32 ch, const u8 *scratch) { t32_afgsm_gsm_api_set(ch, scratch); return 0; }
static void *tb_vmalloc(u32 n) { return vzalloc(n); }
static void tb_vfree(void *p) { vfree(p); }

static const struct t32_get_ops tb_get = {
	.awb_get_attr = tb_awb_get_attr,
	.awb_get_weight = tb_awb_get_weight,
	.awb_get_zone = tb_awb_get_zone,
	.awb_get_global_statis = tb_awb_get_global_statis,
	.awb_api_get_ct_trend_offset = tb_awb_get_ct_trend_offset,
	.api_ae_get_weight = tb_ae_get_weight,
	.api_ae_get_exprinfo_attr = tb_ae_get_exprinfo,
	.ae_api_get_scene_par = tb_ae_get_scene,
	.ae_get_ev_list = tb_ae_get_ev_list,
	.ae_get_at_list = tb_ae_get_at_list,
	.ae_get_bv = tb_ae_get_bv,
	.ae_api_get_stable = tb_ae_get_stable,
	.ae_get_ae_statis = tb_ae_get_ae_statis,
	.ae_api_get_flicker_flag = tb_ae_get_flicker_flag,
	.af_api_get_attr = tb_af_get_attr,
	.ae_api_get_exp_list = tb_ae_get_exp_list,
	.ae_api_get_gobal_roi_luma = tb_ae_get_roi_luma,
	.ae_api_get_face_ae_tune_params = tb_ae_get_face_tune,
};

static const struct t32_api_set_ops tb_set = {
	.awb_set_weight = tb_awb_set_weight,
	.awb_api_set_ct_trend_offset = tb_awb_set_ct_trend_offset,
	.ae_set_weight = tb_ae_set_weight,
	.ae_set_exprinfo_attr = tb_ae_set_exprinfo,
	.ae_api_set_scene_par = tb_ae_set_scene,
	.ae_api_set_exp_list = tb_ae_set_exp_list,
	.ae_set_ev_list = tb_ae_set_ev_list,
	.ae_set_at_list = tb_ae_set_at_list,
	.ae_api_set_face_ae_tune_params = tb_ae_set_face_tune,
};

static const struct t32_api_attr_ops tb_attr = {
	.awb_set_mode = tb_awb_set_mode,
	.awb_set_start = tb_awb_set_start,
	.awb_set_frz = tb_awb_set_frz,
	.awb_set_ct = tb_awb_set_ct,
};

static const struct t32_api_stat_ops tb_stat = {
	.vmalloc = tb_vmalloc, .vfree = tb_vfree, .ae_set_hist_bin = tb_ae_set_hist_bin,
	.gsm_api_set = tb_gsm_api_set, .awb_set_statis_location = tb_awb_set_statis_location,
	.af_api_set_attr = tb_af_set_attr,
};

/* ---- the tisp_[sg]_* entry points (vendor names) ---- */

static int tb_g_awb_attr(u32 ch, u8 *buf) { return t32_g_awb_attr(&tb_get, ch, buf, tb_tattr()); }
static int tb_s_awb_attr(u32 ch, u8 *buf) { return t32_s_awb_attr(&tb_attr, tb_tattr(), ch, buf); }
static int tb_g_awb_weight_attr(u32 ch, u8 *buf) { return t32_g_awb_weight_attr(&tb_get, ch, buf); }
static int tb_s_awb_weight_attr(u32 ch, u8 *buf) { return t32_s_awb_weight_attr(&tb_set, ch, buf); }
static int tb_g_ae_weight_attr(u32 ch, u8 *buf) { return t32_g_ae_weight_attr(&tb_get, ch, buf); }
static int tb_s_ae_weight_attr(u32 ch, u8 *buf) { return t32_s_ae_weight_attr(&tb_set, ch, buf); }
static int tb_g_ae_exprinfo_attr(u32 ch, u8 *buf) { return t32_g_ae_exprinfo_attr(&tb_get, ch, buf, tb_tattr()); }
static int tb_s_ae_exprinfo_attr(u32 ch, u8 *buf) { return t32_s_ae_exprinfo_attr(&tb_set, tb_tattr(), ch, buf); }
static int tb_g_ae_scence_attr(u32 ch, u8 *buf) { return t32_g_ae_scence_attr(&tb_get, ch, buf); }
static int tb_s_ae_scence_attr(u32 ch, u8 *buf) { return t32_s_ae_scence_attr(&tb_set, ch, buf); }
static int tb_g_ae_explist(u32 ch, u8 *buf) { return t32_g_ae_explist(&tb_get, ch, buf); }
static int tb_s_ae_explist(u32 ch, u8 *buf) { return t32_s_ae_explist(&tb_set, ch, buf); }
static int tb_g_at_list(u32 ch, u8 *buf) { return t32_g_at_list(&tb_get, ch, buf); }
static int tb_s_at_list(u32 ch, u8 *buf) { return t32_s_at_list(&tb_set, ch, buf); }
static int tb_g_ev_list(u32 ch, u8 *buf) { return t32_g_ev_list(&tb_get, ch, buf); }
static int tb_s_ev_list(u32 ch, u8 *buf) { return t32_s_ev_list(&tb_set, ch, buf); }
static int tb_g_bv(u32 ch, u8 *buf) { return t32_g_bv(&tb_get, ch, (u32 *)buf); }
static int tb_get_ae_stable(u32 ch, u8 *buf) { return t32_get_ae_stable(&tb_get, ch, buf); }

/* ---- the .isra wrappers (t32_isra_*): user copies, prints, calls ---- */

struct tb_call {
	const char *fn;
	int (*f[4])(u32 ch, u8 *buf);
};

static int tb_in(void *p, u8 *dst, u32 user, u32 n) { return (int)t32_tx_os_copy_from_user(dst, user, n); }
static int tb_out(void *p, u32 user, const u8 *src, u32 n) { return (int)t32_tx_os_copy_to_user(user, src, n); }

static void tb_print(void *p, u32 level)
{
	const struct tb_call *c = p;

	isp_printf(level, level == 2 ? "[ %s ] copy error!!!\n" : "[ %s ] control failed!!!\n", c->fn);
}

static int tb_call1(void *p, u32 ch, u8 *buf) { return ((struct tb_call *)p)->f[0](ch, buf); }
static int tb_call2(void *p, u32 ch, u8 *buf) { return ((struct tb_call *)p)->f[1](ch, buf); }
static int tb_calln(void *p, u32 id, u32 ch, u8 *buf) { return ((struct tb_call *)p)->f[id - 1](ch, buf); }

#define TB_OPS(name, call)	struct t32_isra_ops name = { tb_in, tb_out, tb_print, tb_call1, (void *)&(call), \
							     tb_call2, tb_calln }

/* the vendor's working blocks are its zeroed stack frames: static here (callers hold the tuning lock) */
static u8 tb_loc[TB_ISRA_LOC], tb_io[TB_ISRA_LOC], tb_l2[128], tb_l3[128], tb_l4[16];

static void tb_clear(void)
{
	memset(tb_loc, 0, sizeof(tb_loc));
	memset(tb_io, 0, sizeof(tb_io));
	memset(tb_l2, 0, sizeof(tb_l2));
	memset(tb_l3, 0, sizeof(tb_l3));
	memset(tb_l4, 0, sizeof(tb_l4));
}

static u32 tb_user(u32 arg) { return t32_tx_ld(arg, 0); }

int t32_tx_os_awb_attr_g_attr(u32 ch, u32 arg)
{
	static const struct tb_call c = { "tx_isp_awb_attr_g_attr", { tb_g_awb_attr } };
	TB_OPS(o, c);

	tb_clear();
	return t32_isra_awb_attr_g(&o, ch, tb_user(arg), tb_loc, tb_io);
}

int t32_tx_os_awb_attr_s_attr(u32 ch, u32 user)
{
	static const struct tb_call c = { "tx_isp_awb_attr_s_attr", { tb_g_awb_attr, tb_s_awb_attr } };
	TB_OPS(o, c);

	tb_clear();
	return t32_isra_awb_attr_s(&o, ch, user, tb_io, tb_loc);
}

int t32_tx_os_awb_only_read_attr(u32 ch, u32 arg)
{
	static const struct tb_call c = { "tx_isp_awb_only_read_attr", { tb_g_awb_attr } };
	TB_OPS(o, c);

	tb_clear();
	return t32_isra_awb_only_read(&o, ch, tb_user(arg), tb_loc);
}

int t32_tx_os_awb_weight_g_attr(u32 ch, u32 arg)
{
	static u8 w[T32_ISRA_AWB_WEIGHT];
	static const struct tb_call c = { "tx_isp_awb_weight_g_attr", { tb_g_awb_weight_attr } };
	TB_OPS(o, c);

	memset(w, 0, sizeof(w));
	return t32_isra_awb_weight_g(&o, ch, tb_user(arg), w);
}

int t32_tx_os_awb_weight_s_attr(u32 ch, u32 user)
{
	static u8 w[T32_ISRA_AWB_WEIGHT];
	static const struct tb_call c = { "tx_isp_awb_weight_s_attr", { tb_s_awb_weight_attr } };
	TB_OPS(o, c);

	memset(w, 0, sizeof(w));
	return t32_isra_awb_weight_s(&o, ch, user, w);
}

int t32_tx_os_ae_weight_g_attr(u32 ch, u32 arg)
{
	static const struct tb_call c = { "tx_isp_ae_weight_g_attr", { tb_g_ae_weight_attr } };
	TB_OPS(o, c);

	tb_clear();
	return t32_isra_ae_weight_g(&o, ch, tb_user(arg), tb_loc, tb_io);
}

int t32_tx_os_ae_weight_s_attr(u32 ch, u32 user)
{
	static const struct tb_call c = { "tx_isp_ae_weight_s_attr", { tb_s_ae_weight_attr } };
	TB_OPS(o, c);

	tb_clear();
	return t32_isra_ae_weight_s(&o, ch, user, tb_io, tb_loc);
}

int t32_tx_os_ae_expoinfo_g_attr(u32 ch, u32 arg)
{
	static const struct tb_call c = { "tx_isp_ae_expoinfo_g_attr", { tb_g_ae_exprinfo_attr } };
	TB_OPS(o, c);

	tb_clear();
	return t32_isra_ae_expoinfo_g(&o, ch, tb_user(arg), tb_loc, tb_io);
}

int t32_tx_os_ae_expoinfo_s_attr(u32 ch, u32 user)
{
	static const struct tb_call c = { "tx_isp_ae_expoinfo_s_attr", { tb_s_ae_exprinfo_attr } };
	TB_OPS(o, c);

	tb_clear();
	return t32_isra_ae_expoinfo_s(&o, ch, user, tb_io, tb_loc);
}

int t32_tx_os_ae_scence_g_attr(u32 ch, u32 arg)
{
	static const struct tb_call c = { "tx_isp_ae_scence_g_attr", { tb_g_ae_scence_attr } };
	TB_OPS(o, c);

	tb_clear();
	return t32_isra_ae_scence_g(&o, ch, tb_user(arg), tb_loc);
}

int t32_tx_os_ae_explist_g_attr(u32 ch, u32 arg)
{
	static const struct tb_call c = { "tx_isp_ae_explist_g_attr", { tb_g_ae_explist } };
	TB_OPS(o, c);

	tb_clear();
	return t32_isra_ae_explist_g(&o, ch, tb_user(arg), tb_loc);
}

int t32_tx_os_ae_explist_s_attr(u32 ch, u32 user)
{
	static const struct tb_call c = { "tx_isp_ae_explist_s_attr", { tb_s_ae_explist } };
	TB_OPS(o, c);

	tb_clear();
	return t32_isra_ae_explist_s(&o, ch, user, tb_loc);
}

int t32_tx_os_ae_target_g_attr(u32 ch, u32 arg)
{
	static const struct tb_call c = { "tx_isp_ae_target_g_attr", { tb_g_ae_scence_attr, tb_g_at_list, tb_g_ev_list } };
	TB_OPS(o, c);

	tb_clear();
	return t32_isra_ae_target_g(&o, ch, tb_user(arg), tb_loc, tb_l2, tb_l3, tb_io);
}

int t32_tx_os_ae_target_s_attr(u32 ch, u32 user)
{
	static const struct tb_call c = { "tx_isp_ae_target_s_attr",
					  { tb_g_ae_scence_attr, tb_s_ae_scence_attr, tb_s_at_list, tb_s_ev_list } };
	TB_OPS(o, c);

	tb_clear();
	return t32_isra_ae_target_s(&o, ch, user, tb_io, tb_loc, tb_l2, tb_l3);
}

int t32_tx_os_ae_hlc_or_blc_g_attr(u32 ch, u32 arg)
{
	static const struct tb_call c = { "tx_isp_ae_hlc_or_blc_g_attr", { tb_g_ae_scence_attr } };
	TB_OPS(o, c);

	tb_clear();
	return t32_isra_ae_hlc_or_blc_g(&o, ch, tb_user(arg), tb_loc, tb_io);
}

int t32_tx_os_ae_only_read_g_attr(u32 ch, u32 arg)
{
	static const struct tb_call c = { "tx_isp_ae_only_read_g_attr",
					  { tb_g_ae_exprinfo_attr, tb_g_ae_scence_attr, tb_g_bv, tb_get_ae_stable } };
	TB_OPS(o, c);

	tb_clear();
	return t32_isra_ae_only_read_g(&o, ch, tb_user(arg), tb_loc, tb_l2, tb_l3, tb_l4, tb_io);
}

/* ---- the tisp_[sg]_* hooks the dispatchers call directly ---- */

int t32_tx_os_tisp_g_ae_scence_attr(u32 ch, void *buf) { return tb_g_ae_scence_attr(ch, buf); }
int t32_tx_os_tisp_s_ae_scence_attr(u32 ch, void *buf) { return tb_s_ae_scence_attr(ch, buf); }
int t32_tx_os_tisp_g_awb_statis_attr(u32 ch, void *buf) { return t32_g_awb_statis_attr(&tb_get, ch, buf); }
int t32_tx_os_tisp_g_awb_global_statis_attr(u32 ch, void *buf) { return t32_g_awb_global_statis_attr(&tb_get, ch, buf); }
int t32_tx_os_tisp_g_awb_ct_trend_offset(u32 ch, void *buf) { return t32_g_awb_ct_trend_offset(&tb_get, ch, buf); }
int t32_tx_os_tisp_s_awb_ct_trend_offset(u32 ch, void *buf) { return t32_s_awb_ct_trend_offset(&tb_set, ch, buf); }
int t32_tx_os_tisp_g_ae_statis_attr(u32 ch, void *buf) { return t32_g_ae_statis_attr(&tb_get, ch, buf); }
int t32_tx_os_tisp_g_ae_flicker_flag(u32 ch, void *buf) { return t32_g_ae_flicker_flag(&tb_get, ch, buf); }
int t32_tx_os_tisp_g_face_ae_luma(u32 ch, void *buf) { return t32_g_face_ae_luma(&tb_get, ch, buf); }
int t32_tx_os_tisp_g_face_ae_weiget(u32 ch, void *buf) { return t32_g_face_ae_weiget(&tb_get, ch, buf); }
int t32_tx_os_tisp_s_face_ae_weiget(u32 ch, void *buf) { return t32_s_face_ae_weiget(&tb_set, ch, buf); }
int t32_tx_os_tisp_g_statis_config(u32 ch, void *buf) { return t32_g_statis_config_attr(&tb_get, ch, buf, tb_tattr()); }
int t32_tx_os_tisp_s_statis_config(u32 ch, void *buf) { return t32_s_statis_config_attr(&tb_stat, tb_tattr(), ch, buf); }
