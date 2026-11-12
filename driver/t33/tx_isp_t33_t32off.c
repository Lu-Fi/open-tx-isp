// SPDX-License-Identifier: GPL-2.0
/*
 * The T32 per-frame blocks (tisp per-frame paths, AE / AWB / ct fan-out,
 * LSC ct, LCE / TMO / AF / GSM block interrupts, ISP OSD, isp-m0 tuning
 * controls) that the shared driver/t32 code calls, switched off for the T33
 * module.  The T33 runs its own per-frame AE / AWB (tx_isp_t33_aec.c,
 * tx_isp_t33_awbc.c) and register lists; the T32 objects are not built
 * here.  t32_pp_on() is false, so the shared tisp init skips every T32
 * per-frame attach.  The tuning controls (tx_isp_t33_tunctl.c / _tunblk.c)
 * are built: their AE / AWB ids run on the T33 AE / AWB blocks.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include "tx_isp_t32.h"

struct t32_aec_drv_stats t32_aec_stats;
struct t32_afg_drv_stats t32_afg_stats;
struct t32_awbc_drv_stats t32_awbc_stats;
struct t32_blk_drv_stats t32_blk_stats;
struct t32_ctup_drv_stats t32_ctup_stats;
struct t32_lsc_drv_stats t32_lsc_drv_stats;
struct t32_pf_stats t32_pf_stats;
struct t32_tmo_drv_stats t32_tmo_stats;

bool t32_pp_on(enum t32_pp blk) { return false; }
void t32_pf_reset(void) { }
void t32_pf_attach(enum t32_pp blk, u8 *sec) { kfree(sec); }
void t32_pf_attach_gib(u8 *sec, u32 bayer) { kfree(sec); }
void t32_pf_attach_lce(const u8 *sec, u32 width, u32 height) { }
void t32_pf_attach_lsc(const u8 *sec, const struct t32_lsc *s, u32 width, u32 height, u32 bayer, u32 ct) { }
void t32_pf_attach_mdns(u8 *sec, u8 *val, u32 width, u32 height, u32 memopt) { kfree(sec); kfree(val); }
int t32_pf_attach_rdns(u8 *sec, struct t32_front_write *w) { kfree(sec); return 0; }
void t32_pf_kick(void) { }
void t32_pf_stop(void) { }
void t32_pf_exit(void) { }

void t32_afgsm_attach_af(const u8 *bin, u32 bytes, u32 width, u32 height) { }
void t32_afgsm_attach_gsm(u32 width, u32 height) { }
void t32_afgsm_exit(void) { }
/* the tuning API on the AF / GSM blocks: not attached on the T33 yet (tunblk answers the stored / zero words) */
bool t32_afgsm_af_get_attr(u32 ch, u8 *out) { return false; }
bool t32_afgsm_af_set_attr(u32 ch, const u8 *in) { return false; }
bool t32_afgsm_af_set_weight(u32 ch, const u8 *buf) { return false; }
bool t32_afgsm_af_get_weight(u32 ch, u8 *buf) { return false; }
bool t32_afgsm_af_get_statis(u32 ch, u8 *buf) { return false; }
bool t32_afgsm_af_get_metric(u32 ch, u8 *buf) { return false; }
bool t32_afgsm_gsm_api_set(u32 ch, const u8 *scratch) { return false; }
void t32_awbc_detach(void) { }
void t32_blk_exit(void) { }
void t32_blk_lce_bufs(void *curve, void *stat, void *hist) { }
int t32_ctup_attach_bcsh(const u8 *sec, u32 len, u32 avail, const u8 *csc) { return -ENODEV; }
int t32_ctup_attach_ccm(const u8 *sec, u32 len, u32 avail) { return -ENODEV; }
int t32_ctup_attach_clm(const u8 *sec, u32 len, u32 avail) { return -ENODEV; }
void t32_ctup_detach(void) { }
void t32_lsc_drv_attach(const u8 *blk, const struct t32_lsc *s, const struct t32_lsc_work *wk,
			u32 width, u32 height, u32 bayer, u32 ct) { }
void t32_lsc_drv_detach(void) { }
void t32_lsc_drv_exit(void) { }
void t32_lsc_drv_frame_done(void) { }
void t32_tmo_attach(const u8 *bin, u32 bytes, u32 width, u32 height) { }

long t32_isposd_ctrl(u32 vi, u32 get, u32 id, u32 uptr) { return -ENOIOCTLCMD; }
void t32_isposd_frame_done(void) { }
void t32_isposd_msca_start(u32 in_w, u32 in_h) { }
unsigned long t32_isposd_ram_lock(void) { return 0; }
void t32_isposd_ram_unlock(unsigned long flags) { }

