// SPDX-License-Identifier: GPL-2.0
/*
 * T33 (PRJ008) MSCA OSD and mask outputs: the tuning-id store of the vendor
 * dispatcher (tisp_s_osd_attr / tisp_s_osd_block_attr /
 * tisp_s_mscaler_mask_block_attr and the g_ twins, 0x109120 .. 0x1090b4) on
 * the ported tisp_msca_t33_api_* functions (../t32/tx_isp_t33_osd.h).  The
 * register words go to MSCA list 23 (tx_isp_t32_hw.c), which runs at the next
 * frame end.  T33 only (the T32 module keeps the ISP OSD path of
 * tx_isp_t32_isposd.c).  Callers hold the tuning lock.
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/string.h>
#include "tx_isp_t32.h"
#include "tx_isp_t33_osd.h"

#define T33_OSD_CH	3

int isp_printf(unsigned int level, const char *fmt, ...);
void t33_msca_list_slot(int slot, u32 v);	/* tx_isp_t32_hw.c: MSCA list 23 word */
void t33_msca_list_refresh(void);		/* tispMscaRefresh = 1 */
void t32_core_in_size(u32 *w, u32 *h);		/* tx_isp_t32_core.c */

static struct t33_osd osd[T33_OSD_CH];
static struct t33_draw draw[T33_OSD_CH];
static struct t33_colors colors;	/* the vendor's g_colotList: global */
static bool osd_touched, mask_touched, draw_touched;
/*
 * GET: the vendor dispatcher hands the getter an uninitialised stack block (no copy-in), so the
 * window / mask number the getter reads is stack garbage; the answer is the entry the stack happened
 * to name.  Here the block is cleared and names the window / mask written last (deterministic, and
 * no stack bytes reach user space), #442.
 */
static u8 last_osd, last_mask_ch, last_mask_win, last_draw;

static void os_slot(u32 ch, u32 slot, u32 v) { t33_msca_list_slot((int)slot, v); }
static void os_refresh(u32 ch) { t33_msca_list_refresh(); }

static void os_msg(u32 level, const char *fmt, ...)
{
	char b[160];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(b, sizeof(b), fmt, ap);
	va_end(ap);
	isp_printf(level, "%s", b);
}

static const struct t33_osd_ops os_ops = { os_slot, os_refresh, os_msg };

static struct t33_osd *os_get(u32 ch)
{
	u32 w, h;

	if (ch >= T33_OSD_CH)
		return NULL;
	t32_core_in_size(&w, &h);
	osd[ch].in_w = (u16)w;
	osd[ch].in_h = (u16)h;
	return &osd[ch];
}

int t32_tx_os_tisp_s_osd_attr(u32 ch, void *buf)
{
	struct t33_osd *o = os_get(ch);

	if (!o)
		return -EINVAL;
	osd_touched = true;
	t33_osd_set_attr(o, buf);
	return 0;
}

int t32_tx_os_tisp_g_osd_attr(u32 ch, void *buf)
{
	struct t33_osd *o = os_get(ch);

	if (!o)
		return -EINVAL;
	memset(buf, 0, 32);
	t33_osd_get_attr(o, buf);
	return 0;
}

int t32_tx_os_tisp_s_osd_block_attr(u32 ch, void *buf)
{
	struct t33_osd *o = os_get(ch);

	if (!o)
		return -EINVAL;
	osd_touched = true;
	last_osd = ((u8 *)buf)[0];
	t33_osd_set_out(o, &os_ops, ch, buf);
	return 0;
}

int t32_tx_os_tisp_g_osd_block_attr(u32 ch, void *buf)
{
	struct t33_osd *o = os_get(ch);

	if (!o)
		return -EINVAL;
	memset(buf, 0, 20);
	((u8 *)buf)[0] = last_osd;
	t33_osd_get_out(o, buf);
	return 0;
}

int t32_tx_os_tisp_s_mscaler_mask_block_attr(u32 ch, void *buf)
{
	struct t33_osd *o = os_get(ch);

	if (!o)
		return -EINVAL;
	mask_touched = true;
	last_mask_ch = ((u8 *)buf)[0];
	last_mask_win = ((u8 *)buf)[1];
	t33_mask_set_out(o, &os_ops, ch, buf);
	return 0;
}

int t32_tx_os_tisp_g_mscaler_mask_block_attr(u32 ch, void *buf)
{
	struct t33_osd *o = os_get(ch);

	if (!o)
		return -EINVAL;
	memset(buf, 0, 24);
	((u8 *)buf)[0] = last_mask_ch;
	((u8 *)buf)[1] = last_mask_win;
	t33_mask_get_out(o, buf);
	return 0;
}

int t32_tx_os_tisp_s_draw_block_attr(u32 ch, void *buf)
{
	if (ch >= T33_OSD_CH)
		return -EINVAL;
	draw_touched = true;
	last_draw = ((u8 *)buf)[0];
	t33_draw_set_out(&draw[ch], &colors, &os_ops, ch, buf);
	return 0;
}

int t32_tx_os_tisp_g_draw_block_attr(u32 ch, void *buf)
{
	if (ch >= T33_OSD_CH)
		return -EINVAL;
	memset(buf, 0, 36);
	((u8 *)buf)[0] = last_draw;
	t33_draw_get_out(&draw[ch], buf);
	return 0;
}

/*
 * The MSCA list (23) is built again at every stream-on (the vendor keeps its
 * list memory): put the stored OSD windows / mask words back.  Channel 0 only
 * (single sensor list).
 */
void t33_osd_list_rebuilt(void)
{
	if (osd_touched)
		t33_osd_set_reg(&osd[0], &os_ops, 0);
	if (mask_touched)
		t33_mask_set_reg(&osd[0], &os_ops, 0);
	if (draw_touched)
		t33_draw_set_reg(&draw[0], &colors, &os_ops, 0);
}
