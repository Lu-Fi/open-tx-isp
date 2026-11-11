/* SPDX-License-Identifier: GPL-2.0 */
/* T32 driver-internal declarations. */
#ifndef TX_ISP_T32_H
#define TX_ISP_T32_H

#include <linux/types.h>

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
int t32_core_iq_bin(const u8 **bin, u32 *bytes);
int t32_core_sensor_mbus(u32 *code);
int t32_core_sensor_expo(u32 packed);
u32 t32_core_sensor_vts(void);
int t32_core_sensor_ae_src(const u8 **video_in, const u8 **attr);
u32 t32_core_sensor_alloc_again(u32 log2_q16, u32 *reg);
int t32_core_sensor_set_fps(u32 fps);

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
extern u32 t32_rcfg_runs, t32_rcfg_timeouts, t32_rcfg_last_len;
void t32_hw_stream_on(void);
void t32_hw_stream_off(void);
void t32_hw_msca_push(int ch, u32 y, u32 uv);
void t32_hw_msca_start(int ch, u32 w, u32 h, u32 stride, u32 in_w, u32 in_h);
void t32_hw_msca_stop(int ch);
int t32_hw_msca_pop_done(int ch, u32 *y, u32 *uv);

u32 t32_hw_rd(u32 off);
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
const u8 *t32_hw_ae_zones(void);

/* tx_isp_t32_awbc.c: the per-frame vendor AWB (tisp_awb_init state, tisp_awb_process), behind T32_PP_AWB */
struct t32_awbc_drv_stats {
	u32 frames, writes, refused;
	u32 ct, gain_r, gain_b;		/* the last frame */
};
extern struct t32_awbc_drv_stats t32_awbc_stats;
int t32_awbc_attach(const u8 *sec, u32 width, u32 height, const u8 *bufs);
void t32_awbc_detach(void);
void t32_awbc_kick(void);
bool t32_awbc_ct(u32 *ct);

/* tx_isp_t32_tisp.c: per-frame tisp paths (ported blocks), switch t32_ported_paths */
enum t32_pp { T32_PP_DMSC, T32_PP_YSP, T32_PP_CDNS, T32_PP_YDNS, T32_PP_SDNS, T32_PP_MDNS, T32_PP_DPC, T32_PP_GIB,
	     T32_PP_RDNS, T32_PP_AE, T32_PP_LSC, T32_PP_LCE, T32_PP_TMO, T32_PP_AWB, T32_PP_COUNT };
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

/* tx_isp_t32_fs.c */
struct seq_file;
int t32_fs_init(void);
void t32_fs_exit(void);
void t32_fs_stop_all(void);
void t32_fs_done_irq(u32 done);
void t32_fs_show(struct seq_file *m);

/* tx_isp_t32_sinfo.c (shared registry, driver/common/tx_isp_sinfo.c) */
int tx_isp_sinfo_init(void);
void tx_isp_sinfo_exit(void);
int tx_isp_sinfo_sensor_bind(void *subdev, struct module *owner);
void tx_isp_sinfo_sensor_unbind(void *subdev, struct module *owner);

/* raw access to objects owned by the sensor modules */
#define T32_AT(base, off, type)		(*(type *)((char *)(base) + (off)))

#endif /* TX_ISP_T32_H */
