/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, blocks "chfd" / "chfdx": the channel frame done (driver/t32/tx_isp_t33_ispfd.h) with the
 * vendor calling convention, callees and bss counters, against ispcore_channel_framedone_process.isra.13.
 */
#include "tx_isp_t33_ispfd.h"

extern void lib_tisp_msca_addr_fifo_read(unsigned int pipe, unsigned int ch, unsigned char *y, unsigned char *uv);
extern void lib_tisp_msca_addr_fifo_write(unsigned int pipe, unsigned int slot, unsigned int y, unsigned int uv);
extern void tisp_get_fps(unsigned int pipe, unsigned char *out);
extern unsigned int lib_tisp_api_get_csccr_output(unsigned int pipe);
extern unsigned int tisp_csc_get_full_range(unsigned int pipe);
extern void tx_isp_send_event_to_remote(unsigned int pad, unsigned int event, unsigned char *ev);
extern int printk(const char *fmt, ...);
unsigned int direct_mode, irq_count, frame_chan_done_cnt[12], frame_chan_dq_cnt[12];

static void bo_empty(void) { printk("[CORE][%s, %d]: bbuf is null \n", "ispcore_channel_framedone_process", 929); }

static const struct t33_fd_ops bo_ops = {
	.fifo_read = lib_tisp_msca_addr_fifo_read, .fifo_write = lib_tisp_msca_addr_fifo_write, .get_fps = tisp_get_fps,
	.csccr_output = lib_tisp_api_get_csccr_output, .csc_full_range = tisp_csc_get_full_range,
	.send_event = tx_isp_send_event_to_remote, .empty = bo_empty, .direct_mode = &direct_mode,
	.irq_count = &irq_count, .done_cnt = frame_chan_done_cnt, .dq_cnt = frame_chan_dq_cnt,
};

int bo_chfd(unsigned char *core, unsigned int pipe, unsigned int ch)
{
	return t33_ispcore_channel_framedone_process(&bo_ops, core, pipe, ch);
}
