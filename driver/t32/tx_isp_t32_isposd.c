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
 * Not the vendor's (beyond vendor, user-approved 2026-11-11, openimp #336):
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
#include "tx_isp_t32_msca_init.h"
#include "tx_isp_t32_msca_attr.h"
#include "tx_isp_t32_msca_chx.h"
#include "tx_isp_t32_iq.h"

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
static u8 osd_par[256];				/* IQ bin section 25 copy (vendor tparamsP + 0xef50, 0xbc B; #730) */
static struct t32_msca_ch osd_ch = { .par = osd_par };
static struct t32_front_write osd_w[T32_OSD_WRITES];
static unsigned int osd_refused;		/* cfg_shd messages (busy / flag not ready) */

/*
 * openimp #730: the vendor's live MSCA state of channel 0 (one sensor): tisp_msca_init at the tisp init,
 * lib_tisp_msca_channel_attr_set + tisp_msca_channel_start (tisp_msca_chx_cfg_load) +
 * lib_tisp_msca_chx_stream_on + lib_tisp_api_set_msca_dma_stride at a frame channel's start,
 * lib_tisp_msca_chx_stream_off + lib_tisp_msca_chx_reg_stream_off at its stop.  osd_sb is the
 * parameter block (st_msca_chx_cfg[0]), osd_ch the context (msca_info[0]: par = osd_par, coef = ctx + 4).
 */
#define T32_IQ_SEC_MSCA		25
#define T32_MSCA_PAR_BYTES	0xbcU		/* tisp_bin_data_parse: section 25 -> tparamsP + 0xef50 */
#define T32_MSCA_START_WRITES	1024
static u8 msca_ctx[T32_MSCA_INIT_CTX_SZ];	/* tisp_msca_init's context allocation (coefs mirrored in osd_ch) */
static u8 msca_saved[15 * 4];			/* fcrop_attr[0]: words set + 3 * k */
static u8 msca_def[8];				/* stIspInitPar[0] width / height (the sensor size) */
static u8 msca_az;				/* autozoom[0] (t32_data_e2760[0], #721) */
static u8 msca_live;				/* tisp_msca_init ran on the bin's section 25 */
static struct t32_msca_init_st msca_st;
static struct t32_front_write msca_w[T32_MSCA_START_WRITES];
static u32 msca_inits, msca_starts, msca_stops;

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
		.c = &osd_ch, .sb = osd_sb, .az = msca_az, .osd_info = &osd_info, .w302 = &osd_w302,
	};
	unsigned int i;

	osd_w302 = msca_live ? osd_ch.coef[T32_MSCA_COEFS - 1] : t32_hw_rd(0xf09e0) & 0xffff;
	t32_msca_irq_frame_done(&osd_cfg_ops, &io, &st, 0);
	msca_az = st.az;
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

static u8 *msca_kmalloc(u32 size)
{
	return size == T32_MSCA_INIT_SETS_SZ ? osd_sb : size == T32_MSCA_INIT_CTX_SZ ? msca_ctx : NULL;
}

static void msca_lock_init(void)
{
}

static void msca_error(u32 line)
{
	isp_printf(2, "[%s:%d]: Kmalloc error!!!\n", "tisp_msca_init", line);
}

static void msca_irq_set(u32 ch, u32 irq)
{
	/* irq 33 -> tisp_msca_irq_frame_done: t32_isp_irq calls t32_isposd_frame_done */
}

static const struct t32_msca_init_ops msca_init_ops = {
	.kmalloc = msca_kmalloc, .lock_init = msca_lock_init, .error = msca_error, .irq_func_set = msca_irq_set,
};

static void msca_flush(struct t32_ms_io *io)
{
	unsigned int i;

	for (i = 0; i < io->n; i++)
		t32_hw_wr(io->w[i].reg, io->w[i].val);
	io->n = 0;
}

/*
 * tisp_msca_init(0, stIspInitPar) at the end of the tisp init (lib_tisp_init): a fresh parameter block and
 * context (OSD / mask / line state gone, as the vendor's kmalloc), the bin's section 25 as the parameter
 * block, the MSCA init writes.  Returns 0, or -1 when the bin has no usable section 25 (the caller keeps
 * the direct channel start then).
 */
int t32_isposd_msca_init(u32 w, u32 h)
{
	static u8 p[904];	/* stIspInitPar of channel 0: width, height, word 225 the pipeline count */
	struct t32_ms_io io = { .shadow = 0, .w = msca_w, .n = 0, .rd = osd_rd };
	const u8 *bin;
	u32 bytes, off, len;
	unsigned long fl;
	unsigned int i;
	int r;

	off = t32_core_iq_bin(&bin, &bytes) ? 0 : t32_iq_section(bin, bytes, T32_IQ_SEC_MSCA, &len);
	spin_lock_irqsave(&osd_lock, fl);
	msca_live = 0;
	if (!off || len < T32_MSCA_PAR_BYTES) {
		spin_unlock_irqrestore(&osd_lock, fl);
		isp_printf(1, "tx-isp-t32: IQ bin has no MSCA section, direct channel start\n");
		return -1;
	}
	memset(osd_par, 0, sizeof(osd_par));
	memcpy(osd_par, bin + off, T32_MSCA_PAR_BYTES);
	memset(p, 0, sizeof(p));
	t32_msa_w32(p, w);
	t32_msa_w32(p + 4, h);
	t32_msa_w32(p + 900, 1);	/* one sensor */
	t32_msa_w32(msca_def, w);
	t32_msa_w32(msca_def + 4, h);
	memset(msca_saved, 0, sizeof(msca_saved));
	msca_az = 0;
	osd_mask_marked = 0;
	r = t32_msca_init(&msca_init_ops, &io, &msca_st, 0, p);
	osd_info = msca_st.osd_info;
	for (i = 0; i < T32_MSCA_COEFS; i++)
		osd_ch.coef[i] = t32_msa_r16(msca_ctx + 4 + 2 * i);
	osd_ch.pending = 0;
	msca_flush(&io);
	msca_live = !r;
	msca_inits++;
	spin_unlock_irqrestore(&osd_lock, fl);
	return r;
}

/*
 * A frame channel (set) of channel 0 starts: lib_tisp_msca_channel_attr_set with the S_FMT attribute (a: 13
 * words, ispcore_pad_event_handle 0x3000002), then the stream-on part of 0x3000003: tisp_msca_channel_start
 * (tisp_msca_chx_cfg_load), lib_tisp_msca_chx_stream_on, lib_tisp_api_set_msca_dma_stride(stride, 0).
 * Returns 0, or -1 without the live state (direct start).
 */
int t32_isposd_msca_chx_start(u32 set, const u32 *a, u32 stride)
{
	struct t32_ms_io io = { .shadow = 0, .w = msca_w, .n = 0, .rd = osd_rd };
	struct t32_msca_cfg_st st = {
		.c = &osd_ch, .sb = osd_sb, .az = msca_az, .osd_info = &osd_info, .w302 = &osd_w302,
	};
	u8 attr[52];
	unsigned long fl;
	unsigned int i;

	if (set >= 3)	/* sets of channel 0 */
		return -1;
	for (i = 0; i < 13; i++)
		t32_msa_w32(attr + 4 * i, a[i]);
	spin_lock_irqsave(&osd_lock, fl);
	if (!msca_live) {
		spin_unlock_irqrestore(&osd_lock, fl);
		return -1;
	}
	t32_msca_channel_attr_set(osd_sb, msca_saved, msca_def, msca_az, set, attr);
	osd_w302 = osd_ch.coef[T32_MSCA_COEFS - 1];
	t32_msca_chx_cfg_load(&osd_cfg_ops, &io, &st, 0, set);
	msca_flush(&io);
	msca_az = st.az;
	t32_msca_chx_stream_on(&io, osd_sb, set);
	msca_flush(&io);
	t32_msca_set_dma_stride(&io, 0, set, stride, 0);
	msca_flush(&io);
	msca_starts++;
	spin_unlock_irqrestore(&osd_lock, fl);
	return 0;
}

/*
 * openimp #721: tisp_s_autozoom_control -> tisp_msca_api_set_autozoom (vendor 0xc16a8 / 0x96284) on the live
 * parameter block: per set the window (or the saved front crop, or the sensor size) and the autozoom flag;
 * the next MSCA frame done applies it (tisp_msca_cfg_shd step 3: para_calc, curve_calc, init_chx_cfg of
 * all three sets, curves pending).  tisp_g_autozoom_control -> tisp_msca_api_get_autozoom.  buf: 15 words.
 * Return -1 without the live state (the caller keeps its stored copy then).
 */
int t32_isposd_autozoom_set(u32 vi, const void *buf)
{
	unsigned long fl;

	if (vi != 0)
		return -1;
	spin_lock_irqsave(&osd_lock, fl);
	if (!msca_live) {
		spin_unlock_irqrestore(&osd_lock, fl);
		return -1;
	}
	t32_msca_api_set_autozoom(osd_sb, buf, msca_saved, msca_def, &msca_az);
	spin_unlock_irqrestore(&osd_lock, fl);
	return 0;
}

int t32_isposd_autozoom_get(u32 vi, void *buf)
{
	unsigned long fl;

	if (vi != 0)
		return -1;
	spin_lock_irqsave(&osd_lock, fl);
	if (!msca_live) {
		spin_unlock_irqrestore(&osd_lock, fl);
		return -1;
	}
	t32_msca_api_get_autozoom(osd_sb, buf);
	spin_unlock_irqrestore(&osd_lock, fl);
	return 0;
}

/* stream off (ispcore_frame_channel_streamoff, lock half): lib_tisp_msca_chx_stream_off + _chx_reg_stream_off */
int t32_isposd_msca_chx_stop(u32 set)
{
	struct t32_ms_io io = { .shadow = 0, .w = msca_w, .n = 0, .rd = osd_rd };
	unsigned long fl;

	if (set >= 3)	/* sets of channel 0 */
		return -1;
	spin_lock_irqsave(&osd_lock, fl);
	if (!msca_live) {
		spin_unlock_irqrestore(&osd_lock, fl);
		return -1;
	}
	t32_msca_chx_stream_off(&io, osd_sb, set);
	msca_flush(&io);
	t32_msca_chx_reg_stream_off(&io, set);
	msca_flush(&io);
	msca_stops++;
	spin_unlock_irqrestore(&osd_lock, fl);
	return 0;
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

/* /proc: the live MSCA state (#730) */
void t32_isposd_msca_status(u32 *live, u32 *inits, u32 *starts, u32 *stops)
{
	*live = msca_live;
	*inits = msca_inits;
	*starts = msca_starts;
	*stops = msca_stops;
}
