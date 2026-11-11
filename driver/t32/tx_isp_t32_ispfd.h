/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP core glue, part 17: ispcore_channel_framedone_process.isra.16 (0xb7598), the frame-done
 * step of one MSCA pipe / channel (priv = core private block, idx = 3 * pipe + ch).
 * Counts the frame (priv + 848 + 4 * idx), reads the finished buffer from the MSCA address FIFO
 * and works on frame channel idx (284 B at priv + 1132):
 *  - sensor attribute mode 1 (priv + 1196): in drop mode (bss d9014 == 1 and the channel's
 *    frame counter +116 == 0) the buffer is given back to the FIFO (0x80000000 / 0x81000000, slot
 *    +120) and the bss counter f3348 counts it; otherwise the first queued buffer (list at +220)
 *    is copied as the 52 B event block, gets the fps / CSC output / full range, is moved to the
 *    done list (+228) and the block is sent to the channel's remote pad (+132, event 0x3000006).
 *    An empty queue only prints (line 869).
 *  - other modes: when the FIFO address matches the first queued buffer (its word +8), that
 *    buffer goes to the drop list (+212) on a dropped frame, else to the done list (+228); a frame
 *    that is not dropped is reported with the event block (FIFO words at +8 / +12).
 * A frame is dropped by the frame-rate pattern d9014: 1 = every frame but the first, 2 = frames
 * 0 and 3, 3 = frames 0, 3, 6 below 7, 4 = frames 0, 3, 6, 9 below 10 (counter +116).
 *
 * Vendor behaviour kept: in the other modes the event block outside the FIFO words and the
 * fps / CSC fields is uninitialised stack (zero here); the list moves leave the LIST_POISON
 * values 256 / 512 for a moment as the kernel's list_move_tail does.
 *
 * Checked against the vendor function (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_ispfd_oracle).
 */
#ifndef TX_ISP_T32_ISPFD_H
#define TX_ISP_T32_ISPFD_H

#include "tx_isp_t32_isppm.h"

#define T32_FD_COUNT		848	/* 12 frame counters */
#define T32_FD_FCH		1132
#define T32_FD_FCH_SIZE		284
#define T32_FD_MODE		1196
#define T32_FC_FRAMES		116
#define T32_FC_SLOT		120	/* u8 */
#define T32_FC_REMOTE		132
#define T32_FC_DROPQ		212
#define T32_FC_QUEUE		220
#define T32_FC_DONEQ		228
#define T32_FD_EVENT		0x03000006U
#define T32_FD_EV_SIZE		52

struct t32_fd_ops {
	void (*fifo_read)(u32 pipe, u32 ch, u8 *addr, u8 *info);	/* tisp_msca_addr_fifo_read */
	void (*fifo_write)(u32 pipe, u32 slot, u32 a, u32 b);		/* tisp_msca_addr_fifo_write */
	void (*get_fps)(u32 pipe, u8 *out);				/* tisp_get_fps */
	u32 (*csccr_output)(u32 pipe);					/* lib_tisp_api_get_csccr_output */
	u32 (*csc_full_range)(u32 pipe);				/* tisp_csc_get_full_range */
	void (*send_event)(u32 pad, u32 event, u8 *ev);			/* tx_isp_send_event_to_remote */
	void (*empty)(void);						/* printk(fmt, func, 869) */
	u32 *drop_mode;							/* bss d9014 */
	u32 *dropped;							/* bss f3348 */
};

/* frame dropped by the frame-rate pattern */
static inline int t32_fd_dropped(u32 mode, u32 n)
{
	switch (mode) {
	case 1:
		return n == 0;
	case 2:
		return n == 0 || n == 3;
	case 3:
		return n < 7 && ((0x49U >> n) & 1);
	case 4:
		return n < 10 && ((0x249U >> n) & 1);
	}
	return 0;
}

/* list_move_tail(e, head) */
static inline void t32_fd_move_tail(u32 e, u32 head)
{
	u32 next = t32_ic_r32(e, 0), prev = t32_ic_r32(e, 4), tail;

	t32_ic_w32(next, 4, prev);
	t32_ic_w32(prev, 0, next);
	t32_ic_w32(e, 0, 256);
	t32_ic_w32(e, 4, 512);
	tail = t32_ic_r32(head, 4);
	t32_ic_w32(head, 4, e);
	t32_ic_w32(e, 0, head);
	t32_ic_w32(e, 4, tail);
	t32_ic_w32(tail, 0, e);
}

static inline void t32_fd_fill(const struct t32_fd_ops *ops, u32 pipe, u8 *ev)
{
	u32 v;

	ops->get_fps(pipe, ev + 24);
	v = ops->csccr_output(pipe);
	memcpy(ev + 28, &v, 4);
	v = ops->csc_full_range(pipe);
	memcpy(ev + 48, &v, 4);
}

static inline int t32_ispcore_channel_framedone_process(const struct t32_fd_ops *ops, u32 priv, u32 pipe, u32 ch)
{
	u32 idx = 3 * pipe + ch, fc, q, e, addr;
	u8 ev[T32_FD_EV_SIZE] = { 0 };

	t32_ic_w32(priv, T32_FD_COUNT + 4 * idx, t32_ic_r32(priv, T32_FD_COUNT + 4 * idx) + 1);
	fc = t32_ic_r32(priv, T32_FD_FCH) + idx * T32_FD_FCH_SIZE;
	pipe &= 0xff;
	ops->fifo_read(pipe, ch & 0xff, ev + 8, ev + 12);
	q = fc + T32_FC_QUEUE;
	if (t32_ic_r32(priv, T32_FD_MODE) == 1) {
		if (*ops->drop_mode == 1 && !t32_ic_r32(fc, T32_FC_FRAMES)) {
			ops->fifo_write(0, *T32_IC_P(fc + T32_FC_SLOT), 0x80000000U, 0x81000000U);
			(*ops->dropped)++;
			return 0;
		}
		e = t32_ic_r32(fc, T32_FC_QUEUE);
		if (e == q || !e) {
			ops->empty();
			return 0;
		}
		memcpy(ev, T32_IC_P(e), T32_FD_EV_SIZE);
		t32_fd_fill(ops, pipe, ev);
		t32_fd_move_tail(e, fc + T32_FC_DONEQ);
		ops->send_event(t32_ic_r32(fc, T32_FC_REMOTE), T32_FD_EVENT, ev);
		return 0;
	}
	e = t32_ic_r32(fc, T32_FC_QUEUE);
	if (e == q || !e)
		return 0;
	memcpy(&addr, ev + 8, 4);
	if (t32_ic_r32(e, 8) == addr)
		t32_fd_move_tail(e, fc + (t32_fd_dropped(*ops->drop_mode, t32_ic_r32(fc, T32_FC_FRAMES)) ?
					  T32_FC_DROPQ : T32_FC_DONEQ));
	if (t32_fd_dropped(*ops->drop_mode, t32_ic_r32(fc, T32_FC_FRAMES)))
		return 0;
	t32_fd_fill(ops, pipe, ev);
	ops->send_event(t32_ic_r32(fc, T32_FC_REMOTE), T32_FD_EVENT, ev);
	return 0;
}

#endif /* TX_ISP_T32_ISPFD_H */
