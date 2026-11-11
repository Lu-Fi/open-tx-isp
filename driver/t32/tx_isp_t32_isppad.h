/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP core glue, part 23: ispcore_pad_event_handle (0xb8670), the events a frame channel pad
 * gets from the frame source (pad + 5 = pad active, + 7 = state, + 20 = flags, + 32 = frame
 * channel fc, pad + 0 = subdev, subdev + 280 = core private block).  Events 0x3000001..8:
 *  1 get format: the channel format (116 B) is copied out; in sensor channel mode
 *    (fc + 136 -> + 1148 == 1) only width / height / image size and the crop / scaler flags off.
 *  2 set format: prints the request, looks up the pixel format in the table d8ce0 (8 entries,
 *    44 B: +32 format, +36 bits per pixel) for the line / image size and the channel stride
 *    (fc + 200), builds the 52 B MSCA channel attribute (crop, scaler or the pipe's default size
 *    priv + 384 / 388 + 104 * pipe) and stores the format.
 *  3 stream on (state 3 -> 4, fc + 128 = 4): channel start, stream on and DMA stride; outside
 *    mode 1 the first buffer of the ready list (+212) is armed (or the channel disabled on a
 *    dropped frame) and the ISP is started once (priv + 4012).
 *  4 stream off: ispcore_frame_channel_streamoff.
 *  5 queue buffer: the buffer (event block, state byte at -36, count at -32) takes the first
 *    free entry (+228); mode 1 moves it to the ready list, tags dropped frames (d9014 pattern)
 *    with the pipe / channel and arms the whole ready list; with the pre-buffer mode (d9008)
 *    the slot 0 / 1 extra buffers are written once their count is reached.
 *  7 request buffers: count = n + 1, lists reset, n + 1 entries of 52 B allocated as free list.
 *  8 free buffers: lists reset, entries freed.
 *
 * Vendor behaviour kept: event 5 returns -1 with the channel lock still held when fc + 12 >= 2;
 * event 3 reads word 40 of the first ready (other modes) / queued (mode 1) buffer also when the
 * list is empty (address 40), event 5 outside mode 1 copies into an empty free list (address 8);
 * event 7 does not check the allocation; "mode" is read from a NULL private block when the
 * subdev is missing (events 3 / 5).
 *
 * Checked against the vendor function (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_isppad_oracle).
 */
#ifndef TX_ISP_T32_ISPPAD_H
#define TX_ISP_T32_ISPPAD_H

#include "tx_isp_t32_ispisr.h"

#define T32_PE_ACTIVE		5	/* u8 */
#define T32_PE_STATE		7	/* u8 */
#define T32_PE_FLAGS		20
#define T32_PE_FC		32
#define T32_FC_W		4
#define T32_FC_H		8
#define T32_FC_NBUF		12
#define T32_FC_STREAM		128
#define T32_FC_SRC		136
#define T32_FC_LOCK		168
#define T32_FC_STRIDE		200
#define T32_FC_COUNT		207	/* u8 */
#define T32_FC_FREEQ		228
#define T32_FC_POOL		236
#define T32_FC_DONE		240
#define T32_FC_FMT_SIZE		116
#define T32_PE_FMT_N		8
#define T32_PE_FMT_SIZE		44
#define T32_IS_STARTED		4012

/* isp_printf formats (index into t32_pe_glob.fmt) */
enum { T32_PE_P_REQ, T32_PE_P_CROP, T32_PE_P_SCL, T32_PE_P_FCROP, T32_PE_P_NOFMT, T32_PE_P_NOBUF,
       T32_PE_P_BUSY, T32_PE_P_NOFREE, T32_PE_P_NULL, T32_PE_P_N };

struct t32_pe_glob {
	u32 *drop_mode;		/* d9014 */
	u32 *prebuf;		/* d9008 */
	u32 *npipe;		/* f3354 */
	u32 *cnt[2], *need[2], *size[2], *nbuf[2], *base[2];	/* slot 0: f3340 f3774 f3750 f3808 f3810, slot 1: f333c f3778 f374c f3804 f380c */
	u32 fmts;		/* d8ce0 */
	u32 jt;			/* c312c, event jump table */
	u32 fmt[T32_PE_P_N];	/* format strings */
	u32 func[3];		/* function names c3370 (event 2), c3354 (format), c3338 (event 5) */
};

struct t32_pe_ops {
	void (*print)(u32 level, u32 fmt, u32 a2, u32 a3, u32 s16, u32 s20, u32 s24);	/* isp_printf */
	void (*lock)(u32 lock, u32 *flags);			/* __private_spin_lock_irqsave */
	void (*unlock)(u32 lock, u32 flags);			/* private_spin_unlock_irqrestore */
	void (*init_completion)(u32 c);
	void (*attr_set)(u32 pipe, u32 *attr);			/* lib_tisp_msca_channel_attr_set, 52 B */
	void (*channel_start)(u32 n);				/* tisp_msca_channel_start */
	void (*chx_stream_on)(u32 n);				/* lib_tisp_msca_chx_stream_on */
	void (*dma_stride)(u32 pipe, u32 ch, u32 v, u32 z);	/* lib_tisp_api_set_msca_dma_stride */
	void (*chx_en)(u32 pipe, u32 ch, u32 en);		/* tisp_msca_chx_en */
	void (*fifo_write)(u32 pipe, u32 slot, u32 a, u32 b);	/* tisp_msca_addr_fifo_write */
	u32 (*get_frame_drop)(u32 fc);				/* __ispcore_get_frame_drop */
	void (*stream_on)(void);				/* lib_tisp_stream_on */
	u32 (*streamoff)(u32 pad, u32 a1, u32 a2, u32 a3);	/* ispcore_frame_channel_streamoff */
	u32 (*kmalloc)(u32 size, u32 flags);
	void (*kfree)(u32 p);
	const struct t32_pe_glob *g;
};

static inline int t32_pe_valid(u32 p)
{
	return p && p < 0xfffff001U;
}

static inline u32 t32_pe_priv(u32 pad)
{
	u32 sd = t32_ic_r32(pad, 0);

	return t32_pe_valid(sd) ? t32_ic_r32(sd, T32_IS_PRIV) : 0;
}

static inline void t32_pe_list_init(u32 h)
{
	t32_ic_w32(h, 0, h);
	t32_ic_w32(h, 4, h);
}

static inline void t32_pe_lists_init(u32 fc)
{
	t32_pe_list_init(fc + T32_FC_DROPQ);
	t32_pe_list_init(fc + T32_FC_QUEUE);
	t32_pe_list_init(fc + T32_FC_FREEQ);
}

/* event 1: get format */
static inline int t32_pe_get_fmt(u32 pad, u32 data)
{
	u32 fc = t32_ic_r32(pad, T32_PE_FC);

	if (!data || !fc)
		return 0;
	if (t32_ic_r32(t32_ic_r32(fc, T32_FC_SRC), 1148) == 1) {
		t32_ic_w32(data, 4, t32_ic_r32(fc, 4));
		t32_ic_w32(data, 8, t32_ic_r32(fc, 8));
		*T32_IC_P(data + 52) = 0;
		*T32_IC_P(data + 72) = 0;
		t32_ic_w32(data, 24, t32_ic_r32(fc, 24));
	} else {
		memcpy(T32_IC_P(data), T32_IC_P(fc), T32_FC_FMT_SIZE);
		t32_ic_w32(data, 24, 0);
	}
	return 0;
}

/* event 2: set format */
static inline int t32_pe_set_fmt(const struct t32_pe_ops *ops, u32 pad, u32 data)
{
	const struct t32_pe_glob *g = ops->g;
	u32 sd, priv, fc, dw, dh, fmt, i, e, w, bpp, a[13] = { 0 };

	if (!t32_pe_valid(pad))
		return -22;
	sd = t32_ic_r32(pad, 0);
	if (!t32_pe_valid(sd))
		return -22;
	priv = t32_ic_r32(sd, T32_IS_PRIV);
	if (!t32_pe_valid(priv))
		return -22;
	fc = t32_ic_r32(pad, T32_PE_FC);
	i = (u32)((int)t32_ic_r32(fc, T32_FC_FRAMES) / 3) * 104;
	dw = t32_ic_r32(priv + i, 384);
	dh = t32_ic_r32(priv + i, 388);
	ops->print(0, g->fmt[T32_PE_P_REQ], g->func[0], 1949, t32_ic_r32(fc, 4), t32_ic_r32(fc, 8), 0);
	ops->print(0, g->fmt[T32_PE_P_CROP], *T32_IC_P(data + 72), t32_ic_r32(data, 76), t32_ic_r32(data, 80), 0, 0);
	ops->print(0, g->fmt[T32_PE_P_FCROP], *T32_IC_P(data + 52), t32_ic_r32(data, 56), t32_ic_r32(data, 60),
		   t32_ic_r32(data, 64), t32_ic_r32(data, 68));
	ops->print(0, g->fmt[T32_PE_P_SCL], *T32_IC_P(data + 92), t32_ic_r32(data, 96), t32_ic_r32(data, 100),
		   t32_ic_r32(data, 104), t32_ic_r32(data, 108));
	fmt = t32_ic_r32(data, 12);
	for (i = 0; i < T32_PE_FMT_N; i++)
		if (t32_ic_r32(g->fmts + T32_PE_FMT_SIZE * i, 32) == fmt)
			break;
	if (i < T32_PE_FMT_N) {
		e = g->fmts + T32_PE_FMT_SIZE * i;
		w = t32_ic_r32(data, 4);
		bpp = t32_ic_r32(e, 36);
		t32_ic_w32(data, 20, (w * bpp) >> 3);
		t32_ic_w32(data, 24, t32_ic_r32(data, 8) * t32_ic_r32(data, 20));
		t32_ic_w32(fc, T32_FC_STRIDE, (bpp >> 3) * w);
		t32_ic_w32(data, 32, e);
	} else {
		ops->print(2, g->fmt[T32_PE_P_NOFMT], g->func[1], 1895, fmt, 0, 0);
	}
	ops->init_completion(fc + T32_FC_DONE);
	*T32_IC_P(fc + 252) = 0;

	/* MSCA channel attribute: scaler [8..12], crop [0..2], frame crop [3..7] */
	a[8] = *T32_IC_P(data + 92);
	if (a[8]) {
		a[9] = t32_ic_r32(data, 100);
		a[10] = t32_ic_r32(data, 96);
		a[11] = t32_ic_r32(data, 104);
		a[12] = t32_ic_r32(data, 108);
	} else {
		a[11] = dw;
		a[12] = dh;
	}
	a[0] = *T32_IC_P(data + 72);
	a[1] = a[0] ? t32_ic_r32(data, 76) : a[11];
	a[2] = a[0] ? t32_ic_r32(data, 80) : a[12];
	a[3] = *T32_IC_P(data + 52);
	if (a[3]) {
		a[4] = t32_ic_r32(data, 60);
		a[5] = t32_ic_r32(data, 56);
		a[6] = t32_ic_r32(data, 64);
		a[7] = t32_ic_r32(data, 68);
	} else {
		a[6] = a[1];
		a[7] = a[2];
	}
	ops->attr_set(*T32_IC_P(fc + T32_FC_FRAMES), a);
	memcpy(T32_IC_P(fc), T32_IC_P(data), T32_FC_FMT_SIZE);
	return 0;
}

/* event 3: stream on */
static inline int t32_pe_stream_on(const struct t32_pe_ops *ops, u32 pad)
{
	const struct t32_pe_glob *g = ops->g;
	u32 priv = t32_pe_priv(pad), fc = t32_ic_r32(pad, T32_PE_FC), flags = 0, n, b, h;

	if (*T32_IC_P(pad + T32_PE_STATE) != 3)
		return 0;
	ops->lock(fc + T32_FC_LOCK, &flags);
	if (t32_ic_r32(fc, T32_FC_STREAM) == 4)
		goto out;
	t32_ic_w32(fc, T32_FC_STREAM, 4);
	*T32_IC_P(pad + T32_PE_STATE) = 4;
	n = t32_ic_r32(fc, T32_FC_FRAMES);
	if (t32_ic_r32(priv, T32_FD_MODE) == 1) {
		t32_ic_w32(fc, T32_FC_STATE, 1);
		b = t32_is_first(fc + T32_FC_QUEUE);
		ops->dma_stride(((int)n / 3) & 0xff, ((int)n % 3) & 0xff, t32_ic_r32(b, 40), 0);
		ops->channel_start(n & 0xff);
		ops->chx_stream_on(n & 0xff);
		goto out;
	}
	h = fc + T32_FC_DROPQ;
	b = t32_is_first(h);
	ops->channel_start(n & 0xff);
	ops->chx_stream_on(*T32_IC_P(fc + T32_FC_FRAMES));
	n = t32_ic_r32(fc, T32_FC_FRAMES);
	ops->dma_stride(((int)n / 3) & 0xff, ((int)n % 3) & 0xff, t32_ic_r32(b, 40), 0);
	t32_ic_w32(fc, T32_FC_STATE, 1);
	if (t32_ic_r32(fc, T32_FC_DROPQ) == h || !t32_ic_r32(fc, T32_FC_DROPQ)) {
		ops->print(2, g->fmt[T32_PE_P_NOBUF], 0, 0, 0, 0, 0);
		goto out;
	}
	if (t32_ic_r32(fc, T32_FC_FRAMES) + 2 >= 5 || !*g->prebuf || t32_ic_r32(priv, T32_IS_STOP))
		goto out;
	if (ops->get_frame_drop(fc)) {
		n = t32_ic_r32(fc, T32_FC_FRAMES);
		ops->chx_en(((int)n / 3) & 0xff, ((int)n % 3) & 0xff, 0);
	} else {
		n = t32_ic_r32(fc, T32_FC_FRAMES);
		ops->fifo_write(((int)n / 3) & 0xff, *T32_IC_P(fc + T32_FC_SLOT), t32_ic_r32(b, 8), t32_ic_r32(b, 12));
		n = t32_ic_r32(fc, T32_FC_FRAMES);
		ops->chx_en(((int)n / 3) & 0xff, ((int)n % 3) & 0xff, 1);
		t32_fd_move_tail(b, fc + T32_FC_QUEUE);
	}
	if (!t32_ic_r32(priv, T32_IS_STARTED)) {
		*g->npipe = 0;
		ops->stream_on();
		t32_ic_w32(priv, T32_IS_STARTED, 1);
	}
out:
	ops->unlock(fc + T32_FC_LOCK, flags);
	return 0;
}

/* event 5, pre-buffer mode: the extra buffers of slot s once its count is reached */
static inline void t32_pe_prebuf(const struct t32_pe_ops *ops, u32 fc, u32 s, u32 sz)
{
	const struct t32_pe_glob *g = ops->g;
	u32 i, a, c = *g->cnt[s];

	if (c >= *g->need[s])
		return;
	*g->cnt[s] = ++c;
	if (c != *g->need[s])
		return;
	*g->size[s] = (((t32_ic_r32(fc, T32_FC_H) + 15) & ~15U) * (3 * t32_ic_r32(fc, T32_FC_W))) >> 1;
	for (i = 0; i < *g->nbuf[s]; i++) {
		a = i * *g->size[s] + 0x80000000U + *g->base[s];
		ops->fifo_write(((int)t32_ic_r32(fc, T32_FC_FRAMES) / 3) & 0xff, *T32_IC_P(fc + T32_FC_SLOT), a, sz + a);
	}
}

/* buffer entry e takes the frame (FIFO words +8 / +12, +40 / +44) and moves to the ready list */
static inline void t32_pe_take(u32 e, u32 data, u32 fc)
{
	t32_ic_w32(e, 8, t32_ic_r32(data, 8));
	t32_ic_w32(e, 12, t32_ic_r32(data, 12));
	t32_ic_w32(e, 40, t32_ic_r32(data, 40));
	t32_ic_w32(e, 44, t32_ic_r32(data, 44));
	t32_fd_move_tail(e, fc + T32_FC_DROPQ);
}

/* event 5: queue buffer */
static inline int t32_pe_qbuf(const struct t32_pe_ops *ops, u32 pad, u32 data)
{
	const struct t32_pe_glob *g = ops->g;
	u32 priv = t32_pe_priv(pad), fc, flags = 0, w, h, e, n, a, tag, sz;

	if (t32_ic_r32(pad, T32_PE_FLAGS) & 0x20)
		return 0;
	fc = t32_ic_r32(pad, T32_PE_FC);
	if (!data || !fc) {
		ops->print(2, g->fmt[T32_PE_P_NULL], g->func[2], 1647, data, fc, 0);
		return 0;
	}
	*T32_IC_P(data - 36) = 4;
	ops->lock(fc + T32_FC_LOCK, &flags);
	if (t32_ic_r32(fc, T32_FC_NBUF) >= 2) {
		ops->print(2, g->fmt[T32_PE_P_BUSY], g->func[2], 1540, 0, 0, 0);
		return -1;	/* vendor: lock still held */
	}
	w = t32_ic_r32(fc, T32_FC_W);
	h = t32_ic_r32(fc, T32_FC_H);
	*T32_IC_P(data - 36) = 5;
	t32_ic_w32(data, -32, t32_ic_r32(data, -32) + 1);
	if (*g->drop_mode == 1 && !t32_ic_r32(fc, T32_FC_FRAMES))
		t32_ic_w32(data, 12, t32_ic_r32(data, 8) | 0x1000000U);
	if (t32_ic_r32(priv, T32_FD_MODE) != 1) {
		e = t32_is_first(fc + T32_FC_FREEQ);
		memcpy(T32_IC_P(e + 8), T32_IC_P(data + 8), 8);
		n = t32_ic_r32(fc, T32_FC_FRAMES);
		if (t32_fd_dropped(*g->drop_mode, n)) {
			/* dropped frame: tagged with pipe % 4 (bits 25..26) and n / 12 (bits 28..) */
			a = t32_ic_r32(data, 8);
			tag = ((u32)(((int)n / 3) % 4) << 25) | ((u32)((int)n / 12) << 28);
			t32_ic_w32(data, 12, a | 0x1000000U);
			t32_ic_w32(data, 8, tag | a);
			t32_ic_w32(data, 12, tag | a | 0x1000000U);
		}
		t32_pe_take(e, data, fc);
		goto out;
	}
	e = t32_is_first(fc + T32_FC_FREEQ);
	if (e)
		t32_pe_take(e, data, fc);
	else
		ops->print(1, g->fmt[T32_PE_P_NOFREE], g->func[2], 1595, 0, 0, 0);
	if (t32_ic_r32(priv, T32_IS_STREAMOFF) != 1) {
		e = t32_is_first(fc + T32_FC_DROPQ);
		if (!e) {
			ops->chx_en(0, *T32_IC_P(fc + T32_FC_FRAMES), 0);
		} else {
			do {
				n = t32_ic_r32(fc, T32_FC_FRAMES);
				ops->fifo_write(((int)n / 3) & 0xff, *T32_IC_P(fc + T32_FC_SLOT), t32_ic_r32(e, 8), t32_ic_r32(e, 12));
				ops->chx_en(0, *T32_IC_P(fc + T32_FC_FRAMES), 1);
				t32_fd_move_tail(e, fc + T32_FC_QUEUE);
				e = t32_is_first(fc + T32_FC_DROPQ);
			} while (e);
		}
	}
	if (!*g->prebuf)
		goto out;
	sz = ((w + 15) & ~15U) * ((h + 15) & ~15U);
	if (t32_ic_r32(fc, T32_FC_SLOT) <= 1)
		t32_pe_prebuf(ops, fc, t32_ic_r32(fc, T32_FC_SLOT), sz);
out:
	ops->unlock(fc + T32_FC_LOCK, flags);
	return 0;
}

/* event 7: request buffers */
static inline int t32_pe_reqbufs(const struct t32_pe_ops *ops, u32 pad, u32 data)
{
	u32 fc = t32_ic_r32(pad, T32_PE_FC), flags = 0, head, e, tail;
	int i;

	if ((t32_ic_r32(pad, T32_PE_FLAGS) & 0x20) || !fc)
		return 0;
	ops->lock(fc + T32_FC_LOCK, &flags);
	*T32_IC_P(fc + T32_FC_COUNT) = t32_ic_r32(data, 0) + 1;
	t32_pe_lists_init(fc);
	ops->unlock(fc + T32_FC_LOCK, flags);
	t32_ic_w32(fc, T32_FC_POOL, ops->kmalloc(*T32_IC_P(fc + T32_FC_COUNT) * 52, 0x2080020U));
	head = fc + T32_FC_FREEQ;
	for (i = 0; i < *T32_IC_P(fc + T32_FC_COUNT); i++) {
		e = t32_ic_r32(fc, T32_FC_POOL) + 52 * i;
		t32_pe_list_init(e);
		tail = t32_ic_r32(head, 4);
		t32_ic_w32(head, 4, e);
		t32_ic_w32(e, 0, head);
		t32_ic_w32(e, 4, tail);
		t32_ic_w32(tail, 0, e);
	}
	if (!t32_ic_r32(fc, 260)) {
		t32_ic_w32(fc, 264, 0);
		t32_ic_w32(fc, 268, 0xffffffffU);
		t32_ic_w32(fc, 272, 0);
	}
	return 0;
}

/* event 8: free buffers */
static inline int t32_pe_freebufs(const struct t32_pe_ops *ops, u32 pad)
{
	u32 fc = t32_ic_r32(pad, T32_PE_FC), flags = 0;

	if ((t32_ic_r32(pad, T32_PE_FLAGS) & 0x20) || !fc)
		return 0;
	ops->lock(fc + T32_FC_LOCK, &flags);
	if (t32_ic_r32(fc, T32_FC_POOL)) {
		t32_pe_lists_init(fc);
		ops->kfree(t32_ic_r32(fc, T32_FC_POOL));
		t32_ic_w32(fc, T32_FC_POOL, 0);
	}
	ops->unlock(fc + T32_FC_LOCK, flags);
	return 0;
}

static inline int t32_ispcore_pad_event_handle(const struct t32_pe_ops *ops, u32 pad, u32 event, u32 data, u32 a3)
{
	if (!*T32_IC_P(pad + T32_PE_ACTIVE))
		return 0;
	switch (event) {
	case 0x3000001:
		return t32_pe_get_fmt(pad, data);
	case 0x3000002:
		return t32_pe_set_fmt(ops, pad, data);
	case 0x3000003:
		return t32_pe_stream_on(ops, pad);
	case 0x3000004:
		return (int)ops->streamoff(pad, ops->g->jt + 12, data, a3);	/* vendor: a1 = its jump table entry */
	case 0x3000005:
		return t32_pe_qbuf(ops, pad, data);
	case 0x3000007:
		return t32_pe_reqbufs(ops, pad, data);
	case 0x3000008:
		return t32_pe_freebufs(ops, pad);
	}
	return 0;
}

#endif
