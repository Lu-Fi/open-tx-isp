// SPDX-License-Identifier: GPL-2.0
/*
 * T32 ISP tuning controls (/dev/isp-m0 ioctl 0xc0105467, /dev/tx-isp AE start EV).
 *
 * The vendor flow: isp_core_tunning_default_ioctl copies { vi, dir, id, value }
 * in, dir 0 calls tx_isp_core_ops_s_ctrl(vi, isp, &id), dir 1
 * tx_isp_core_ops_g_ctrl and copies the block back.  Both dispatchers and their
 * attribute wrappers are the oracle-proven ports of tx_isp_t32_txsctrl.h,
 * tx_isp_t32_txgctrl.h and tx_isp_t32_txattr.h; this file is their kernel glue:
 *
 *  - the t32_tx_os_* hooks (user copies, prints, vmalloc) as kernel calls;
 *  - the vendor ISP device / core subdevice words the dispatchers read (BCSH
 *    bytes, mirror / flip, WDR flag, autozoom cache, sensor geometry) in
 *    vendor-shaped blocks, refreshed from the driver state before each call;
 *  - the tisp_s_* / tisp_g_* hooks of the other blocks: per channel the
 *    attribute the vendor block API keeps (what tisp_g_* returns after a
 *    tisp_s_*), with the vendor conversions (gamma presets, module control
 *    mask, face AE clamp, autozoom crop words).
 *
 * The AE / AWB / statistics ids (attributes, weights, exposure info / list,
 * target, scene, only-read, statistics, statis config, face AE luma / weight,
 * CT trend offset; AE start EV) run on the live AE / AWB blocks in
 * tx_isp_t32_tunblk.c.  Still kept here, not applied to the image blocks:
 * anti-flicker, CSC, CCM, BCSH, mirror / flip, module ratio, face AE / AWB
 * areas, TMO face AE, WDR mode, scaler level; AF statistics answer zeros (no
 * AF block in the driver).  The sensor frame rate (0x08000070) stays in
 * tx_isp_t32_core.c.
 * Not served here: 0x71 day/night, 0x78 max fps, 0xa5 switch bin (#289); the ISP OSD ids
 * 0x74 / 0xa0 / 0xa1 / 0xa2 go to tx_isp_t32_isposd.c (#336, the ported MSCA OSD path).
 * Beyond vendor (safety): a VI number >= 3 is refused with -EINVAL (the vendor
 * indexes its per-channel arrays with it unchecked).
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/jiffies.h>

#include "tx_isp_t32.h"
#include "tx_isp_t32_txsctrl.h"
#include "tx_isp_t32_txgctrl.h"
#include "tx_isp_t32_tunpre.h"

#define T32_TC_CH		3
#define T32_TC_ID_FIRST		0x08000000U
#define T32_AE_SCENE_BYTES_TC	80	/* AE scene attribute (start EV in words 6, 7) */

/* ---- t32_tx_os_* hooks: kernel calls ---- */

u32 t32_tx_link_mode;

u32 t32_tx_os_copy_from_user(void *dst, u32 user, u32 n)
{
	return copy_from_user(dst, (const void __user *)(uintptr_t)user, n);
}

u32 t32_tx_os_copy_to_user(u32 user, const void *src, u32 n)
{
	return copy_to_user((void __user *)(uintptr_t)user, src, n);
}

void t32_tx_os_printk(const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	printk("%pV", &vaf);
	va_end(args);
}

void t32_tx_os_isp_printf(u32 level, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	isp_printf(level, "%pV", &vaf);
	va_end(args);
}

void t32_tx_os_msleep(u32 ms)
{
	msleep(ms);
}

u32 t32_tx_os_vmalloc(u32 n)
{
	return T32_TX_A(vmalloc(n));
}

void t32_tx_os_vfree(u32 a)
{
	vfree(T32_TX_P(a));
}

void t32_tx_os_memset(u32 a, u32 c, u32 n)
{
	memset(T32_TX_P(a), c, n);
}

void t32_tx_os_memcpy(u32 d, u32 s, u32 n)
{
	memcpy(T32_TX_P(d), T32_TX_P(s), n);
}

u32 t32_tx_os_thread_limit(void)
{
	return (u32)current_thread_info()->addr_limit.seg;
}

/* __copy_user after the access check: the bytes left */
u32 t32_tx_os_copy_user(void *dst, const void *src, u32 n)
{
	if ((uintptr_t)dst < TASK_SIZE)
		return copy_to_user((void __user *)dst, src, n);
	return copy_from_user(dst, (const void __user *)src, n);
}

int t32_tx_os_wait_for_completion_timeout(u32 c, u32 t)
{
	return 0;	/* day / night and switch bin are not served here */
}

/* switch bin (0xa5) is not served here (#289): refused before the dispatcher */
void t32_tx_os_get_bin_path(u32 ch, u32 buf) { }
int t32_tx_os_tisp_switch_bin(u32 ch, void *blk) { return -1; }
void t32_tx_os_sprintf(char *dst, const char *src) { strcpy(dst, src); }

/* ---- the tisp attribute store (what the vendor block APIs keep per channel) ---- */

struct t32_tc_attr {
	u8 antiflick[8], coefft_wb[8], mask[24], csc[92], ccm[40], gamma[264];
	u8 ratio[128], af_weight[228];
	u8 autozoom[60], osd[32], osd_block[20], draw[36], scaler[12];
	u8 face_ae[24], face_awb[24], tmo_faceae[56], csccr[12];
	u32 module_ctl, wdr_mode, flip[3];
};

static struct t32_tc_attr t32_tc[T32_TC_CH];
static u16 t32_tc_line_space;
static DEFINE_MUTEX(t32_tc_lock);

/* frame done (0x91): per channel counter, flag and wait queue */
static u32 t32_tc_fd_cnt[T32_TC_CH][2], t32_tc_fd_flag[T32_TC_CH];
static DECLARE_WAIT_QUEUE_HEAD(t32_tc_fd_wq);

static int t32_tc_put(void *dst, const void *src, u32 n)
{
	memcpy(dst, src, n);
	return 0;
}

#define T32_TC_PAIR(name, field)							\
int t32_tx_os_tisp_s_##name(u32 ch, void *buf)						\
{ return t32_tc_put(t32_tc[ch].field, buf, sizeof(t32_tc[ch].field)); }		\
int t32_tx_os_tisp_g_##name(u32 ch, void *buf)						\
{ return t32_tc_put(buf, t32_tc[ch].field, sizeof(t32_tc[ch].field)); }

T32_TC_PAIR(antiflick, antiflick)
T32_TC_PAIR(coefft_wb, coefft_wb)
T32_TC_PAIR(mscaler_mask_block_attr, mask)
T32_TC_PAIR(module_ratio, ratio)
T32_TC_PAIR(osd_attr, osd)
T32_TC_PAIR(osd_block_attr, osd_block)
T32_TC_PAIR(draw_block_attr, draw)
T32_TC_PAIR(csccr_mode, csccr)

/*
 * tisp_s_Gamma -> tisp_gamma_api_set (tx_isp_t32_gamma.h t32_gamma_api_set): the API block
 * { u32 mode, u8 curve[258] } takes the mode; 1..3 the preset curves, 4 the caller's curve,
 * > 4 returns -1 after storing the mode.  Mode 0 (back to the tuned curve) keeps the last
 * curve here: the gamma block state is not kept after the init yet.
 */
int t32_tx_os_tisp_s_gamma(u32 ch, void *buf)
{
	static const u32 *const preset[3] = { t32_tun_gamma_bf754, t32_tun_gamma_bf650, t32_tun_gamma_bf54c };
	u8 *b = t32_tc[ch].gamma;
	u32 v = *(u32 *)buf;

	*(u32 *)b = v;
	if (v >= 1 && v <= 3)
		memcpy(b + 4, preset[v - 1], 258);
	else if (v == 4)
		memcpy(b + 4, (u8 *)buf + 4, 258);
	else if (v > 4)
		return -1;
	return 0;
}

int t32_tx_os_tisp_g_gamma(u32 ch, void *buf)
{
	return t32_tc_put(buf, t32_tc[ch].gamma, 264);
}

/*
 * tisp_g_autozoom_control -> tisp_msca_api_get_autozoom: the five crop words of the three
 * MSCA output sets (start x, y, ..., width, height at +6 / +8 of each set), here the
 * sensor input size as the MSCA state holds it before an autozoom frame applies the request
 * (QEMU vendor reference: 0, 0, 0, 3840, 2160 for each set).
 */
static u32 t32_tc_sw, t32_tc_sh;

int t32_tx_os_tisp_g_autozoom(u32 ch, void *buf)
{
	u32 *w = buf, k;

	memset(buf, 0, 60);
	for (k = 0; k < 3; k++) {
		w[9 + k] = t32_tc_sw;
		w[12 + k] = t32_tc_sh;
	}
	return 0;
}
int t32_tx_os_tisp_s_autozoom_control(u32 ch, void *buf) { return t32_tc_put(t32_tc[ch].autozoom, buf, 60); }
int t32_tx_os_tisp_set_csc_attr(u32 ch, void *buf) { return t32_tc_put(t32_tc[ch].csc, buf, 92); }
int t32_tx_os_tisp_get_csc_attr(u32 ch, void *buf) { return t32_tc_put(buf, t32_tc[ch].csc, 92); }
int t32_tx_os_tisp_s_ccm_attr(u32 ch, void *buf) { return t32_tc_put(t32_tc[ch].ccm, buf, 40); }
int t32_tx_os_tisp_awb_face_set(u32 ch, void *buf) { return t32_tc_put(t32_tc[ch].face_awb, buf, 24); }
int t32_tx_os_tisp_awb_face_get(u32 ch, void *buf) { return t32_tc_put(buf, t32_tc[ch].face_awb, 24); }
/*
 * tisp_ae_face_set -> tisp_ae_sepc_area: { enable, x0, y0, x1, y1, target }; the target is
 * clamped to 10..200, tisp_ae_api_get_faceae returns the six words.  Not here yet:
 * the face ROI zone bytes (e1518 .. e1524) and tisp_ae_roi_point (AE state).
 */
int t32_tx_os_tisp_ae_face_set(u32 ch, void *buf)
{
	u32 *w = buf;

	if (w[5] > 200)
		w[5] = 200;
	else if (w[5] < 10)
		w[5] = 10;
	return t32_tc_put(t32_tc[ch].face_ae, buf, 24);
}
int t32_tx_os_tisp_ae_face_get(u32 ch, void *buf) { return t32_tc_put(buf, t32_tc[ch].face_ae, 24); }
int t32_tx_os_tisp_s_tmo_faceae(u32 ch, void *buf) { return t32_tc_put(t32_tc[ch].tmo_faceae, buf, 56); }
int t32_tx_os_tisp_set_scaler_level_control(u32 ch, void *buf) { return t32_tc_put(t32_tc[ch].scaler, buf, 12); }

int t32_tx_os_tisp_set_wdr_output_mode(u32 ch, void *buf)
{
	t32_tc[ch].wdr_mode = *(u32 *)buf;
	return 0;
}

int t32_tx_os_tisp_get_wdr_output_mode(u32 ch, void *buf)
{
	*(u32 *)buf = t32_tc[ch].wdr_mode;
	return 0;
}

/* tisp_s_module_control: the low 22 bits of the top enable word are the user's */
int t32_tx_os_tisp_s_module_control(u32 ch, u32 v)
{
	t32_tc[ch].module_ctl = (t32_tc[ch].module_ctl & ~0x3fffffU) | (v & 0x3fffffU);
	return 0;
}

int t32_tx_os_tisp_g_module_control(u32 ch, void *buf)
{
	*(u32 *)buf = t32_tc[ch].module_ctl;
	return 0;
}

/* mirror / flip flags of output k = byte 0 bits 0..1, state bits 2..5 */
int t32_tx_os_tisp_g_hv_flip(u32 ch, void *flags)
{
	u8 *f = flags;

	*f = (u8)((*f & 3) | (t32_tc[ch].flip[*f & 3] & 0x3c));
	return 0;
}

int t32_tx_os_tisp_s_hv_flip(u32 ch, void *flags)
{
	u8 *f = flags;

	t32_tc[ch].flip[*f & 3] = *f & 0x3c;
	return 0;
}

int t32_tx_os_tisp_lsc_hvflip(u32 ch, u32 a, u32 b, u32 h, u32 v) { return 0; }

/* BCSH bytes: the dispatcher keeps them in the ISP device (+16 + 32 ch) */
int t32_tx_os_tisp_set_hue(u32 ch, u32 v) { return 0; }
int t32_tx_os_tisp_set_brightness(u32 ch, u32 v) { return 0; }
int t32_tx_os_tisp_set_sharpness(u32 ch, u32 v) { return 0; }
int t32_tx_os_tisp_set_saturation(u32 ch, u32 v) { return 0; }
int t32_tx_os_tisp_set_contrast(u32 ch, u32 v) { return 0; }

int t32_tx_os_tisp_set_fps(u32 ch, u32 fps) { return 0; }

/* 0x70 is answered by tx_isp_t32_core.c; tisp_get_fps: the sensor's fps word */
int t32_tx_os_tisp_get_fps(u32 ch, u32 arg)
{
	t32_tx_st(arg, 0, t32_core_sensor_fps());
	return 0;
}

void t32_tx_os_lib_tisp_api_set_line_space(u32 v)
{
	t32_tc_line_space = (u16)v;
}

int t32_tx_os_lib_tisp_api_get_line_space(void *buf)
{
	*(u16 *)buf = t32_tc_line_space;
	return 0;
}

/* statistics / read-only state: zeroed until the statistics are wired */
static int t32_tc_zero(void *buf, u32 n)
{
	memset(buf, 0, n);
	return 0;
}

/*
 * AF (tx_isp_t32_afgsm.c): tisp_s/g_af_weight_attr, tisp_g_af_statis_attr and tisp_g_af_metric_attr on the
 * live AF block; before its attach (or with T32_PP_AF off) the stored weight and zeros.
 */
int t32_tx_os_tisp_s_af_weight(u32 ch, void *buf)
{
	t32_tc_put(t32_tc[ch].af_weight, buf, T32_ATTR_AF_WEIGHT);
	t32_afgsm_af_set_weight(ch, buf);
	return 0;
}

int t32_tx_os_tisp_g_af_weight(u32 ch, void *buf)
{
	if (!t32_afgsm_af_get_weight(ch, buf))
		t32_tc_put(buf, t32_tc[ch].af_weight, T32_ATTR_AF_WEIGHT);
	return 0;
}

int t32_tx_os_tisp_g_af_statis_attr(u32 ch, void *buf)
{
	if (!t32_afgsm_af_get_statis(ch, buf))
		t32_tc_zero(buf, 5400);
	return 0;
}

/*
 * tisp_af_api_get_metric writes bytes 0 .. 8; bytes 9 .. 11 of the 12 copied out are the caller's stack in the
 * vendor (0 in the vendor QEMU run): zeroed here, so no kernel stack reaches the user.
 */
int t32_tx_os_tisp_g_af_metric_attr(u32 ch, void *buf)
{
	t32_tc_zero(buf, 12);
	t32_afgsm_af_get_metric(ch, buf);
	return 0;
}

/* ---- tiziano_isp_ccm_attr_g_ctrl (the AE / AWB .isra wrappers are in tx_isp_t32_tunblk.c) ---- */

static int t32_tc_out(u32 arg, const u8 *src, u32 n, const char *fn)
{
	if (!t32_tx_os_copy_to_user(t32_tx_ld(arg, 0), src, n))
		return 0;
	t32_tx_os_isp_printf(2, "[ %s:%d ] copy error!!!\n", fn, 0);
	return -EFAULT;
}

int t32_tx_os_ccm_attr_g_ctrl(u32 ch, u32 arg)
{
	return t32_tc_out(arg, t32_tc[ch].ccm, 40, "tiziano_isp_ccm_attr_g_ctrl");
}

/* isp_frame_done_wait: wait (ms, 0 = none) for the next frame end, count = the channel's frames */
int t32_tx_os_isp_frame_done_wait(u32 ch, u32 wait, void *count)
{
	u32 *c = count;

	t32_tc_fd_flag[ch] = 0;
	if (wait)
		wait_event_interruptible_timeout(t32_tc_fd_wq, t32_tc_fd_flag[ch], msecs_to_jiffies(wait));
	c[0] = t32_tc_fd_cnt[ch][0];
	c[1] = t32_tc_fd_cnt[ch][1];
	return 0;
}

/* isp_frame_done_wakeup: called at the frame end of channel ch */
void t32_tunctl_frame_done(u32 ch)
{
	if (ch >= T32_TC_CH)
		return;
	if (!++t32_tc_fd_cnt[ch][0])
		t32_tc_fd_cnt[ch][1]++;
	t32_tc_fd_flag[ch] = 1;
	wake_up_interruptible(&t32_tc_fd_wq);
}

/* ---- the vendor ISP device / core subdevice words the dispatchers read ---- */

#define T32_TC_CORE_BYTES	4400
static u32 t32_tc_isp[64];			/* +0 sd, +16 + 32 ch BCSH .. day/night */
static u32 t32_tc_sd[128];			/* +280 core */
static u32 t32_tc_core[T32_TC_CORE_BYTES / 4];	/* +376 + 104 ch input, +1180 + 4 ch WDR, +4044 + 60 ch autozoom */
static u8 t32_tc_sattr[0x190];			/* sensor attribute copy for 0x33 */

/*
 * The vendor core keeps per input (+376 + 104 ch) the sensor attribute pointer, the size and
 * the frame rate as synced at sensor setup (ispcore_sync_sensor_attr): a later frame rate
 * change does not update them (0x33 answers the setup HTS / VTS / fps in the vendor run).
 * Here: a snapshot of the attribute block, size and fps at the first call with the sensor
 * up, taken again when the attribute block moves (new sensor).
 */
static const u8 *t32_tc_snap_src;

static void t32_tc_refresh(u32 ch)
{
	u32 core = T32_TX_A(t32_tc_core), in = core + 376 + 104 * ch, w = 0, h = 0;
	const u8 *video_in, *attr;

	t32_tc_isp[0] = T32_TX_A(t32_tc_sd);
	t32_tc_sd[280 / 4] = core;
	if (t32_core_sensor_ae_src(&video_in, &attr)) {
		t32_tc_snap_src = NULL;
		memset(t32_tc_sattr, 0, sizeof(t32_tc_sattr));
		t32_tc_sw = t32_tc_sh = 0;
	} else if (attr != t32_tc_snap_src) {
		t32_tc_snap_src = attr;
		memcpy(t32_tc_sattr, attr, sizeof(t32_tc_sattr));
		t32_core_sensor_size(&w, &h);
		t32_tc_sw = w;
		t32_tc_sh = h;
		t32_tx_st(core + 376, 8, w);
		t32_tx_st(core + 376, 12, h);
		t32_tx_st(core + 376, 16, t32_core_sensor_fps());
	}
	t32_tx_st(core + 376, 0, T32_TX_A(t32_tc_sattr));
	(void)in;
}

static bool t32_tc_served(u32 id)
{
	switch (id - T32_TC_ID_FIRST) {
	case 0x00: case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x17:
	case 0x21: case 0x22: case 0x23: case 0x25: case 0x26: case 0x27: case 0x28: case 0x29:
	case 0x2a: case 0x2b: case 0x2d: case 0x2e: case 0x31: case 0x32: case 0x33: case 0x34:
	case 0x35: case 0x54: case 0x62: case 0x72: case 0x73: case 0x77: case 0x80:
	case 0x81: case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97:
	case 0x9b: case 0xa4: case 0xa6: case 0xa7:
		return true;
	}
	return false;
}

/*
 * isp_core_tunning_default_ioctl for the served ids: w = { vi, dir, id, value }
 * as copied in.  Returns -ENOIOCTLCMD for ids not served here.
 */
long t32_tunctl_ctrl(u32 *w, unsigned long arg)
{
	u32 ch = w[0];
	int r;

	mutex_lock(&t32_tc_lock);
	t32_tc_refresh(ch);	/* also on the first (fps) request: the setup snapshot */
	mutex_unlock(&t32_tc_lock);
	if (!t32_tc_served(w[2]))
		return -ENOIOCTLCMD;
	if (ch >= T32_TC_CH)
		return -EINVAL;
	mutex_lock(&t32_tc_lock);
	if (!w[1]) {
		r = t32_tx_core_ops_s_ctrl(ch, T32_TX_A(t32_tc_isp), T32_TX_A(&w[2]));
	} else {
		r = t32_tx_core_ops_g_ctrl(ch, T32_TX_A(t32_tc_isp), T32_TX_A(&w[2]));
		if ((!r || r == -ENOIOCTLCMD) && copy_to_user((void __user *)arg, w, 16))
			r = -EFAULT;
	}
	mutex_unlock(&t32_tc_lock);
	return r;
}

/*
 * /dev/tx-isp 0xc0045436 / 0xc0045437 (tx_isp_set_ae_start_ev / tx_isp_get_ae_start_ev):
 * { u8 vi, ev[2] } in the AE scene attribute words 6, 7, read and written with
 * tisp_g_ae_scence_attr / tisp_s_ae_scence_attr (tx_isp_t32_tunblk.c, the live AE).
 */
long t32_tunctl_ae_start(unsigned int cmd, unsigned long arg)
{
	u32 u[3], sc[T32_AE_SCENE_BYTES_TC / 4];
	int r;

	if (copy_from_user(u, (const void __user *)arg, sizeof(u))) {
		isp_printf(2, "[ %s:%d ] copy error!!!\n", "tx_isp_set_ae_start_ev", 2080);
		return -EFAULT;
	}
	if ((u8)u[0] >= T32_TC_CH)
		return -EINVAL;
	memset(sc, 0, sizeof(sc));
	mutex_lock(&t32_tc_lock);
	r = t32_tx_os_tisp_g_ae_scence_attr((u8)u[0], sc);
	if (!r && cmd == 0xc0045436U) {
		sc[6] = u[1];
		sc[7] = u[2];
		r = t32_tx_os_tisp_s_ae_scence_attr((u8)u[0], sc);
	} else if (!r) {
		u[1] = sc[6];
		u[2] = sc[7];
	}
	mutex_unlock(&t32_tc_lock);
	if (r)
		return r;
	if (cmd == 0xc0045437U && copy_to_user((void __user *)arg, u, sizeof(u)))
		return -EFAULT;
	return 0;
}
