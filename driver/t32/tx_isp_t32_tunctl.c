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
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/jiffies.h>

#ifdef TX_ISP_T33
#include "tx_isp_t33_attrAWB.h"
#endif
#include "tx_isp_t32.h"
#include "tx_isp_t32_bayer.h"
#include "tx_isp_t32_txsctrl.h"
#include "tx_isp_t32_txgctrl.h"
#include "tx_isp_t32_tunpre.h"
#ifndef TX_ISP_T33
#include "tx_isp_t32_api_ctl.h"
#include "tx_isp_t32_msca_api.h"
#include "tx_isp_t32_msca_cfg.h"
#endif

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

#ifdef TX_ISP_T33
/* T33 (group 14e-2): tisp_s_antiflick / tisp_g_antiflick on the live AE (../t33/tx_isp_t33_aec.c); always 0 */
int t33_aec_api_i9j(u32 ch, u32 which, void *buf);
int t32_tx_os_tisp_s_antiflick(u32 ch, void *buf) { t33_aec_api_i9j(ch, 0, buf); return 0; }
int t32_tx_os_tisp_g_antiflick(u32 ch, void *buf) { t33_aec_api_i9j(ch, 1, buf); return 0; }
#else
T32_TC_PAIR(antiflick, antiflick)
#endif
T32_TC_PAIR(coefft_wb, coefft_wb)
#ifndef TX_ISP_T33
T32_TC_PAIR(mscaler_mask_block_attr, mask)
#endif
T32_TC_PAIR(module_ratio, ratio)
#ifndef TX_ISP_T33
T32_TC_PAIR(osd_attr, osd)
#endif
#ifndef TX_ISP_T33
T32_TC_PAIR(osd_block_attr, osd_block)
#endif
#ifndef TX_ISP_T33
T32_TC_PAIR(draw_block_attr, draw)
#endif
T32_TC_PAIR(csccr_mode, csccr)

/*
 * tisp_s_Gamma -> tisp_gamma_api_set (tx_isp_t32_gamma.h t32_gamma_api_set): the API block
 * { u32 mode, u8 curve[258] } takes the mode; 1..3 the preset curves, 4 the caller's curve,
 * > 4 returns -1 after storing the mode.  T32 (openimp #717): while a stream's gamma state
 * exists (stream-on to the next stream-on) the call runs tisp_gamma_api_set on it and the LUT
 * is written at once; this store answers before the first stream-on only.
 */
int t32_tx_os_tisp_s_gamma(u32 ch, void *buf)
{
	static const u32 *const preset[3] = { t32_tun_gamma_bf754, t32_tun_gamma_bf650, t32_tun_gamma_bf54c };
	u8 *b = t32_tc[ch].gamma;
	u32 v = *(u32 *)buf;

#ifndef TX_ISP_T33
	/* T32 (openimp #717): tisp_gamma_api_set on the gamma state of the stream (tx_isp_t32_tisp.c), LUT at once */
	{
		static const u8 *const pre8[3] = { (const u8 *)t32_tun_gamma_bf754, (const u8 *)t32_tun_gamma_bf650,
						   (const u8 *)t32_tun_gamma_bf54c };
		int r = t32_pf_gamma_api_set(ch, buf, pre8);

		if (r != -ENODEV)
			return r;
	}
#endif

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
#ifndef TX_ISP_T33
	if (!t32_pf_gamma_api_get(ch, buf))	/* T32: tisp_gamma_api_get on the live state (#717) */
		return 0;
#endif
	return t32_tc_put(buf, t32_tc[ch].gamma, 264);
}

#ifdef TX_ISP_T33
/*
 * T33 (openimp #608, group 13d-2): tx_isp_gamma_s_attr / _g_attr are the proven attr2 ports (tx_isp_t33_attr2.h)
 * around tisp_s_Gamma / tisp_g_Gamma on the gamma block gamma_para (tx_isp_t32_hw.c t33_tisp_s_gamma: the curve
 * reaches list 11 at the next irq 46, as the vendor).  Before the ISP init (no block yet) the T32 store above.
 */
#include "tx_isp_t33_attr2.h"

int t33_tisp_s_gamma(u32 ch, void *buf);
int t33_tisp_g_gamma(u32 ch, void *buf);

static int t33_tc_s_gamma(u32 ch, void *buf)
{
	if (!t33_tisp_s_gamma(ch, buf))
		return 0;
	t32_tx_os_tisp_s_gamma(ch, buf);
	return 0;	/* tisp_s_Gamma ignores tisp_gamma_api_set's result */
}

static int t33_tc_g_gamma(u32 ch, void *buf)
{
	if (!t33_tisp_g_gamma(ch, buf))
		return 0;
	return t32_tx_os_tisp_g_gamma(ch, buf);
}

static int t33_tc_copy_from(void *to, void *from, unsigned long n)
{
	return (int)t32_tx_os_copy_from_user(to, (u32)(uintptr_t)from, (u32)n);
}

static int t33_tc_copy_to(void *to, void *from, unsigned long n)
{
	return (int)t32_tx_os_copy_to_user((u32)(uintptr_t)to, from, (u32)n);
}

static int t33_tc_attr_pr(int level, const char *msg, const char *fn, int line)
{
	t32_tx_os_isp_printf((u32)level, "[ %s:%d ] %s!!!\n", fn, line, msg);
	return 0;
}

int t33_tx_gamma_s_attr(u32 ch, u32 user)
{
	return t33_gamma_s_attr(ch, user, t33_tc_s_gamma, t33_tc_copy_from, t33_tc_attr_pr);
}

int t33_tx_gamma_g_attr(u32 ch, u32 arg)
{
	return t33_gamma_g_attr(ch, (const u8 *)T32_TX_P(arg), t33_tc_g_gamma, t33_tc_copy_to, t33_tc_attr_pr);
}
#endif

/*
 * tisp_g_autozoom_control -> tisp_msca_api_get_autozoom: the five crop words of the three
 * MSCA output sets (start x, y, ..., width, height at +6 / +8 of each set), here the
 * sensor input size as the MSCA state holds it before an autozoom frame applies the request
 * (QEMU vendor reference: 0, 0, 0, 3840, 2160 for each set).
 */
static u32 t32_tc_sw, t32_tc_sh;
#ifdef TX_ISP_T33
int t33_msca_autozoom_set(u32 ch, void *buf);	/* ../t33/tx_isp_t33_osd.c: tisp_msca_t33_api_{set,get}_autozoom (g17-3) */
int t33_msca_autozoom_get(u32 ch, void *buf);
#endif

int t32_tx_os_tisp_g_autozoom(u32 ch, void *buf)
{
	u32 *w = buf, k;

#ifdef TX_ISP_T33
	if (!t33_msca_autozoom_get(ch, buf))
		return 0;
#endif

	memset(buf, 0, 60);
	for (k = 0; k < 3; k++) {
		w[9 + k] = t32_tc_sw;
		w[12 + k] = t32_tc_sh;
	}
	return 0;
}
int t32_tx_os_tisp_s_autozoom_control(u32 ch, void *buf)
{
#ifdef TX_ISP_T33
	if (!t33_msca_autozoom_set(ch, buf))
		return 0;
#endif
	return t32_tc_put(t32_tc[ch].autozoom, buf, 60);
}
#ifdef TX_ISP_T33
/* T33 (group 14e-1): tisp_set_csc_attr / tisp_get_csc_attr on csc_para (tx_isp_t32_hw.c) */
int t33_tisp_set_csc_attr(u32 ch, void *buf);
int t33_tisp_get_csc_attr(u32 ch, void *buf);
int t32_tx_os_tisp_set_csc_attr(u32 ch, void *buf) { t33_tisp_set_csc_attr(ch, buf); return 0; }
int t32_tx_os_tisp_get_csc_attr(u32 ch, void *buf) { t33_tisp_get_csc_attr(ch, buf); return 0; }
#else
int t32_tx_os_tisp_set_csc_attr(u32 ch, void *buf) { return t32_tc_put(t32_tc[ch].csc, buf, 92); }
int t32_tx_os_tisp_get_csc_attr(u32 ch, void *buf) { return t32_tc_put(buf, t32_tc[ch].csc, 92); }
#endif
#ifdef TX_ISP_T33
/* T33 (group 14d-1): tisp_s_ccm_attr -> tisp_ccm_api_set on ccm_para (tx_isp_t32_hw.c) */
int t33_tisp_s_ccm_attr(u32 ch, const void *buf);
int t33_tisp_g_ccm_attr(u32 ch, void *buf);
int t32_tx_os_tisp_s_ccm_attr(u32 ch, void *buf) { return t33_tisp_s_ccm_attr(ch, buf); }
#else
int t32_tx_os_tisp_s_ccm_attr(u32 ch, void *buf) { return t32_tc_put(t32_tc[ch].ccm, buf, 40); }
#endif
int t32_tx_os_tisp_awb_face_set(u32 ch, void *buf) { return t32_tc_put(t32_tc[ch].face_awb, buf, 24); }
int t32_tx_os_tisp_awb_face_get(u32 ch, void *buf) { return t32_tc_put(buf, t32_tc[ch].face_awb, 24); }
/*
 * tisp_ae_face_set -> tisp_ae_sepc_area: { enable, x0, y0, x1, y1, target }; the target is
 * clamped to 10..200, tisp_ae_api_get_faceae returns the six words.  T32: on the live AE
 * (t32_aec_face_set / _get, tx_isp_t32_ae.c: face zone bounds, tisp_ae_roi_point, openimp
 * #686); before the AE is attached (or ch != 0) only this copy is kept.
 */
#ifdef TX_ISP_T33
/* T33 (group 14e-2): tisp_ae_face_set -> tisp_ae_sepc_area, tisp_ae_face_get -> tisp_ae_api_get_faceae (aec.c) */
int t32_tx_os_tisp_ae_face_set(u32 ch, void *buf) { t33_aec_api_i9j(ch, 2, buf); return 0; }
int t32_tx_os_tisp_ae_face_get(u32 ch, void *buf) { t33_aec_api_i9j(ch, 3, buf); return 0; }
#else
int t32_tx_os_tisp_ae_face_set(u32 ch, void *buf)
{
	u32 *w = buf;

	if (!t32_aec_face_set(ch, buf))		/* clamps w[5] like the vendor */
		return t32_tc_put(t32_tc[ch].face_ae, buf, 24);
	if (w[5] > 200)
		w[5] = 200;
	else if (w[5] < 10)
		w[5] = 10;
	return t32_tc_put(t32_tc[ch].face_ae, buf, 24);
}
int t32_tx_os_tisp_ae_face_get(u32 ch, void *buf)
{
	if (!t32_aec_face_get(ch, buf))
		return 0;
	return t32_tc_put(buf, t32_tc[ch].face_ae, 24);
}
#endif
int t32_tx_os_tisp_s_tmo_faceae(u32 ch, void *buf) { return t32_tc_put(t32_tc[ch].tmo_faceae, buf, 56); }
int t32_tx_os_tisp_set_scaler_level_control(u32 ch, void *buf) { return t32_tc_put(t32_tc[ch].scaler, buf, 12); }

#ifndef TX_ISP_T33
/*
 * T32 (openimp #717): tisp_set_wdr_output_mode (vendor 0xc232c) maps modes 0..3 to themselves and any other to 3
 * with "The change mode is not supported."; tisp_wdr_api_set_output_mode keeps it as a u16 (wdr block +0x116) and
 * runs tisp_wdr_params_hard_refresh / tisp_wdr_direct_reg_refresh.  The open T32 driver runs no WDR block (linear
 * sensors only): the mode is kept as the vendor stores it, the WDR refresh has nothing to run on.
 */
int t32_tx_os_tisp_set_wdr_output_mode(u32 ch, void *buf)
{
	u32 m = *(u32 *)buf;

	if (m > 3) {
		t32_tx_os_isp_printf(1, "The change mode is not supported.\n");
		m = 3;
	}
	t32_tc[ch].wdr_mode = (u16)m;
	return 0;
}
#else
int t32_tx_os_tisp_set_wdr_output_mode(u32 ch, void *buf)
{
	t32_tc[ch].wdr_mode = *(u32 *)buf;
	return 0;
}
#endif

int t32_tx_os_tisp_get_wdr_output_mode(u32 ch, void *buf)
{
	*(u32 *)buf = t32_tc[ch].wdr_mode;
	return 0;
}

#ifdef TX_ISP_T33
void t33_module_control_set(u32 ch, u32 v);	/* tx_isp_t32_hw.c: the TOP bypass word (block top) */
u32 t33_module_control_get(u32 ch);
#else
/*
 * T32 (openimp #717): tisp_s_module_control (vendor 0xc09b4) = t32_s_module_control (tx_isp_t32_api_ctl.h, binary
 * oracle t32-modctl) in direct mode: tisp_mdns_start (0xd018 = 1, 0xd014 = 1) before and tisp_mdns_reg_trig
 * (0xd014 = 1) after the write when bit 11 changes, tisp_lce_top_change_state on the stream's LCE state when bit 4
 * or 10 changes (oracle t32-lcetcs), the low 22 bits of 0x40 replaced.
 */
static u32 t32_tc_mc_rd(u32 ch, u32 a) { (void)ch; return t32_hw_rd(a); }
static int t32_tc_mc_wr(u32 ch, u32 a, u32 v) { (void)ch; t32_hw_wr(a, v); return 0; }
static void t32_tc_mc_warn(void) { }
static int t32_tc_mdns_start(u32 ch) { (void)ch; t32_hw_wr(0xd018, 1); t32_hw_wr(0xd014, 1); return 0; }
static int t32_tc_mdns_trig(u32 ch) { (void)ch; t32_hw_wr(0xd014, 1); return 0; }
static int t32_tc_lce_tcs(u32 ch) { t32_pf_lce_top_change_state(ch); return 0; }
static const struct t32_api_ctl_ops t32_tc_mc_ops = {
	t32_tc_mc_rd, t32_tc_mc_wr, t32_tc_mc_warn, t32_tc_mdns_start, t32_tc_lce_tcs, t32_tc_mdns_trig,
};
#endif
/* tisp_s_module_control: the low 22 bits of the top enable word are the user's */
int t32_tx_os_tisp_s_module_control(u32 ch, u32 v)
{
#ifdef TX_ISP_T33
	t33_module_control_set(ch, v);
	return 0;
#endif
	t32_tc[ch].module_ctl = (t32_tc[ch].module_ctl & ~0x3fffffU) | (v & 0x3fffffU);
#ifndef TX_ISP_T33
	if (!ch)
		t32_s_module_control(&t32_tc_mc_ops, 0, ch, v);
#endif
	return 0;
}

int t32_tx_os_tisp_g_module_control(u32 ch, void *buf)
{
#ifdef TX_ISP_T33
	*(u32 *)buf = t33_module_control_get(ch);
	return 0;
#endif
	/* T32 (openimp #717): tisp_g_module_control = the 22 enable bits of the top register 0x40 (direct mode) */
	*(u32 *)buf = t32_hw_rd(0x40) & 0x3fffffU;
	return 0;
}

/* mirror / flip flags of output k = byte 0 bits 0..1, state bits 2..5 */
#ifdef TX_ISP_T33
int t33_msca_hv_flip_set(u32 ch, void *flags);	/* ../t33/tx_isp_t33_osd.c: tisp_msca_t33_api_{set,get}_mirr_flip */
int t33_msca_hv_flip_get(u32 ch, void *flags);
#endif
#ifndef TX_ISP_T33
/*
 * T32 (openimp #717): tisp_msca_set_mirr_flip / _get_mirr_flip (vendor 0x96480 / 0x96574, t32_msca_*_mirr_flip of
 * tx_isp_t32_msca_api.h) on the mirror words of the three output sets of channel 0 (st_msca_chx_cfg[0] + 48 +
 * 872 set; the open driver keeps no other part of that block), and at the next MSCA frame done (interrupt bit 33)
 * the mirror part of tisp_msca_cfg_shd: the bits into 0xf0050 (t32_msca_mirr_word), flag mirrflip cleared.
 */
static u8 t32_tc_msca_sb[3 * T32_MSCA_SET_SZ];
static u8 t32_tc_mirrflip;
static DEFINE_SPINLOCK(t32_tc_mf_lock);

void t32_tunctl_msca_frame_done(void)
{
	unsigned long f;

	spin_lock_irqsave(&t32_tc_mf_lock, f);
	if (t32_tc_mirrflip) {
		t32_hw_wr(0xf0050, t32_msca_mirr_word(t32_tc_msca_sb, t32_hw_rd(0xf0050)));
		t32_tc_mirrflip = 0;
	}
	spin_unlock_irqrestore(&t32_tc_mf_lock, f);
}
#endif

int t32_tx_os_tisp_g_hv_flip(u32 ch, void *flags)
{
	u8 *f = flags;

#ifdef TX_ISP_T33
	if (!t33_msca_hv_flip_get(ch, flags))
		return 0;
#else
	if (!ch) {
		unsigned long fl;

		spin_lock_irqsave(&t32_tc_mf_lock, fl);
		t32_msca_get_mirr_flip(t32_tc_msca_sb, f);
		spin_unlock_irqrestore(&t32_tc_mf_lock, fl);
		return 0;
	}
#endif

	*f = (u8)((*f & 3) | (t32_tc[ch].flip[*f & 3] & 0x3c));
	return 0;
}

int t32_tx_os_tisp_s_hv_flip(u32 ch, void *flags)
{
	u8 *f = flags;

#ifdef TX_ISP_T33
	if (!t33_msca_hv_flip_set(ch, flags))
		return 0;
#else
	if (!ch) {
		unsigned long fl;

		spin_lock_irqsave(&t32_tc_mf_lock, fl);
		t32_msca_set_mirr_flip(t32_tc_msca_sb, f, &t32_tc_mirrflip);
		spin_unlock_irqrestore(&t32_tc_mf_lock, fl);
		return 0;
	}
#endif

	t32_tc[ch].flip[*f & 3] = *f & 0x3c;
	return 0;
}

int t32_tx_os_tisp_lsc_hvflip(u32 ch, u32 a, u32 b, u32 h, u32 v) { return 0; }

/* BCSH bytes: the dispatcher keeps them in the ISP device (+16 + 32 ch) */
#ifdef TX_ISP_T33
/* T33 (group 14c): tisp_set_* -> tisp_bcsh_api_set_* on bcsh_para (tx_isp_t32_hw.c) */
void t33_tisp_set_bcsh(u32 ch, u32 which, u32 v);
void t33_tisp_set_sharpness(u32 ch, u32 v);	/* tisp_set_sharpness (tx_isp_t32_hw.c, openimp #707) */
int t32_tx_os_tisp_set_hue(u32 ch, u32 v) { t33_tisp_set_bcsh(ch, 3, v); return 0; }
int t32_tx_os_tisp_set_brightness(u32 ch, u32 v) { t33_tisp_set_bcsh(ch, 0, v); return 0; }
int t32_tx_os_tisp_set_sharpness(u32 ch, u32 v) { t33_tisp_set_sharpness(ch, v); return 0; }
int t32_tx_os_tisp_set_saturation(u32 ch, u32 v) { t33_tisp_set_bcsh(ch, 2, v); return 0; }
int t32_tx_os_tisp_set_contrast(u32 ch, u32 v) { t33_tisp_set_bcsh(ch, 1, v); return 0; }
#else
/* T32: tisp_set_* -> tisp_bcsh_api_set_* on the BCSH state of tx_isp_t32_ctup.c (openimp #716) */
int t32_tx_os_tisp_set_hue(u32 ch, u32 v) { t32_ctup_bcsh_set(ch, 3, (u8)v); return 0; }
int t32_tx_os_tisp_set_brightness(u32 ch, u32 v) { t32_ctup_bcsh_set(ch, 0, (u8)v); return 0; }
int t32_tx_os_tisp_set_sharpness(u32 ch, u32 v) { t32_pf_set_sharpness(ch, v); return 0; }	/* #711 */
int t32_tx_os_tisp_set_saturation(u32 ch, u32 v) { t32_ctup_bcsh_set(ch, 2, (u8)v); return 0; }
int t32_tx_os_tisp_set_contrast(u32 ch, u32 v) { t32_ctup_bcsh_set(ch, 1, (u8)v); return 0; }
#endif

#ifdef TX_ISP_T33
int t32_tx_os_tisp_set_fps(u32 ch, u32 fps) { return 0; }
#else
/* T32 (openimp #716): tisp_set_fps on the live AE (tx_isp_t32_ae.c); 0x70 itself is answered by tx_isp_t32_core.c */
int t32_tx_os_tisp_set_fps(u32 ch, u32 fps) { return t32_aec_tisp_set_fps(ch, fps); }
#endif

/* 0x70 is answered by tx_isp_t32_core.c; tisp_get_fps: the sensor's fps word */
int t32_tx_os_tisp_get_fps(u32 ch, u32 arg)
{
	t32_tx_st(arg, 0, t32_core_sensor_fps());
	return 0;
}

#ifndef TX_ISP_T33
/*
 * T32 (openimp #717): lib_tisp_api_set_line_space / _get_line_space (vendor 0xb3304 / 0xb32cc) are the
 * proven ports of tx_isp_t32_bayer.h (t32_regapi_oracle): bits 31..16 of the top register 0x90 through
 * system_reg_read / _write, bits 15..0 kept.
 */
static u32 t32_tc_sys_rd(void *ctx, u32 reg) { (void)ctx; return t32_hw_rd(reg); }
static u32 t32_tc_sys_wr(void *ctx, u32 reg, u32 val) { (void)ctx; t32_hw_wr(reg, val); return 0; }
static const struct t32_regio t32_tc_io = { .sys_read = t32_tc_sys_rd, .sys_write = t32_tc_sys_wr };

void t32_tx_os_lib_tisp_api_set_line_space(u32 v)
{
	t32_tc_line_space = (u16)v;
	t32_api_set_line_space(&t32_tc_io, v);
}

int t32_tx_os_lib_tisp_api_get_line_space(void *buf)
{
	unsigned short s;

	t32_api_get_line_space(&t32_tc_io, &s);
	*(u16 *)buf = s;
	return 0;
}
#else
void t32_tx_os_lib_tisp_api_set_line_space(u32 v)
{
	t32_tc_line_space = (u16)v;
}

int t32_tx_os_lib_tisp_api_get_line_space(void *buf)
{
	*(u16 *)buf = t32_tc_line_space;
	return 0;
}
#endif

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

#ifdef TX_ISP_T33
/* T33: the oracle-proven vendor glue (tx_isp_t33_attrAWB.h, block attrAWB): always 0, a failed copy only prints */
/* tisp_g_ccm_attr (group 14d-1): ccm_para + 612 behind the top bypass gate; its return is ignored by the g_ctrl glue */
static int t33_tc_g_ccm(u32 ch, u8 *buf) { return t33_tisp_g_ccm_attr(ch, buf); }
static int t33_tc_out(u32 user, const u8 *src, u32 n) { return (int)t32_tx_os_copy_to_user(user, src, n); }
static void t33_tc_print(u32 level) { t32_tx_os_isp_printf(level, "[ %s:%d ] copy error!!!\n", "tiziano_isp_ccm_attr_g_ctrl", 0); }
static const struct t33_awbattr_ops t33_tc_ccm_ops = { NULL, NULL, NULL, t33_tc_g_ccm, t33_tc_out, NULL, t33_tc_print };

int t32_tx_os_ccm_attr_g_ctrl(u32 ch, u32 arg)
{
	return t33_ccm_attr_g_ctrl(&t33_tc_ccm_ops, ch, t32_tx_ld(arg, 0));
}
#else
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
#endif

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
#ifdef TX_ISP_T33
	t32_tc_sd[T32_SC_CORE_PRIV / 4] = core;	/* the 3.10 subdev: 0x73 reads the private block at +276 (#669) */
#endif
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
#ifdef TX_ISP_T33
	/*
	 * T33 (t32_isposd_ctrl is off): OSD attribute / OSD block / mask run on
	 * the ported MSCA OSD / mask / draw functions (tx_isp_t33_osd.c, MSCA list 23;
	 * tx_isp_core_ops_s_ctrl / _g_ctrl @ 0x18170 / 0x19dfc of the vendor module).
	 */
	case 0x74: case 0xa0: case 0xa1: case 0xa2:
		return true;
#endif
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
		/*
		 * openimp #669 (T33) / #689 (T32): a sensor flip mode 0x73 queued as the vendor's pending
		 * frame-done entry 5 (core +1340 / +1344 + 96 ch on T33, +1344 / +1348 on T32) goes to the
		 * live core, which runs tx_isp_core_shvflip at the next frame end (ispcore_irq_fd_work0).
		 */
		if (w[2] == T32_TC_ID_FIRST + 0x73 && t32_tc_core[(T32_SC_FLIP_PEND + 96 * ch) / 4]) {
			t32_tc_core[(T32_SC_FLIP_PEND + 96 * ch) / 4] = 0;
			t33_core_flip_request(ch, t32_tc_core[(T32_SC_FLIP_PEND + 4 + 96 * ch) / 4]);
		}
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
