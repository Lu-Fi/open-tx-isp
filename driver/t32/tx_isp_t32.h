/* SPDX-License-Identifier: GPL-2.0 */
/* T32 driver-internal declarations. */
#ifndef TX_ISP_T32_H
#define TX_ISP_T32_H

#include <linux/types.h>
#include <linux/compiler.h>

/* Linux 3.10 (T32-2.1.0-3.10.14 vendor tree) predates READ_ONCE/WRITE_ONCE. */
#ifndef READ_ONCE
#define READ_ONCE(x)		ACCESS_ONCE(x)
#endif
#ifndef WRITE_ONCE
#define WRITE_ONCE(x, val)	(ACCESS_ONCE(x) = (val))
#endif

struct i2c_driver;
struct platform_device;

extern int t32_print_level;

int isp_printf(unsigned int level, const char *fmt, ...);

/* tx_isp_t32_core.c */
int tx_isp_t32_core_init(void);
void tx_isp_t32_core_exit(void);
void t32_core_sensor_driver_gone(struct i2c_driver *drv);
int tx_isp_subdev_init(struct platform_device *pdev, void *sd, void *ops);
void tx_isp_subdev_deinit(void *sd);

u32 t32_core_sensor_fps(void);
int t32_core_sensor_size(u32 *w, u32 *h);
unsigned int t32_core_memopt(void);
int t32_core_isp_mem(u32 *pa);
int t32_core_iq_bin(const u8 **bin, u32 *bytes);
int t32_core_sensor_mbus(u32 *code);
int t32_core_clk_active(void);
int t32_core_sensor_expo(u32 packed);
u32 t32_core_sensor_vts(void);
int t32_core_sensor_ae_src(const u8 **video_in, const u8 **attr);
u32 t32_core_sensor_alloc_again(u32 log2_q16, u32 *reg);
int t32_core_sensor_set_fps(u32 fps);

/* tx_isp_t32_tunctl.c: isp-m0 tuning controls, AE start EV */
long t32_tunctl_ctrl(u32 *w, unsigned long arg);
long t32_tunctl_ae_start(unsigned int cmd, unsigned long arg);
void t32_tunctl_frame_done(u32 ch);

/* tx_isp_t32_hw.c */
struct t32_hw_stats {
	u32 isp_irqs, vic_irqs, frame_ends, stream_ons;
	u32 ae_irqs, ae_hist_irqs, awb_irqs, stats_inits;
	u32 bringup_steps, bringup_mean16, bringup_it;
};
extern struct t32_hw_stats t32_hw_stats;
int t32_hw_init(void);
void t32_hw_exit(void);
int t32_hw_input_setup(const void *attr, u32 w, u32 h);
void t32_hw_rcfg_run_stream_on(void);
void t32_hw_rcfg_lsc_trig(u32 list_pa);
extern u32 t32_rcfg_runs, t32_rcfg_timeouts, t32_rcfg_last_len;
void t32_hw_stream_on(void);
void t32_hw_stream_off(void);
void t32_hw_msca_push(int ch, u32 y, u32 uv);
void t32_hw_msca_start(int ch, u32 w, u32 h, u32 stride, u32 in_w, u32 in_h);
void t32_hw_msca_stop(int ch);
int t32_hw_msca_pop_done(int ch, u32 *y, u32 *uv);

u32 t32_hw_rd(u32 off);
/* ISP-drawn OSD (tx_isp_t32_isposd.c, #336) */
long t32_isposd_ctrl(u32 vi, u32 get, u32 id, u32 uptr);
void t32_isposd_frame_done(void);
void t32_isposd_msca_start(u32 in_w, u32 in_h);
void t32_isposd_status(u32 *pending, u32 *refused, u32 *fixes);
unsigned long t32_isposd_ram_lock(void);
void t32_isposd_ram_unlock(unsigned long flags);
void t32_hw_wr(u32 off, u32 v);

/* tx_isp_t32_ae.c: the per-frame vendor AE chain (tx_isp_t32_aechain.h), behind T32_PP_AE */
struct t32_ae_blk;
struct t32_aec_drv_stats {
	u32 frames, events, writes, expo_sets, fps_sets;
	u32 mean, target, it, again, dgain, tgain;	/* the last frame */
};
extern struct t32_aec_drv_stats t32_aec_stats;
int t32_aec_attach(const struct t32_ae_blk *b, u8 *S, const u8 *zones);
void t32_aec_detach(void);
void t32_aec_kick(void);
bool t32_aec_tgain(u32 *g);
struct t32_aea_ctx;
bool t32_aec_api_get(struct t32_aea_ctx *c);
void t32_aec_api_put(void);
void t32_aec_api_sleep(u32 ms);
u32 t32_aec_api_bv(void);
u32 t32_aec_api_dcw(void);
const u8 *t32_hw_ae_zones(void);

/* tx_isp_t32_awbc.c: the per-frame vendor AWB (tisp_awb_init state, tisp_awb_process), behind T32_PP_AWB */
struct t32_awbc_drv_stats {
	u32 frames, writes, refused;
	u32 ct, gain_r, gain_b;		/* the last frame */
	u32 ev_updates, tgain_updates;	/* AE inputs (events 6 / 4) */
};
extern struct t32_awbc_drv_stats t32_awbc_stats;
int t32_awbc_attach(const u8 *sec, u32 width, u32 height, const u8 *bufs);
void t32_awbc_detach(void);
void t32_awbc_kick(void);
bool t32_awbc_ct(u32 *ct);
void t32_awbc_ev(u32 ev);
void t32_awbc_tgain(u32 gain);

/* tx_isp_t32_ctup.c: the colour temperature fan-out (tisp_ct_update) after each AWB frame, behind T32_PP_CT */
#define T32_CTUP_MAX_WRITES	1024	/* the CLM LUT: 2 x 422 + 2 */
struct t32_ctup_drv_stats {
	u32 runs, writes, ct, ev_runs;
};
extern struct t32_ctup_drv_stats t32_ctup_stats;
int t32_ctup_attach_ccm(const u8 *sec, u32 len, u32 avail);
int t32_ctup_attach_bcsh(const u8 *sec, u32 len, u32 avail, const u8 *csc);
int t32_ctup_attach_clm(const u8 *sec, u32 len, u32 avail);
void t32_ctup_detach(void);
void t32_ctup_ev(u32 lo, u32 hi, u32 x, u32 y);
void t32_ctup_run(u32 ct);
struct t32_awb_ctx;
bool t32_awbc_api_get(struct t32_awb_ctx *c);
void t32_awbc_api_put(void);
void t32_awbc_api_set_gain(void);

/* tx_isp_t32_tisp.c: per-frame tisp paths (ported blocks), switch t32_ported_paths */
enum t32_pp { T32_PP_DMSC, T32_PP_YSP, T32_PP_CDNS, T32_PP_YDNS, T32_PP_SDNS, T32_PP_MDNS, T32_PP_DPC, T32_PP_GIB,
	     T32_PP_RDNS, T32_PP_AE, T32_PP_LSC, T32_PP_LCE, T32_PP_TMO, T32_PP_AWB, T32_PP_CT, T32_PP_EV,
	     T32_PP_AF, T32_PP_GSM, T32_PP_LSCCT, T32_PP_COUNT };
struct t32_pf_stats {
	u32 runs, writes;
};
extern struct t32_pf_stats t32_pf_stats;
bool t32_pp_on(enum t32_pp blk);
void t32_pf_reset(void);
void t32_pf_attach(enum t32_pp blk, u8 *sec);
void t32_pf_attach_mdns(u8 *sec, u8 *val, u32 width, u32 height, u32 memopt);
void t32_pf_attach_gib(u8 *sec, u32 bayer);
struct t32_front_write;
int t32_pf_attach_rdns(u8 *sec, struct t32_front_write *w);
int t32_pf_attach_ae(const u8 *sec, u32 width, u32 height);
struct t32_lsc;
void t32_pf_attach_lsc(const u8 *sec, const struct t32_lsc *s, u32 width, u32 height, u32 bayer, u32 ct);
void t32_pf_attach_lce(const u8 *sec, u32 width, u32 height);

/* tx_isp_t32_lscct.c: the LSC colour temperature path (tisp_lsc_ct_update, list run at the LSC frame done),
 * behind T32_PP_LSCCT (with T32_PP_LSC, T32_PP_CT) */
struct t32_lsc_drv_stats {
	u32 attaches, ct_calls, mesh_writes, ct, tgains, frames, list_runs, waits, timeouts;
};
extern struct t32_lsc_drv_stats t32_lsc_drv_stats;
struct t32_lsc_work;
void t32_lsc_drv_attach(const u8 *blk, const struct t32_lsc *s, const struct t32_lsc_work *wk,
			u32 width, u32 height, u32 bayer, u32 ct);
void t32_lsc_drv_detach(void);
void t32_lsc_drv_exit(void);
bool t32_lsc_drv_on(void);
void t32_lsc_drv_ct(u32 ch, u32 ct);
bool t32_lsc_drv_tgain(u32 ch, u32 gain);
void t32_lsc_drv_frame_done(void);
void t32_pf_stop(void);
void t32_pf_start(void);
void t32_pf_kick(void);
void t32_pf_exit(void);

/* tx_isp_t32_blkirq.c: block interrupt paths (vendor LCE bits 8 / 30, event 14, behind T32_PP_LCE;
 * TMO bits 9 / 31, behind T32_PP_TMO) */
#define T32_BLK_IRQ_LCE_BITS	((1U << 8) | (1U << 30))
#define T32_BLK_IRQ_TMO_BITS	((1U << 9) | (1U << 31))
#define T32_BLK_IRQ_MASK	(T32_BLK_IRQ_LCE_BITS | T32_BLK_IRQ_TMO_BITS)
struct t32_blk_drv_stats {
	u32 attaches, lce_irqs, lsf_irqs, events, ev_full;
};
extern struct t32_blk_drv_stats t32_blk_stats;
void t32_blk_irq(u32 sta);
void t32_blk_lce_bufs(void *curve, void *stat, void *hist);
void t32_blk_lce_attach(u8 *st, u32 width, u32 height);
void t32_blk_lce_detach(void);
void t32_blk_exit(void);
void t32_blk_tmo_set(u32 (*tmo)(u32 ch), u32 (*fbf)(u32 ch));

/* tx_isp_t32_tmo.c: the vendor TMO / ADR block (tisp_tmo_init, bits 9 / 31), behind T32_PP_TMO */
struct t32_tmo_drv_stats {
	u32 attaches, tmo_irqs, fbf_irqs, tgains;
};
extern struct t32_tmo_drv_stats t32_tmo_stats;
void t32_tmo_attach(const u8 *bin, u32 bytes, u32 width, u32 height);
void t32_tmo_detach(void);
void t32_tmo_tgain(u32 ch, u64 gain);

/* tx_isp_t32_afgsm.c: the vendor AF / GSM blocks (tisp_af_init, tisp_gsm_init, bits 6 / 7 / 63, events 15 / 16),
 * behind T32_PP_AF / T32_PP_GSM */
struct t32_afg_drv_stats {
	u32 af_attaches, gsm_attaches, af_irqs, gsm_irqs, af_events, gsm_events, ev_full;
};
extern struct t32_afg_drv_stats t32_afg_stats;
void t32_afgsm_attach_af(const u8 *bin, u32 bytes, u32 width, u32 height);
void t32_afgsm_attach_gsm(u32 width, u32 height);
void t32_afgsm_detach(void);
void t32_afgsm_exit(void);
void t32_afgsm_irq(u32 sta, u32 sta3);
void t32_afgsm_tgain(u32 ch, u32 gain);
bool t32_afgsm_af_get_attr(u32 ch, u8 *out);
bool t32_afgsm_af_set_attr(u32 ch, const u8 *in);
bool t32_afgsm_af_set_weight(u32 ch, const u8 *buf);
bool t32_afgsm_af_get_weight(u32 ch, u8 *buf);
bool t32_afgsm_af_get_statis(u32 ch, u8 *buf);
bool t32_afgsm_af_get_metric(u32 ch, u8 *buf);
bool t32_afgsm_gsm_api_set(u32 ch, const u8 *scratch);

/* tx_isp_t32_fs.c */
struct seq_file;
int t32_fs_init(void);
void t32_fs_exit(void);
void t32_fs_stop_all(void);
void t32_fs_done_irq(u32 done);
void t32_fs_show(struct seq_file *m);
int t32_fs_ivdc_frame(u32 ch);
int t32_fs_crop(int ch, u32 *pos, u32 *size);
void t32_fs_snap_pause(int restart);

/* tx_isp_t32_snapldc.c: RAW snapshot, online LDC requests, /dev/offline_ldc (openimp #491) */
int t32_snapldc_init(void);
void t32_snapldc_exit(void);
long t32_snap_ioctl(unsigned int cmd, unsigned long arg);
long t32_ldc_online_ioctl(unsigned int cmd, unsigned long arg, u32 ldc_mode);
void t32_snap_isp_irq(u32 sta2, u32 sta3);
void t32_snap_vic_irq(u32 ch, u32 s, u32 s2);
void t32_snapldc_show(struct seq_file *m);
void t32_ldc_probe_count(void);

/* tx_isp_t32_ivdcdrv.c: IVDC direct mode (#349) */
int t32_ivdc_init(void);
void t32_ivdc_exit(void);
int t32_ivdc_direct(int ch);
long t32_ivdc_misc_ioctl(unsigned int cmd, unsigned long arg);
void t32_ivdc_stream(int on);
int t32_ivdc_set_fmt(u8 *fmt);
int t32_ivdc_reqbufs(u32 count);
int t32_ivdc_fs_stream(int on);
void t32_ivdc_show(struct seq_file *m);
#ifdef TX_ISP_T33
void t33_stats_show(struct seq_file *m);	/* tx_isp_t32_hw.c */
/* ../t33/tx_isp_t33_awbc.c: per-frame AWB */
int t33_awbc_attach(const u8 *sec, u32 len, u32 width, u32 height, const u8 *bufs);
void t33_awbc_detach(void);
void t33_awbc_kick(void);
void t33_awbc_show(struct seq_file *m);
void t33_awbc_ae_event(u32 type, u32 v0, u32 v1);	/* AE events 4 / 6 (tisp_long_tgain_update / ev_update) */
/* ../t33/tx_isp_t33_aec.c: per-frame AE; tx_isp_t32_core.c: its sensor entries */
int t33_aec_attach(const u8 *sec, u32 len, u32 width, u32 height, const u8 *zbufs, const u8 *hbufs);
void t33_aec_detach(void);
void t33_aec_irq4(u32 idx);
void t33_aec_irq5(u32 idx);
void t33_aec_show(struct seq_file *m);
bool t33_evq_queue(struct work_struct *w);	/* the vendor's tisp event thread: one ordered queue */
void t33_evq_get(void);
void t33_tgain_refresh(u32 gain);	/* tx_isp_t32_hw.c: event 4 block refreshes */
void t33_ev_refresh(u32 lo, u32 hi);
void t33_ct_refresh(u32 ct);
void t33_evq_free(void);
int t32_core_sensor_ae_src(const u8 **video_in, const u8 **attr);
u32 t32_core_sensor_alloc_again(u32 log2_q16, u32 *reg);
int t32_core_sensor_set_fps(u32 fps);
#endif

/* tx_isp_t32_sinfo.c (shared registry, driver/common/tx_isp_sinfo.c) */
int tx_isp_sinfo_init(void);
void tx_isp_sinfo_exit(void);
int tx_isp_sinfo_sensor_bind(void *subdev, struct module *owner);
void tx_isp_sinfo_sensor_unbind(void *subdev, struct module *owner);

/* raw access to objects owned by the sensor modules */
#define T32_AT(base, off, type)		(*(type *)((char *)(base) + (off)))

#endif /* TX_ISP_T32_H */
