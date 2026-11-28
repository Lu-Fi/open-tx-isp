// SPDX-License-Identifier: GPL-2.0
/*
 * T33 (PRJ008) MSCA attribute state and its tuning ids (wiring group 11): the
 * vendor's stMscaAttr[0] (6036 B) / msca_info[0] (368 B) / MscaOsdInfo /
 * mscaCfgMode, set up by the ported tisp_msca_t33_init (tx_isp_t33_mscaInit.h),
 * and the dispatcher entries of tisp_s_osd_attr / tisp_s_osd_block_attr /
 * tisp_s_mscaler_mask_block_attr / tisp_s_draw_block_attr / tisp_s_hv_flip and
 * their g_ twins (0x109120 .. 0x1090b4) on the ported tisp_msca_t33_api_*
 * functions in that byte layout (tx_isp_t33_mscaosd.h, _msca_api4.h,
 * _msca_api5.h, _mscastrm.h, _mscaline.h).  The register words go to MSCA
 * list 23 (tisp_reg_write_t33, mscaCfgMode 1), which runs at the next frame
 * end (tispMscaRefresh = t33_msca_list_refresh).  T33 only (the T32 module
 * keeps the ISP OSD path of tx_isp_t32_isposd.c).
 *
 * Locking (openimp #578): every entry holds t33_osd_mutex - the tuning ids
 * (t32_tc_lock -> osd), stream-on (core.lock -> osd, tx_isp_t32_hw.c wraps
 * the list 23 build, t33_msca_attr_stream_on and t33_osd_list_rebuilt in
 * t33_osd_lock / t33_osd_unlock) and rmmod (t33_msca_attr_exit).  Nothing
 * here takes core.lock or t32_tc_lock.  The vendor module takes no lock on
 * these paths; the lock changes no register or list word.
 *
 * Differences to the vendor:
 * - the attribute block is set up at the first stream-on or tuning call of an ISP open
 *   and again when the input size changes; it survives stream-off (dropped at the last ISP close by
 *   t33_hw_lib_deinit, as the vendor re-inits it in lib_tisp_init: openimp #577), and
 *   the stored OSD / mask / draw windows go back into every rebuilt list 23
 *   (t33_osd_list_rebuilt).  The vendor's list memory survives instead.
 * - tisp_msca_t33_init's own callees: tisp_rcfg_module_register and
 *   system_irq_func_set(33) are done by tx_isp_t32_hw.c (list 23 is built and
 *   registered there, its frame-done handler is t33_msca_frame_done), here
 *   they only answer 0.
 * - tparamsP[0] + 0xa30c (the MSCA part of the IQ bin parameters) is IQ bin
 *   section 25 (the only word read is +0x2c).
 * - GET: the vendor dispatcher hands the getter an uninitialised stack block
 *   (no copy-in), so the window / mask number the getter reads is stack
 *   garbage.  Here the block is cleared and names the entry written last
 *   (#442).  The OSD block getter's word at +12 is the stored word (the vendor
 *   writes the address of its attribute bytes: no kernel address to user
 *   space).  Window numbers out of range are refused.
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/lockdep.h>
#include "tx_isp_t32.h"
#include "tx_isp_t32_iq.h"
#include "tx_isp_t33_mscaInit.h"
#include "tx_isp_t33_mscaosd.h"
#include "tx_isp_t33_msca_api4.h"
/* tisp_msca_t33_api_set_autozoom's callees: the proven ports of tx_isp_t33_mscacur.h (g17-3) */
#define tisp_msca_t33_scaling_algorithm	t33_osd_az_scaling
#define tisp_msca_t33_curve_calc	t33_osd_az_curve_calc
#define tisp_msca_t33_ch_curve_write	t33_osd_az_curve_write
#define tisp_msca_t33_ctrl_reg_write	t33_osd_az_ctrl
#include "tx_isp_t33_msca_api5.h"
#undef tisp_msca_t33_scaling_algorithm
#undef tisp_msca_t33_curve_calc
#undef tisp_msca_t33_ch_curve_write
#undef tisp_msca_t33_ctrl_reg_write
#include "tx_isp_t33_msca_api2.h"
#include "tx_isp_t33_mscastrm.h"
#include "tx_isp_t33_mscaline.h"
#include "tx_isp_t33_mscachx.h"
#include "tx_isp_t33_mscacur.h"
#include "tx_isp_t33_r13B.h"
#include "tx_isp_t33_sinlut.h"
/* tisp_msca_t33_channel_attr_set only (msca-api6); its FIFO helpers and their register declarations are not used here */
#define tisp_reg_write_t33	t33_osd_api6_reg_write_unused
#define tisp_reg_read_t33	t33_osd_api6_reg_read_unused
#include "tx_isp_t33_msca_api6.h"
#undef tisp_reg_write_t33
#undef tisp_reg_read_t33

#define T33_OSD_CH		3	/* tuning channels the dispatcher passes; one MSCA attribute block (single sensor) */
#define T33_MSCA_ATTR_BYTES	6036
#define T33_TPARAMS_MSCA	0xa30c	/* tparamsP[ch] + 0xa30c: the MSCA parameters (msca_info word 0) */

int isp_printf(unsigned int level, const char *fmt, ...);
void t33_msca_list_refresh(void);		/* tx_isp_t32_hw.c: tispMscaRefresh = 1 */
void t32_core_in_size(u32 *w, u32 *h);		/* tx_isp_t32_core.c */
extern u32 t33_st_isp_init_par_mode[1008];	/* stIspInitPar (tx_isp_t32_hw.c) */
int tisp_rcfg_get_dma_state(u32 mod);		/* tx_isp_t33_bst.h */
int tisp_rcfg_module_cfg_trig(u32 ch, u32 mod);

static u8 *stMscaAttr[1];
static u8 *msca_info[1];
static u32 MscaOsdInfo;
static u8 mscaCfgMode = 1;			/* list slots (tisp_reg_write_t33 on list 23) */
static u8 *tparamsP[1];
#define T33_MSCA_SEC_COPY	0x200		/* msca_info word 0 (+ 0xdc for word 1): bytes of the section 25 copy */
static u8 *msca_sec_copy;			/* the vendor's tparams copy of section 25 (curve_calc writes its steps there) */
static u32 fcrop_attr[60];			/* fcrop_attr: the front crop table of tisp_msca_t33_channel_attr_set */
static u32 g_colotList[5];			/* the draw colour list: 4 colours + fill count (global, as the vendor) */
static u16 attr_w, attr_h;
static bool osd_touched, mask_touched, draw_touched, flip_touched;
static u8 last_osd, last_mask_ch, last_mask_win, last_draw;
static DEFINE_MUTEX(t33_osd_mutex);	/* stMscaAttr[0] / msca_info[0], the stored windows, list 23 words (#578) */
static DEFINE_SPINLOCK(t33_attr_lock);	/* stMscaAttr[0] pointer vs the channel stream on / off (atomic; g16-3) */

void t33_osd_lock(void)
{
	mutex_lock(&t33_osd_mutex);
}

void t33_osd_unlock(void)
{
	mutex_unlock(&t33_osd_mutex);
}

/* tisp_msca_t33_init's callees that tx_isp_t32_hw.c owns (see the header comment) */
void *private_kmalloc(u32 size, u32 flags)
{
	return kmalloc(size, flags);
}

int tisp_rcfg_module_register(u32 chn, u32 mod, void *reg_init)
{
	return 0;
}

int system_irq_func_set(u32 chn, u32 irq, void *fn)
{
	return 0;
}

static void msca_reg_init_none(void) { }
static void msca_frame_irq_none(void) { }

static void attr_free(void)
{
	u8 *a = stMscaAttr[0], *i = msca_info[0], *s = msca_sec_copy;
	unsigned long fl;

	spin_lock_irqsave(&t33_attr_lock, fl);
	stMscaAttr[0] = NULL;
	msca_info[0] = NULL;
	msca_sec_copy = NULL;
	spin_unlock_irqrestore(&t33_attr_lock, fl);
	kfree(a);
	kfree(i);
	kfree(s);
}

/*
 * tisp_msca_t33_init(0, par) for input size w x h (par +16 / +18), stIspInitPar
 * init mode word 226; first call or a new size only.
 */
static u8 *attr_get_size(u32 w, u32 h)
{
	const u8 *bin, *sec = NULL;
	u32 bytes, off, len;
	u8 par[20] = { 0 };
	u8 *la[1] = { NULL }, *li[1] = { NULL };
	unsigned long fl;

	if (stMscaAttr[0] && attr_w == w && attr_h == h)
		return stMscaAttr[0];
	if (!w || !h)
		return stMscaAttr[0];
	if (!t32_core_iq_bin(&bin, &bytes) && (off = t32_iq_section(bin, bytes, T33_SEC_MSCA, &len)) && len >= 0x30)
		sec = bin + off;
	attr_free();
	/* the vendor's tparamsP is its own copy of the bin: curve_calc / chx_cfg_load write into it (not into the bin) */
	msca_sec_copy = kzalloc(T33_MSCA_SEC_COPY, GFP_KERNEL);
	if (!msca_sec_copy)
		return NULL;
	if (sec)
		memcpy(msca_sec_copy, sec, min_t(u32, len, T33_MSCA_SEC_COPY));
	tparamsP[0] = msca_sec_copy - T33_TPARAMS_MSCA;
	par[16] = (u8)w;
	par[17] = (u8)(w >> 8);
	par[18] = (u8)h;
	par[19] = (u8)(h >> 8);
	/*
	 * #658 a: init into local tables and publish both pointers together under t33_attr_lock.  The vendor runs
	 * tisp_msca_t33_init at lib_tisp_init before any channel exists; here the first tuning call or stream-on
	 * can race a channel enable (t33_msca_chx_start reads msca_info[0] word 0, the parameter pointer), so the
	 * channel side must never see a half-filled block (PREEMPT kernel, GFP_KERNEL allocation in between).
	 */
	if (t33_msca_t33_init(0, par, la, li, &MscaOsdInfo, tparamsP, t33_st_isp_init_par_mode[226],
			      mscaCfgMode, (void *)msca_reg_init_none, (void *)msca_frame_irq_none)) {
		kfree(la[0]);
		kfree(li[0]);
		attr_free();
		return NULL;
	}
	spin_lock_irqsave(&t33_attr_lock, fl);
	stMscaAttr[0] = la[0];
	msca_info[0] = li[0];
	spin_unlock_irqrestore(&t33_attr_lock, fl);
	attr_w = (u16)w;
	attr_h = (u16)h;
	return stMscaAttr[0];
}

static u8 *attr_get(void)
{
	u32 w = 0, h = 0;

	t32_core_in_size(&w, &h);
	/* the init writes 0xf0128 directly: not into a gated ISP (before the first stream-on / after the last close) */
	if ((!stMscaAttr[0] || attr_w != w || attr_h != h) && !t32_core_clk_active())
		return stMscaAttr[0];
	return attr_get_size(w, h);
}

/* stream-on, after list 23 was built for the w x h input (tx_isp_t32_hw.c, under t33_osd_lock) */
void t33_msca_attr_stream_on(u32 w, u32 h)
{
	lockdep_assert_held(&t33_osd_mutex);
	attr_get_size(w, h);
}

/*
 * g16-3: tisp_msca_t33_api_chx_stream_on / _off (block msca-strm) with tisp_msca_t33_chx_en (hand port,
 * block mscachx) on stMscaAttr[0] and list 23, called from the channel start / stop (frame-channel lock held,
 * atomic): the stream flag byte 6032 + ch, the FIFO clear slots 30 * ch + 42 / + 50 = 0, the enable bit of
 * slot 3 (set only while the flag is 1).  Returns -1 without the attribute block (the caller then writes
 * the same three slots itself).  tispMscaRefresh: the caller (list 23 run).
 */
static u32 chx_rd(u32 id, u32 blk, u32 reg)
{
	return tisp_reg_read_t33(id, blk, reg);
}

static void chx_wr(u32 id, u32 blk, u32 reg, u32 val)
{
	tisp_reg_write_t33(id, blk, reg, val);
}

/*
 * g17-2: tisp_msca_t33_chx_cfg_load with its proven callees (tx_isp_t33_mscacur.h, blocks mscacur / mscanorm /
 * mscacurc): scaling_algorithm, curve_calc (t33_sin on the vendor kernel tables), ch_curve_write and
 * ctrl_reg_write (chx_en, list 23 trigger tisp_rcfg_module_cfg_trig or tispMscaRefresh).
 */
static u8 cur_refresh;				/* tispMscaRefresh of the channel start */
static const struct t33_mscacur cur_ctx;
static const struct t33_mscachx chx_ctx;

static void chx_scl(u32 id, u32 ch) { t33_msca_scaling_algorithm_v(&cur_ctx, id); }
static void chx_cc(u32 id, u32 ch) { t33_msca_curve_calc_v(&cur_ctx, id, ch); }
static void chx_cw(u32 id, u32 ch) { t33_msca_ch_curve_write_v(&cur_ctx, id, ch); }
static int chx_ctl(u32 id, u32 ch) { return t33_msca_ctrl_reg_write_v(&cur_ctx, id, ch); }

static const struct t33_mscachx chx_ctx = {
	msca_info, stMscaAttr, &mscaCfgMode, chx_scl, chx_cc, chx_cw, chx_ctl, chx_rd, chx_wr,
};

static void chx_en(u32 id, u32 ch, u32 en)
{
	t33_msca_chx_en_v(&chx_ctx, id, ch, en);
}

static int cur_wr(u32 id, u32 mod, u32 reg, u32 val) { return tisp_reg_write_t33(id, mod, reg, val); }
static void cur_sin(u32 n, u32 cnt, short *out) { t33_sin(n, cnt, out); }

static const struct t33_mscacur cur_ctx = {
	msca_info, stMscaAttr, &mscaCfgMode, (const u8 *)t33_st_isp_init_par_mode, &cur_refresh, cur_wr, cur_sin,
	t33_msca_two_point_v, t33_msca_thr_point_v, t33_msca_four_point_v, t33_msca_six_point_v,
	system_reg_write, system_reg_read, chx_en, tisp_rcfg_get_dma_state, tisp_rcfg_module_cfg_trig,
};

/*
 * The channel start of ispcore_pad_event_handle (0x3000003, single sensor) on stMscaAttr[0]:
 * lib_tisp_msca_channel_attr_set with the S_FMT attribute a[13] (the vendor stores it at S_FMT; here at the
 * start, from the stored format), lib_tisp_msca_channel_start (chx_cfg_load), lib_tisp_msca_chx_stream_on,
 * then lib_tisp_msca_chx_en (ch, 1) after the buffer push (the driver pushed the queued buffers before).  The
 * vendor's lib_tisp_api_set_msca_dma_stride in between is refused with stIspInitPar word 226 == 1 (printk only;
 * not made, vendor-equal: it never ran in the vendor QEMU runs, no printk there, #650).  Returns tispMscaRefresh, -1 without the attribute block.  Frame-channel lock held (atomic).
 */
void t33_osd_az_scaling(u32 ch) { t33_msca_scaling_algorithm_v(&cur_ctx, ch); }
void t33_osd_az_curve_calc(u32 ch, u32 i) { t33_msca_curve_calc_v(&cur_ctx, ch, i); }
void t33_osd_az_curve_write(u32 ch, u32 i) { t33_msca_ch_curve_write_v(&cur_ctx, ch, i); }
void t33_osd_az_ctrl(u32 ch, u32 i) { t33_msca_ctrl_reg_write_v(&cur_ctx, ch, i); }

int t33_msca_chx_start(u32 ch, const u32 *a)
{
	unsigned long fl;
	int r = -1;

	spin_lock_irqsave(&t33_attr_lock, fl);
	if (stMscaAttr[0] && msca_info[0] && ch < 3) {
		cur_refresh = 0;
		t33_msca_channel_attr_set(ch, a, stMscaAttr, fcrop_attr);
		t33_msca_chx_cfg_load_v(&chx_ctx, 0, ch);
		t33_msca_stream_on(stMscaAttr, &cur_refresh, ch, chx_en);
		chx_en(0, ch, 1);
		r = cur_refresh;
	}
	spin_unlock_irqrestore(&t33_attr_lock, fl);
	return r;
}

/*
 * #1007 (beyond vendor, proposal): the QBUF stride of a channel whose buffer is wider than the vendor pitch
 * (a FrameSource Joint), ALIGN16(stride) on the stride words of tisp_msca_t33_ctrl_reg_write (stIspInitPar
 * word 226 == 1: system_reg_write 0xf0140 / 0xf0160 + ch * 0x100, else the list 23 slots / registers).  The
 * one-sensor vendor start never writes it (the joint rows land at the channel-width pitch there).
 */
void t33_msca_dma_stride(u32 ch, u32 stride)
{
	u32 v = t33_msca_stride_value(0, stride), c = ch & 0xff, b = 30 * c, d = c << 8;
	unsigned long fl;

	spin_lock_irqsave(&t33_attr_lock, fl);
	if (t33_st_isp_init_par_mode[226] == 1) {
		system_reg_write(d + 0xf0140, v);
		system_reg_write(d + 0xf0160, v);
	} else {
		tisp_reg_write_t33(0, 23, mscaCfgMode ? b + 0x29 : d + 0xf0140, v);
		tisp_reg_write_t33(0, 23, mscaCfgMode ? b + 0x31 : d + 0xf0160, v);
	}
	spin_unlock_irqrestore(&t33_attr_lock, fl);
}

int t33_msca_chx_stream_set(u32 ch, u32 on)
{
	unsigned long fl;
	u8 refresh = 0;
	int r = -1;

	spin_lock_irqsave(&t33_attr_lock, fl);
	if (stMscaAttr[0] && ch < 3) {
		if (on)
			t33_msca_stream_on(stMscaAttr, &refresh, ch, chx_en);
		else
			t33_msca_stream_off(stMscaAttr, &refresh, ch, chx_en);
		r = 0;
	}
	spin_unlock_irqrestore(&t33_attr_lock, fl);
	return r;
}

void t33_msca_attr_exit(void)
{
	mutex_lock(&t33_osd_mutex);
	attr_free();
	attr_w = attr_h = 0;
	/* nothing stored any more: the next list 23 build carries no OSD / mask / draw / flip words (#577) */
	osd_touched = mask_touched = draw_touched = flip_touched = false;
	mutex_unlock(&t33_osd_mutex);
}

static void wr23(u32 ch, u32 blk, u32 reg, u32 val)
{
	tisp_reg_write_t33(0, blk, reg, val);
}

static int mo_msg(u32 level, const char *fmt, ...)
{
	char b[160];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(b, sizeof(b), fmt, ap);
	va_end(ap);
	return isp_printf(level, "%s", b);
}

/* the port's tispMscaRefresh store -> list 23 at the next frame end */
static void refresh_done(u8 r)
{
	if (r)
		t33_msca_list_refresh();
}

static void mo_env(struct t33_mo_env *e, u8 *a, u8 *refresh)
{
	e->attr = a;
	e->cfg_mode = mscaCfgMode;
	e->refresh = refresh;
	e->osd_info = &MscaOsdInfo;
	e->wr = wr23;
	e->msg = mo_msg;
}

static void ml_wr(u32 ch, u32 reg, u32 v) { tisp_reg_write_t33(0, T33_MOD_MSCA, reg, v); }
static void ml_refresh(u32 ch) { t33_msca_list_refresh(); }
static void ml_note(u32 what)
{
	static const char *const m[] = { "colour list full", "msk size error", "line end points equal", "unknown line type" };

	isp_printf(1, "%s: %s\n", "tisp_msca_t33_api_set_line_out_cfg", what < 4 ? m[what] : "?");
}

static const struct t33_line_ops ml_ops = { ml_wr, ml_refresh, ml_note };

static int t32_tx_os_tisp_s_osd_attr_nl(u32 ch, void *buf)
{
	u8 *a = ch < T33_OSD_CH ? attr_get() : NULL;

	if (!a)
		return -EINVAL;
	osd_touched = true;
	t33_msca_set_osd_out(a, buf);	/* tisp_msca_t33_api_set_osd_out_attribute_cfg: store only */
	return 0;
}

static int t32_tx_os_tisp_g_osd_attr_nl(u32 ch, void *buf)
{
	u8 *a = ch < T33_OSD_CH ? attr_get() : NULL;

	if (!a)
		return -EINVAL;
	memset(buf, 0, 32);
	t33_msca_get_osd_out(a, buf);
	return 0;
}

static int t32_tx_os_tisp_s_osd_block_attr_nl(u32 ch, void *buf)
{
	u8 *a = ch < T33_OSD_CH ? attr_get() : NULL, r = 0;
	struct t33_mo_env e;

	if (!a)
		return -EINVAL;
	osd_touched = true;
	last_osd = ((u8 *)buf)[0];
	mo_env(&e, a, &r);
	t33_msca_osd_out_cfg(&e, 0, buf);
	refresh_done(r);
	return 0;
}

static int t32_tx_os_tisp_g_osd_block_attr_nl(u32 ch, void *buf)
{
	u8 *a = ch < T33_OSD_CH ? attr_get() : NULL, *p = buf;

	if (!a)
		return -EINVAL;
	memset(buf, 0, 20);
	p[0] = last_osd;
	if (p[0] > 3)
		return 0;
	t33_msca_get_osd_cfg(stMscaAttr, 0, p);
	if (p[1])	/* the stored word, not the vendor's attribute address */
		t33_osd_p32(p + 12, t33_osd_u32(a + 712 + 20 * p[0] + 12));
	return 0;
}

static int t32_tx_os_tisp_s_mscaler_mask_block_attr_nl(u32 ch, void *buf)
{
	u8 *a = ch < T33_OSD_CH ? attr_get() : NULL, r = 0;
	struct t33_mo_env e;

	if (!a)
		return -EINVAL;
	mask_touched = true;
	last_mask_ch = ((u8 *)buf)[0];
	last_mask_win = ((u8 *)buf)[1];
	mo_env(&e, a, &r);
	t33_msca_mask_out_cfg(&e, 0, buf);
	refresh_done(r);
	return 0;
}

static int t32_tx_os_tisp_g_mscaler_mask_block_attr_nl(u32 ch, void *buf)
{
	u8 *a = ch < T33_OSD_CH ? attr_get() : NULL, *p = buf;

	if (!a)
		return -EINVAL;
	memset(buf, 0, 24);
	p[0] = last_mask_ch;
	p[1] = last_mask_win;
	if (p[0] > 2 || p[1] > 3)
		return 0;
	t33_msca_get_mask_out_cfg(0, p, stMscaAttr);
	return 0;
}

static int t32_tx_os_tisp_s_draw_block_attr_nl(u32 ch, void *buf)
{
	u8 *a = ch < T33_OSD_CH ? attr_get() : NULL;

	if (!a)
		return -EINVAL;
	if (((u8 *)buf)[0] >= T33_ML_WIN)	/* the vendor writes past its windows */
		return 0;
	draw_touched = true;
	last_draw = ((u8 *)buf)[0];
	t33_msca_line_set_out(a, g_colotList, mscaCfgMode, &ml_ops, 0, buf);
	return 0;
}

static int t32_tx_os_tisp_g_draw_block_attr_nl(u32 ch, void *buf)
{
	u8 *a = ch < T33_OSD_CH ? attr_get() : NULL, *p = buf;

	if (!a)
		return -EINVAL;
	memset(buf, 0, 36);
	p[0] = last_draw;
	if (p[0] >= T33_ML_WIN)
		return 0;
	t33_msca_get_line_cfg(stMscaAttr, 0, p);
	return 0;
}

/*
 * g17-3: tisp_s_autozoom_control / tisp_g_autozoom_control -> tisp_msca_t33_api_set_autozoom (msca-api5) /
 * _get_autozoom (msca-api2) on stMscaAttr[0] and fcrop_attr: per output channel the zoom window (or the front
 * crop / the input size when off), scaling_algorithm, curve_calc, ch_curve_write, ctrl_reg_write (all three
 * channels, as the vendor) and slot 1 = 1, tispMscaRefresh.  Attribute lock held (the channel start changes the
 * same block and list words); t33_osd_mutex around it as the other tuning entries.  buf: IMPISPAutoZoom (60 B).
 */
static int t33_msca_autozoom_set_nl(u32 ch, void *buf)
{
	unsigned long fl;
	u8 r = 0;

	if (ch >= T33_OSD_CH || !attr_get())
		return -EINVAL;
	spin_lock_irqsave(&t33_attr_lock, fl);
	cur_refresh = 0;
	t33_msca_set_autozoom(0, buf, stMscaAttr, fcrop_attr, &mscaCfgMode, &r);
	r |= cur_refresh;
	spin_unlock_irqrestore(&t33_attr_lock, fl);
	refresh_done(r);
	return 0;
}

static int t33_msca_autozoom_get_nl(u32 ch, void *buf)
{
	u8 *a = ch < T33_OSD_CH ? attr_get() : NULL;

	if (!a)
		return -EINVAL;
	t33_msca_get_autozoom(a, buf);
	return 0;
}

/* tisp_s_hv_flip -> tisp_msca_t33_api_set_mirr_flip: byte 0 bits 0..1 the output, bits 2..7 its flags */
static int t33_msca_hv_flip_set_nl(u32 ch, void *flags)
{
	u8 *a = ch < T33_OSD_CH ? attr_get() : NULL, r = 0;

	if (!a)
		return -EINVAL;
	flip_touched = true;
	t33_msca_set_mirr_flip(0, flags, stMscaAttr, &mscaCfgMode, &r);
	refresh_done(r);
	return 0;
}

static int t33_msca_hv_flip_get_nl(u32 ch, void *flags)
{
	u8 *a = ch < T33_OSD_CH ? attr_get() : NULL;

	if (!a)
		return -EINVAL;
	t33_msca_get_mirr_flip(a, flags);
	return 0;
}

/* the tuning entries: t33_osd_mutex around each (#578) */
#define T33_OSD_LOCKED(name)						\
int name(u32 ch, void *buf)						\
{									\
	int r;								\
									\
	mutex_lock(&t33_osd_mutex);					\
	r = name##_nl(ch, buf);						\
	mutex_unlock(&t33_osd_mutex);					\
	return r;							\
}

T33_OSD_LOCKED(t32_tx_os_tisp_s_osd_attr)
T33_OSD_LOCKED(t32_tx_os_tisp_g_osd_attr)
T33_OSD_LOCKED(t32_tx_os_tisp_s_osd_block_attr)
T33_OSD_LOCKED(t32_tx_os_tisp_g_osd_block_attr)
T33_OSD_LOCKED(t32_tx_os_tisp_s_mscaler_mask_block_attr)
T33_OSD_LOCKED(t32_tx_os_tisp_g_mscaler_mask_block_attr)
T33_OSD_LOCKED(t32_tx_os_tisp_s_draw_block_attr)
T33_OSD_LOCKED(t32_tx_os_tisp_g_draw_block_attr)
T33_OSD_LOCKED(t33_msca_hv_flip_set)
T33_OSD_LOCKED(t33_msca_hv_flip_get)
T33_OSD_LOCKED(t33_msca_autozoom_set)
T33_OSD_LOCKED(t33_msca_autozoom_get)

/*
 * The MSCA list (23) is built again at every stream-on (the vendor keeps its
 * list memory): put the stored OSD windows / mask / draw words and the
 * mirror / flip bits back (tisp_msca_t33_api_set_osd_reg / _set_mask_reg /
 * tisp_msca_t33_set_line_reg / _api_set_mirr_flip).  Channel 0 only (single sensor list).
 * Caller holds t33_osd_lock.
 */
void t33_osd_list_rebuilt(void)
{
	u8 *a = stMscaAttr[0], r = 0;
	struct t33_mo_env e;

	lockdep_assert_held(&t33_osd_mutex);
	if (!a)
		return;
	mo_env(&e, a, &r);
	if (osd_touched)
		t33_msca_osd_reg(&e, 0);
	if (mask_touched)
		t33_msca_mask_reg(&e, 0);
	if (draw_touched)
		t33_msca_line_set_reg(a, g_colotList, mscaCfgMode, &ml_ops, 0);
	if (flip_touched) {	/* block 0's flags: the port puts the bits of all three blocks into slot 7 */
		u8 f = (u8)((a[80] & 0x3f) << 2);

		t33_msca_set_mirr_flip(0, &f, stMscaAttr, &mscaCfgMode, &r);
	}
	refresh_done(r);
}
