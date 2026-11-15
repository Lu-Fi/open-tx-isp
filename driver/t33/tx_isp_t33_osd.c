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
 * - the attribute block is set up once (first stream-on or first tuning call)
 *   and again only when the input size changes; it survives stream-off, and
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
#include <linux/lockdep.h>
#include "tx_isp_t32.h"
#include "tx_isp_t32_iq.h"
#include "tx_isp_t33_mscaInit.h"
#include "tx_isp_t33_mscaosd.h"
#include "tx_isp_t33_msca_api4.h"
#include "tx_isp_t33_msca_api5.h"
#include "tx_isp_t33_mscastrm.h"
#include "tx_isp_t33_mscaline.h"

#define T33_OSD_CH		3	/* tuning channels the dispatcher passes; one MSCA attribute block (single sensor) */
#define T33_MSCA_ATTR_BYTES	6036
#define T33_TPARAMS_MSCA	0xa30c	/* tparamsP[ch] + 0xa30c: the MSCA parameters (msca_info word 0) */

int isp_printf(unsigned int level, const char *fmt, ...);
void t33_msca_list_refresh(void);		/* tx_isp_t32_hw.c: tispMscaRefresh = 1 */
void t32_core_in_size(u32 *w, u32 *h);		/* tx_isp_t32_core.c */
extern u32 t33_st_isp_init_par_mode[1008];	/* stIspInitPar (tx_isp_t32_hw.c) */

static u8 *stMscaAttr[1];
static u8 *msca_info[1];
static u32 MscaOsdInfo;
static u8 mscaCfgMode = 1;			/* list slots (tisp_reg_write_t33 on list 23) */
static u8 *tparamsP[1];
static u8 msca_sec_zero[0x30];			/* no section 25: its +0x2c word reads 0 */
static u32 g_colotList[5];			/* the draw colour list: 4 colours + fill count (global, as the vendor) */
static u16 attr_w, attr_h;
static bool osd_touched, mask_touched, draw_touched, flip_touched;
static u8 last_osd, last_mask_ch, last_mask_win, last_draw;
static DEFINE_MUTEX(t33_osd_mutex);	/* stMscaAttr[0] / msca_info[0], the stored windows, list 23 words (#578) */

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
	kfree(stMscaAttr[0]);
	kfree(msca_info[0]);
	stMscaAttr[0] = NULL;
	msca_info[0] = NULL;
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

	if (stMscaAttr[0] && attr_w == w && attr_h == h)
		return stMscaAttr[0];
	if (!w || !h)
		return stMscaAttr[0];
	if (!t32_core_iq_bin(&bin, &bytes) && (off = t32_iq_section(bin, bytes, T33_SEC_MSCA, &len)) && len >= 0x30)
		sec = bin + off;
	attr_free();
	tparamsP[0] = (u8 *)(sec ? sec : msca_sec_zero) - T33_TPARAMS_MSCA;
	par[16] = (u8)w;
	par[17] = (u8)(w >> 8);
	par[18] = (u8)h;
	par[19] = (u8)(h >> 8);
	if (t33_msca_t33_init(0, par, stMscaAttr, msca_info, &MscaOsdInfo, tparamsP, t33_st_isp_init_par_mode[226],
			      mscaCfgMode, (void *)msca_reg_init_none, (void *)msca_frame_irq_none)) {
		attr_free();
		return NULL;
	}
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

void t33_msca_attr_exit(void)
{
	mutex_lock(&t33_osd_mutex);
	attr_free();
	attr_w = attr_h = 0;
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
