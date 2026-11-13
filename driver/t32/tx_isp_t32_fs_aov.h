/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 frame-source AOV (always-on video) prepare / resume from the vendor
 * module (Matt Davis' recovered C, PR #15): fs_prepare_module (0x140b4) and
 * fs_resume_module (0x11dfc).
 *
 * fs device and channel layout: tx_isp_t32_fs_pm.h / tx_isp_t32_fch.h; the
 * channel also holds its vb2 buffer pointers +0x114 + 4k and the buffer count
 * +0x214.  Module tables: per channel saved buffers (t32_data_d9954, 140 bytes
 * each) and saved count (t32_data_d9c04).
 *
 * prepare: every streaming channel (4) saves its buffers, is stopped with a
 * kernel argument (flag +0x328 = 2), its buffers are released (reqbufs count
 * 0), its completion drained, and it is parked (5).  resume: every parked
 * channel goes back to 3, gets its format again, count buffers (memory 2),
 * queues the saved buffers and starts streaming.  The frame channel calls and
 * try_wait_for_completion are hooks of struct t32_fs_aov_os.
 *
 * Vendor quirks kept: the channel counter is 8 bits (more than 255 channels
 * would loop forever); prepare always prints its entry line first, and its
 * stream-off / reqbufs failures only print; resume adds up the results of
 * set_fmt, reqbufs and every qbuf, so an earlier failure aborts at the first
 * qbuf (-22), and the stream-on failure (sum including stream-on) only prints.
 *
 * Checked bit-exact against the vendor flow by tests/t32_oracle
 * (t32_fsaov_oracle).
 */
#ifndef TX_ISP_T32_FS_AOV_H
#define TX_ISP_T32_FS_AOV_H

#include "tx_isp_t32_fch.h"

#define T32_FCH_VBUFS		0x114U
#define T32_FCH_NBUFS		0x214U
#define T32_FCH_ST_PARKED	5U
#define T32_FCH_KARG_AOV	2U
#define T32_VB2_BUF_BYTES	140U
#define T32_REQBUFS_BYTES	20U

struct t32_fs_aov_os {
	int (*streamoff)(u8 *ch, u8 *type);		/* frame_channel_vb2_streamoff */
	int (*streamon)(u8 *ch, u8 *type);		/* frame_channel_vb2_streamon */
	int (*reqbufs)(u8 *ch, u8 *req);		/* frame_channel_reqbufs */
	int (*set_fmt)(u8 *ch, u8 *fmt);		/* frame_channel_vidioc_set_fmt */
	int (*qbuf)(u8 *ch, u32 buf);			/* frame_channel_vb2_qbuf */
	int (*try_wait)(u8 *c);				/* try_wait_for_completion */
};

struct t32_fs_aov_tabs {
	u32 *bufs;	/* t32_data_d9954[ch] */
	u32 *nbufs;	/* t32_data_d9c04[ch] */
};

static inline int t32_fs_prepare_module(const struct t32_fs_aov_os *os, const struct t32_fs_aov_tabs *tb, u8 *fs)
{
	static const char fn[] = "fs_prepare_module";
	u8 i;

	isp_printf(0, "[%s %d] Enter the FS prepare ... \n", fn, 1885);
	if (!t32_fs_valid(fs)) {
		isp_printf(2, "[%s %d] AOV fs is NULL\n", fn, 1887);
		return -22;
	}
	if ((int)t32_g_rd32(fs, T32_FS_STATE) < 2)
		return 0;
	for (i = 0; (int)i < (int)t32_g_rd32(fs, T32_FS_NCHAN); i++) {
		u8 *ch = t32_fs_chan(fs, i), req[T32_REQBUFS_BYTES];
		u32 bufs = tb->bufs[i], k, type = 1;

		if (t32_g_rd32(ch, T32_FCH_STATE) != T32_FCH_ST_STREAM)
			continue;
		if (!bufs) {
			isp_printf(2, "framesource channel memory space does not exist!\n");
			return -22;
		}
		for (k = 0; k < t32_g_rd32(ch, T32_FCH_NBUFS); k++)
			memcpy(T32_PTR(bufs + k * T32_VB2_BUF_BYTES), T32_PTR(t32_g_rd32(ch, T32_FCH_VBUFS + 4 * k)),
			       T32_VB2_BUF_BYTES);
		tb->nbufs[i] = t32_g_rd32(ch, T32_FCH_NBUFS);
		t32_g_wr32(ch, T32_FCH_KARG, T32_FCH_KARG_AOV);
		if (os->streamoff(ch, (u8 *)&type))
			isp_printf(1, "[%s %d] stream off is error\n", fn, 1920);
		memset(req, 0, sizeof(req));
		t32_g_wr32(req, 4, 1);		/* type */
		t32_g_wr32(req, 8, 2);		/* memory */
		if (os->reqbufs(ch, req))
			isp_printf(1, "[%s %d] reqbufs is error\n", fn, 1928);
		while (os->try_wait(ch + T32_FCH_DONE))
			;
		t32_g_wr32(ch, T32_FCH_STATE, T32_FCH_ST_PARKED);
	}
	return 0;
}

static inline int t32_fs_resume_module(const struct t32_fs_aov_os *os, const struct t32_fs_aov_tabs *tb, u8 *fs)
{
	static const char fn[] = "fs_resume_module";
	u8 i;

	if (!t32_fs_valid(fs)) {
		isp_printf(2, "[%s %d] AOV fs is NULL\n", fn, 1973);
		return -22;
	}
	if ((int)t32_g_rd32(fs, T32_FS_STATE) < 2) {
		isp_printf(2, "[%s %d] fs state is error\n", fn, 1978);
		return -22;
	}
	for (i = 0; (int)i < (int)t32_g_rd32(fs, T32_FS_NCHAN); i++) {
		u8 *ch = t32_fs_chan(fs, i), fmt[T32_FCH_FMT_BYTES], req[T32_REQBUFS_BYTES];
		u32 type = 1, bufs, r;
		int k;

		if (t32_g_rd32(ch, T32_FCH_STATE) != T32_FCH_ST_PARKED)
			continue;
		t32_g_wr32(ch, T32_FCH_STATE, T32_FCH_ST_OPEN);
		memcpy(fmt, ch + T32_FCH_FMT, sizeof(fmt));
		t32_g_wr32(ch, T32_FCH_KARG, T32_FCH_KARG_AOV);
		r = (u32)os->set_fmt(ch, fmt);
		memset(req, 0, sizeof(req));
		t32_g_wr32(req, 0, tb->nbufs[i]);	/* count */
		t32_g_wr32(req, 4, 1);
		t32_g_wr32(req, 8, 2);
		r += (u32)os->reqbufs(ch, req);
		bufs = tb->bufs[i];
		for (k = 0; k < (int)tb->nbufs[i]; k++) {
			r += (u32)os->qbuf(ch, bufs + (u32)k * T32_VB2_BUF_BYTES);
			if (r) {
				isp_printf(1, "[%s %d] qbufs is error\n", fn, 2005);
				return -22;
			}
		}
		if (r + (u32)os->streamon(ch, (u8 *)&type))
			isp_printf(1, "isp frame channel, complete failed\n");
	}
	return 0;
}

#endif
