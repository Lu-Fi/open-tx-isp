/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 frame done of one MSCA pipe / channel: vendor ispcore_channel_framedone_process.isra.13 (0xfe680),
 * called by the ISP interrupt for every channel done bit (core = ISP core private block, idx = 3 * pipe + ch).
 *
 * Counts the frame (core +844 + 4 idx), reads the finished Y / UV addresses from the MSCA address FIFO
 * (lib_tisp_msca_addr_fifo_read, event block +8 / +12) and works on frame channel idx (296 B at core +1128):
 *  - core +1192 == 1 (one buffer per frame): in direct mode 1 the channel +116 == 0 (channel 0 goes to the
 *    IVDC) gives the buffer back to the FIFO (0x80000000 / 0x81000000, slot u8 +120) and counts irq_count;
 *    otherwise the first queued buffer (list +232) is copied as the 52 B event block, gets fps (+24), CSC
 *    output (+28) and full range (+48), moves to the done list (+240) and the block goes to the channel's
 *    remote pad (+132, event 0x3000006).  An empty queue only prints (line 929).
 *  - else: frame_chan_done_cnt[chn]++ (chn = +116); with a queued buffer frame_chan_dq_cnt[chn]++, and when
 *    the FIFO address equals the buffer's +8 it moves to the IVDC list (+224, the direct-mode channels:
 *    mode 1 chn 0, 2: 0 / 3, 3: 0 / 3 / 6, 4: 0 / 3 / 6 / 9) or to the done list (+240); a channel that is
 *    not a direct-mode channel then sends the event block.
 * Same flow as the T32 (tx_isp_t32_ispfd.h) with the T33 offsets and the two per-channel counters.
 *
 * Vendor behaviour kept: outside mode 1 the event block words other than +8 / +12 / +24 / +28 / +48 are
 * uninitialised stack in the vendor module (zero here); list moves leave LIST_POISON 0x100100 / 0x200200 for a
 * moment as list_move_tail does.
 *
 * Our frame channels keep their own queues (tx_isp_t32_fs.c), so this port is not wired; it is checked against
 * the vendor instructions by tests/t33_binoracle (blocks chfd / chfdx, openimp #99).
 */
#ifndef TX_ISP_T33_ISPFD_H
#define TX_ISP_T33_ISPFD_H

#define T33_FD_COUNT		844U	/* 12 frame counters */
#define T33_FD_FCH		1128U	/* frame channel array pointer */
#define T33_FD_FCH_SIZE		296U
#define T33_FD_MODE		1192U
#define T33_FC_CHN		116U
#define T33_FC_SLOT		120U	/* u8 */
#define T33_FC_REMOTE		132U
#define T33_FC_IVDCQ		224U
#define T33_FC_QUEUE		232U
#define T33_FC_DONEQ		240U
#define T33_FD_EVENT		0x03000006U
#define T33_FD_EV_SIZE		52U

struct t33_fd_ops {
	void (*fifo_read)(unsigned int pipe, unsigned int ch, unsigned char *y, unsigned char *uv);	/* lib_tisp_msca_addr_fifo_read */
	void (*fifo_write)(unsigned int pipe, unsigned int slot, unsigned int y, unsigned int uv);	/* lib_tisp_msca_addr_fifo_write */
	void (*get_fps)(unsigned int pipe, unsigned char *out);					/* tisp_get_fps */
	unsigned int (*csccr_output)(unsigned int pipe);					/* lib_tisp_api_get_csccr_output */
	unsigned int (*csc_full_range)(unsigned int pipe);					/* tisp_csc_get_full_range */
	void (*send_event)(unsigned int pad, unsigned int event, unsigned char *ev);		/* tx_isp_send_event_to_remote */
	void (*empty)(void);									/* printk(fmt, func, 929) */
	unsigned int *direct_mode, *irq_count, *done_cnt, *dq_cnt;
};

#define T33_FD_W(p, o)		(*(unsigned int *)((unsigned char *)(p) + (o)))

/* the direct-mode channels (their buffers go to the IVDC, no event) */
static inline int t33_fd_direct(unsigned int mode, unsigned int n)
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
static inline void t33_fd_move_tail(unsigned char *e, unsigned char *head)
{
	unsigned char *next = (unsigned char *)T33_FD_W(e, 0), *prev = (unsigned char *)T33_FD_W(e, 4), *tail;

	T33_FD_W(next, 4) = (unsigned int)prev;
	T33_FD_W(prev, 0) = (unsigned int)next;
	T33_FD_W(e, 0) = 0x100100;
	T33_FD_W(e, 4) = 0x200200;
	tail = (unsigned char *)T33_FD_W(head, 4);
	T33_FD_W(head, 4) = (unsigned int)e;
	T33_FD_W(e, 0) = (unsigned int)head;
	T33_FD_W(e, 4) = (unsigned int)tail;
	T33_FD_W(tail, 0) = (unsigned int)e;
}

static inline void t33_fd_fill(const struct t33_fd_ops *ops, unsigned int pipe, unsigned char *ev)
{
	ops->get_fps(pipe, ev + 24);
	T33_FD_W(ev, 28) = ops->csccr_output(pipe);
	T33_FD_W(ev, 48) = ops->csc_full_range(pipe);
}

static inline int t33_ispcore_channel_framedone_process(const struct t33_fd_ops *ops, unsigned char *core,
							unsigned int pipe, unsigned int ch)
{
	unsigned int idx = 3 * pipe + ch, chn;
	unsigned char *fc, *e;
	unsigned int ev[T33_FD_EV_SIZE / 4] = { 0 };
	unsigned char *b = (unsigned char *)ev;

	T33_FD_W(core, T33_FD_COUNT + 4 * idx)++;
	fc = (unsigned char *)T33_FD_W(core, T33_FD_FCH) + idx * T33_FD_FCH_SIZE;
	pipe &= 0xff;
	ops->fifo_read(pipe, ch & 0xff, b + 8, b + 12);
	if (T33_FD_W(core, T33_FD_MODE) == 1) {
		if (*ops->direct_mode == 1 && !T33_FD_W(fc, T33_FC_CHN)) {
			ops->fifo_write(0, fc[T33_FC_SLOT], 0x80000000U, 0x81000000U);
			(*ops->irq_count)++;
			return 0;
		}
		e = (unsigned char *)T33_FD_W(fc, T33_FC_QUEUE);
		if (e == fc + T33_FC_QUEUE || !e) {
			ops->empty();
			return 0;
		}
		__builtin_memcpy(b, e, T33_FD_EV_SIZE);
		t33_fd_fill(ops, pipe, b);
		t33_fd_move_tail(e, fc + T33_FC_DONEQ);
		ops->send_event(T33_FD_W(fc, T33_FC_REMOTE), T33_FD_EVENT, b);
		return 0;
	}
	e = (unsigned char *)T33_FD_W(fc, T33_FC_QUEUE);
	if (e == fc + T33_FC_QUEUE)
		e = 0;
	chn = T33_FD_W(fc, T33_FC_CHN);
	ops->done_cnt[chn]++;
	if (!e)
		return 0;
	ops->dq_cnt[chn]++;
	if (T33_FD_W(e, 8) == ev[2])
		t33_fd_move_tail(e, fc + (t33_fd_direct(*ops->direct_mode, chn) ? T33_FC_IVDCQ : T33_FC_DONEQ));
	if (t33_fd_direct(*ops->direct_mode, T33_FD_W(fc, T33_FC_CHN)))
		return 0;
	t33_fd_fill(ops, pipe, b);
	ops->send_event(T33_FD_W(fc, T33_FC_REMOTE), T33_FD_EVENT, b);
	return 0;
}

#endif /* TX_ISP_T33_ISPFD_H */
