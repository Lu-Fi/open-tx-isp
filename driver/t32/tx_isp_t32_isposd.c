// SPDX-License-Identifier: GPL-2.0
/*
 * T32 ISP-drawn OSD (openimp #336): the request path from libimp to the
 * ported MSCA OSD / mask / line code.
 *
 * libimp 2.2.0 hands its ISP OSD to the driver as isp-m0 tuning controls
 * (DEFAULT_CMD_ISP_TUNING 0xc0105467, {vinum, get, id, user pointer}),
 * recorded in QEMU (openimp tools/t32 isposd run, #336):
 *   0x08000074 mask block    IMP_ISP_Tuning_Set/GetMaskBlock   24 B  (cover)
 *   0x080000a0 draw block    IMP_ISP_Tuning_Set/GetDrawBlock   36 B  (line, range, window)
 *   0x080000a1 OSD attribute IMP_ISP_Tuning_Set/GetOSDAttr     32 B  (picture group format)
 *   0x080000a2 OSD block     IMP_ISP_Tuning_Set/GetOSDBlock    20 B  (picture window)
 * As the vendor tx_isp_core_ops_s_ctrl / g_ctrl (ported in tx_isp_t32_txsctrl.h /
 * tx_isp_t32_txgctrl.h) each id copies the block in or out and calls the vendor setter
 * or getter (tisp_s/g_mscaler_mask_block_attr, tisp_s/g_draw_block_attr,
 * tisp_s/g_osd_attr, tisp_s/g_osd_block_attr), which reach the ported MSCA API
 * (tx_isp_t32_msca_api.h, _set.h, _arr.h: tisp_msca_api_set_mask_out_cfg,
 * _line_out_cfg, _osd_out_attribute_cfg, _osd_out_cfg and their getters).  These
 * only fill the channel's MSCA parameter block (vendor t32_data_e2768[vi], 2636 B)
 * and mark what changed.
 *
 * The hardware sees it at the next MSCA frame done (ISP status 0x300a8 bit 1, the
 * vendor's system irq 33): tisp_msca_irq_frame_done (tx_isp_t32_msca_irq.h, oracle
 * t32_mscafd_oracle) runs tisp_msca_cfg_shd(vi, 0) (tx_isp_t32_msca_cfg.h), which pushes the pending OSD windows, masks and lines as
 * (value, register) pairs through the MSCA shadow-RAM port (0xf8000 / 0xf8004 /
 * 0xf8100), guarded by the shadow-RAM flag 0xf09e0 (token 0x5a).  The write list of
 * the ported function goes to the registers in its order.
 *
 * The parameter block's picture size (set 0 crop window, +44 / +46) is the sensor size
 * the MSCA starts channel 0 with (no frame crop on this driver yet); the OSD block
 * setter checks windows against it, as the vendor's.  The shadow-RAM flag (0xf09e0:
 * token 0x5a in bits 23:16, low half the last polyphase-curve word) gets its token
 * from the channel-start curve load (tx_isp_t32_hw.c) as the vendor's from its init
 * (op 2) and every transfer; the commit's flag pair keeps the low half the register
 * holds (the vendor's channel word +302, the same curve word: 0x5a0155 in QEMU).
 *
 * Not the vendor's (needs the user's OK to differ, see the commit):
 *  - an index the vendor uses unchecked (OSD window >= 8, line >= 20, mask set >= 3 /
 *    block >= 4, a VI other than 0) is refused with -EINVAL instead of writing outside
 *    the parameter block;
 *  - the getters start from a zeroed block (the vendor's is uninitialised stack, so
 *    the window / line / set index it reads back is undefined); index 0 is answered.
 * The vendor's other paths that use these setters (tisp parameter restore,
 * tisp_api_param_array_set) are not wired.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/string.h>

#include "tx_isp_t32.h"
#include "tx_isp_t32_msca_irq.h"
#include "tx_isp_t32_msca_set.h"
#include "tx_isp_t32_msca_arr.h"

#define T32_OSD_ID_MASK		0x08000074U
#define T32_OSD_ID_DRAW		0x080000a0U
#define T32_OSD_ID_ATTR		0x080000a1U
#define T32_OSD_ID_BLOCK	0x080000a2U
#define T32_OSD_WRITES		512

static unsigned int t32_isposd_trace;
module_param(t32_isposd_trace, uint, 0644);
MODULE_PARM_DESC(t32_isposd_trace, "1: log every ISP OSD request and every MSCA OSD commit");

static DEFINE_SPINLOCK(osd_lock);
static u8 osd_sb[T32_MSCA_BLK_SZ];		/* t32_data_e2768[0] */
static u8 osd_mask_marked;			/* vendor t32_data_f3258[14] */
static u32 osd_info;				/* t32_data_e28bc: alignment fixes */
static u16 osd_w302;				/* the channel context's word +302 (0xf09e0 low half) */
static u8 osd_par[256];				/* channel parameter block (unused: no autozoom here) */
static struct t32_msca_ch osd_ch = { .par = osd_par };
static struct t32_front_write osd_w[T32_OSD_WRITES];
static unsigned int osd_refused;		/* cfg_shd messages (busy / flag not ready) */

static u32 osd_rd(u32 reg)
{
	return t32_hw_rd(reg);
}

static void osd_nolock(void)
{
}

static void osd_msg(unsigned int kind, u32 idx)
{
	if (kind == 0)
		osd_refused++;
	else
		isp_printf(1, "tx-isp-t32: msca line %u refused (sloped line >= 8)\n", idx);
}

static const struct t32_msca_cfg_ops osd_cfg_ops = {
	.lock = osd_nolock,	/* osd_lock is held around the call */
	.unlock = osd_nolock,
	.msg = osd_msg,
};

/* tisp_msca_irq_frame_done(0) under the lock, then its writes in order */
static void osd_commit(void)
{
	struct t32_ms_io io = { .shadow = 0, .w = osd_w, .n = 0, .rd = osd_rd };
	struct t32_msca_cfg_st st = {
		.c = &osd_ch, .sb = osd_sb, .osd_info = &osd_info, .w302 = &osd_w302,
	};
	unsigned int i;

	osd_w302 = t32_hw_rd(0xf09e0) & 0xffff;
	t32_msca_irq_frame_done(&osd_cfg_ops, &io, &st, 0);
	for (i = 0; i < io.n; i++)
		t32_hw_wr(osd_w[i].reg, osd_w[i].val);
	if (t32_isposd_trace && io.n)
		isp_printf(0, "tx-isp-t32: msca osd commit %u writes\n", io.n);
}

/* tisp_msca_irq_frame_done(0): the MSCA frame-done interrupt (0x300a8 bit 1) */
void t32_isposd_frame_done(void)
{
	spin_lock(&osd_lock);
	osd_commit();
	spin_unlock(&osd_lock);
}

/*
 * Channel 0 starts: the parameter block's picture size (set 0: input +32 h / +34 w,
 * crop window +44 w / +46 h, full size +0 w / +6 h).
 */
void t32_isposd_msca_start(u32 in_w, u32 in_h)
{
	unsigned long fl;

	spin_lock_irqsave(&osd_lock, fl);
	t32_msa_w16(osd_sb + 32, in_h);
	t32_msa_w16(osd_sb + 34, in_w);
	t32_msa_w16(osd_sb + 44, in_w);
	t32_msa_w16(osd_sb + 46, in_h);
	t32_msa_w16(osd_sb + 0, in_w);
	t32_msa_w16(osd_sb + 6, in_h);
	spin_unlock_irqrestore(&osd_lock, fl);
}

/* the vendor's index use, checked (see the header) */
static int osd_index_ok(u32 id, const u8 *b)
{
	switch (id) {
	case T32_OSD_ID_MASK:	return b[0] < 3 && b[1] < 4;
	case T32_OSD_ID_DRAW:	return b[0] < 20;
	case T32_OSD_ID_BLOCK:	return b[0] < 8;
	default:		return 1;
	}
}

static unsigned int osd_size(u32 id)
{
	switch (id) {
	case T32_OSD_ID_MASK:	return 24;
	case T32_OSD_ID_DRAW:	return 36;
	case T32_OSD_ID_ATTR:	return 32;
	case T32_OSD_ID_BLOCK:	return 20;
	default:		return 0;
	}
}

/*
 * isp-m0 tuning control of the ISP OSD (vi, get, id, user pointer).  Returns
 * -ENOIOCTLCMD for any other id.  Set: a failed copy returns the copy's result
 * (the mask block: 0), else 0 whatever the setter says (vendor s_ctrl);
 * get: the getter, then the copy's result.
 */
long t32_isposd_ctrl(u32 vi, u32 get, u32 id, u32 uptr)
{
	void __user *u = (void __user *)(uintptr_t)uptr;
	unsigned int n = osd_size(id), msgs = 0;
	unsigned long fl;
	u8 b[96];
	int r = 0;

	if (!n)
		return -ENOIOCTLCMD;
	if (vi != 0)
		return -EINVAL;
	memset(b, 0, sizeof(b));
	if (get) {
		spin_lock_irqsave(&osd_lock, fl);
		switch (id) {
		case T32_OSD_ID_MASK:	t32_msca_api_get_mask_out_cfg(osd_sb, b); break;
		case T32_OSD_ID_DRAW:	t32_msca_api_get_line_out_cfg(osd_sb, b); break;
		case T32_OSD_ID_ATTR:	t32_msca_api_get_osd_out_attr_cfg(osd_sb, b); break;
		case T32_OSD_ID_BLOCK:	t32_msca_api_get_osd_out_cfg(osd_sb, b); break;
		}
		spin_unlock_irqrestore(&osd_lock, fl);
		return copy_to_user(u, b, n) ? -EFAULT : 0;
	}
	if (copy_from_user(b, u, n)) {
		isp_printf(2, "tx-isp-t32: isp osd id %#x copy error\n", id);
		return id == T32_OSD_ID_MASK ? 0 : -EFAULT;
	}
	if (!osd_index_ok(id, b)) {
		isp_printf(1, "tx-isp-t32: isp osd id %#x index %u/%u refused\n", id, b[0], b[1]);
		return -EINVAL;
	}
	spin_lock_irqsave(&osd_lock, fl);
	switch (id) {
	case T32_OSD_ID_MASK:	t32_s_mscaler_mask_block_attr(osd_sb, b, &osd_mask_marked); break;
	case T32_OSD_ID_DRAW:	t32_msca_api_set_line_out_cfg(osd_sb, b); break;
	case T32_OSD_ID_ATTR:	r = t32_msca_api_set_osd_out_attr_cfg(osd_sb, b); break;
	case T32_OSD_ID_BLOCK:	r = t32_msca_api_set_osd_out_cfg(osd_sb, b, &msgs); break;
	}
	spin_unlock_irqrestore(&osd_lock, fl);
	if (r || msgs || t32_isposd_trace)
		isp_printf(r || msgs ? 1 : 0, "tx-isp-t32: isp osd id %#x [%u %u] -> %d (msgs %u)\n",
			   id, b[0], b[1], r, msgs);
	return 0;
}

/* the shadow-RAM port for the channel-start curve load (tx_isp_t32_hw.c) */
unsigned long t32_isposd_ram_lock(void)
{
	unsigned long fl;

	spin_lock_irqsave(&osd_lock, fl);
	return fl;
}

void t32_isposd_ram_unlock(unsigned long flags)
{
	spin_unlock_irqrestore(&osd_lock, flags);
}

/* /proc status line */
void t32_isposd_status(u32 *pending, u32 *refused, u32 *fixes)
{
	*pending = t32_msa_r32(osd_sb + 880) | t32_msa_r32(osd_sb + 884) | t32_msa_r32(osd_sb + 888);
	*refused = osd_refused;
	*fixes = osd_info;
}
