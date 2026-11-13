/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 frame channel: buffer-done event from the vendor module (Matt Davis'
 * recovered C, PR #15): frame_chan_event (0x12f20), the pad event handler
 * of a frame channel for event 0x3000006 (frame written by the scaler).
 *
 * Pad: module +0 (its channel array at +288, 816 bytes per channel), pad
 * index byte +4.  Event: buffer address +8, +24, +28, +32, ISP index +36,
 * flags +48.  Channel (vendor offsets): queued list +536 (count +544), done
 * list +548 (prev +552, count +556), done wait queue +560, channel index
 * +720, queued lock +724, completion +740, semaphore +752, last sequence
 * +764, error count +768, miss count +804, mode +808 (1 = encoder path).
 * Buffer: index +0, timestamp sec / usec +20 / +24, sequence +44, address
 * +52, +60, flags +80, field word +88, format +92, state +100, byte +104,
 * frame count +112, +116 / +118 (format 4), +120, queued node +124, done
 * node +132.
 *
 * Flow: the queued list is searched for the event's buffer address; every
 * buffer in front of it was missed (the count is printed with the list).
 * Each of them and the buffer itself (count + 1 times) is taken from the
 * list head: a buffer not in state 3 is reported and counted, the head
 * stays; otherwise it is stamped (sequence: the event's ISP index minus the
 * missed ones still to come), moved to the done list, the waiters woken,
 * the completion (or with d9008 && !d9004 the semaphore) signalled and,
 * with d9008, the first frames printed as TTFF lines (mode 1: 11 per
 * channel index, then the encoder / f3848 callbacks; mode 0: while the
 * counter d9a54 is below f3774).  Without a match the miss count rises.
 * The event's buffer address is kept per channel index in d9bd4.
 *
 * Vendor quirks kept: the TTFF timestamp is sec * 1000000 + usec in 32 bits,
 * sign-extended to the %lld argument; the counters of the search and the
 * take loop are bytes; the "vb is null" line prints the channel index twice.
 * Vendor UB (not reachable from a valid list): 255 missed buffers loop
 * forever (byte counter); a queued node at address 124
 * reads the buffer at address 0.
 *
 * Checked bit-exact against the vendor flow by tests/t32_oracle
 * (t32_fchev_oracle).
 */
#ifndef TX_ISP_T32_FCH_EVENT_H
#define TX_ISP_T32_FCH_EVENT_H

#include "tx_isp_t32_fch_qbuf.h"	/* t32_qb_noted: the frame-skip channel list */

#define T32_FE_EVENT		0x3000006U
#define T32_FE_CHANNELS		288U
#define T32_FE_CH_BYTES		816U
#define T32_FE_QLIST		536U
#define T32_FE_QCOUNT		544U
#define T32_FE_DLIST		548U
#define T32_FE_DCOUNT		556U
#define T32_FE_WAIT		560U
#define T32_FE_INDEX		720U
#define T32_FE_LOCK		724U
#define T32_FE_DONE		740U
#define T32_FE_SEM		752U
#define T32_FE_SEQ		764U
#define T32_FE_ERRS		768U
#define T32_FE_MISS		804U
#define T32_FE_MODE		808U
#define T32_FE_VB_QNODE		124U
#define T32_FE_VB_DNODE		132U
#define T32_FE_VB_STATE		100U
#define T32_FE_VB_MARK		104U
#define T32_FE_STATE_QUEUED	3U
#define T32_FE_STATE_DONE	4U
#define T32_FE_TTFF_MAX		11
#define T32_FE_LIST_POISON1	0x100U
#define T32_FE_LIST_POISON2	0x200U
#define T32_FE_OST_REG		0x2078U	/* OS timer count, read at 0xb0000000 + this */

typedef void (*t32_fe_frame_cb)(u32 chn, u32 index);

struct t32_fchev_os {
	void (*lock)(u8 *l, u32 *flags);		/* __private_spin_lock_irqsave */
	void (*unlock)(u8 *l, u32 flags);		/* private_spin_unlock_irqrestore */
	void (*dump)(void);				/* dump_msca_regs */
	void (*rawmono)(u32 ts[2]);			/* private_getrawmonotonic: sec, nsec */
	void (*wake_up)(u8 *wq);
	void (*complete)(u8 *c);
	void (*up)(u8 *sem);
	void (*sprintf)(char *buf, const char *fmt, u32 chn, u32 index, long long ts);
	void (*send_frame)(u32 chn);			/* VPU_Encoder_SendFrame */
	const u8 *cpm;					/* 0xb0000000 */
	const u32 *d9004, *d9008, *d9014, *d9874, *f3848, *f3774;
	u32 *d9a54, *d9a84;				/* TTFF counters per channel index */
	u32 *d9bd4;					/* last event buffer per channel index */
};

static inline void t32_fe_ttff(const struct t32_fchev_os *os, const u8 *vb, u32 chn, int line)
{
	static const char fn[] = "frame_channel_buffer_done";
	char buf[64];
	u32 us = t32_g_rd32(vb, 20) * 1000000U + t32_g_rd32(vb, 24);

	memset(buf, 0, sizeof(buf));
	os->sprintf(buf, "Chn:%d Buf:%d Write Done TS:%lld", chn, t32_g_rd32(vb, 0), (long long)(int)us);
	isp_printf(1, "TTFF %s:%d %s:%d\n", fn, line, buf,
		   (t32_g_rd32(os->cpm, T32_FE_OST_REG) << 10) / 24000U);
}

/* the buffer at the head of the queued list: done (state 3) or reported */
static inline void t32_fe_take(const struct t32_fchev_os *os, u8 *ch, const u8 *ev, u32 seq)
{
	static const char fn[] = "frame_channel_buffer_done";
	u8 *node = T32_PTR(t32_g_rd32(ch, T32_FE_QLIST)), *vb = node - T32_FE_VB_QNODE, *dn, *prev;
	u32 chn = t32_g_rd32(ch, T32_FE_INDEX), fmt, w, ts[2];
	t32_fe_frame_cb cb;

	if (t32_g_rd32(vb, T32_FE_VB_STATE) != T32_FE_STATE_QUEUED) {
		isp_printf(1, "error: %s, %d vb = %p, buf = 0x%lx, state = %d chan %d\n", fn, 205,
			   vb, t32_g_rd32(vb, 52), t32_g_rd32(vb, T32_FE_VB_STATE), chn);
		os->dump();
		t32_g_wr32(ch, T32_FE_ERRS, t32_g_rd32(ch, T32_FE_ERRS) + 1);
		return;
	}
	vb[T32_FE_VB_MARK] = 102;
	t32_g_wr32(vb, 60, t32_g_rd32(ev, 24));
	fmt = t32_g_rd32(vb, 92);
	if (fmt == 0) {
		w = t32_g_rd32(vb, 88);
		if (t32_qb_noted(*os->d9014, chn))
			w |= 3;
		t32_g_wr32(vb, 88, t32_g_rd32(ev, 28) << 16 | (w & 0xffff));
	} else if (fmt == 4) {
		if (t32_qb_noted(*os->d9014, chn))
			t32_g_wr16(vb, 116, 3);
		vb[118] = (u8)t32_g_rd32(ev, 28);
	}
	os->rawmono(ts);
	t32_g_wr32(vb, 112, t32_g_rd32(vb, 112) + 1);
	t32_g_wr32(vb, 20, ts[0]);
	t32_g_wr32(vb, 24, (u32)((int)ts[1] / 1000));
	t32_g_wr32(vb, 44, seq);
	t32_g_wr32(vb, 80, t32_g_rd32(ev, 48) & 0xf);
	t32_g_wr32(vb, T32_FE_VB_STATE, T32_FE_STATE_DONE);
	/* list_add_tail(done node, done list) */
	dn = vb + T32_FE_VB_DNODE;
	prev = T32_PTR(t32_g_rd32(ch, T32_FE_DLIST + 4));
	t32_g_wr32(ch, T32_FE_DLIST + 4, T32_WORD(dn));
	t32_g_wr32(dn, 4, T32_WORD(prev));
	t32_g_wr32(dn, 0, T32_WORD(ch + T32_FE_DLIST));
	t32_g_wr32(prev, 0, T32_WORD(dn));
	t32_g_wr32(ch, T32_FE_DCOUNT, t32_g_rd32(ch, T32_FE_DCOUNT) + 1);
	/* list_del(queued node) */
	t32_g_wr32(T32_PTR(t32_g_rd32(node, 0)), 4, t32_g_rd32(node, 4));
	t32_g_wr32(T32_PTR(t32_g_rd32(node, 4)), 0, t32_g_rd32(node, 0));
	t32_g_wr32(node, 0, T32_FE_LIST_POISON1);
	t32_g_wr32(node, 4, T32_FE_LIST_POISON2);
	t32_g_wr32(ch, T32_FE_QCOUNT, t32_g_rd32(ch, T32_FE_QCOUNT) - 1);
	os->wake_up(ch + T32_FE_WAIT);
	if (*os->d9008 && !*os->d9004)
		os->up(ch + T32_FE_SEM);
	else
		os->complete(ch + T32_FE_DONE);
	t32_g_wr32(ch, T32_FE_SEQ, seq);
	if (!*os->d9008)
		return;
	if (t32_g_rd32(ch, T32_FE_MODE) == 1) {
		if ((int)os->d9a84[chn]++ < T32_FE_TTFF_MAX)
			t32_fe_ttff(os, vb, chn, 188);
		if (*os->d9874 == 1)
			os->send_frame(chn);
		if (*os->f3848) {
			cb = (t32_fe_frame_cb)T32_PTR(*os->f3848);
			cb(chn, t32_g_rd32(vb, 0));
		}
	} else if (t32_g_rd32(ch, T32_FE_MODE) == 0) {
		if (os->d9a54[chn]++ < os->f3774[chn])
			t32_fe_ttff(os, vb, chn, 199);
	}
}

static inline int t32_frame_chan_event(const struct t32_fchev_os *os, u8 *pad, u32 event, u8 *ev)
{
	static const char fn[] = "frame_channel_buffer_done";
	u8 *ch, *head, *vb;
	u32 flags = 0, chn, missed = 0, i;

	if (!t32_qb_valid((uintptr_t)pad))
		return -22;
	if (event != T32_FE_EVENT)
		return 0;
	if (!ev) {
		isp_printf(1, "error: %s, %d buf == NULL\n", fn, 70);
		os->dump();
		return 0;
	}
	ch = T32_PTR(t32_g_rd32(T32_PTR(t32_g_rd32(pad, 0)), T32_FE_CHANNELS)) + pad[4] * T32_FE_CH_BYTES;
	os->lock(ch + T32_FE_LOCK, &flags);
	head = ch + T32_FE_QLIST;
	for (vb = T32_PTR(t32_g_rd32(head, 0)) - T32_FE_VB_QNODE; vb + T32_FE_VB_QNODE != head;
	     vb = T32_PTR(t32_g_rd32(vb, T32_FE_VB_QNODE)) - T32_FE_VB_QNODE) {
		if (t32_g_rd32(vb, 52) == t32_g_rd32(ev, 8))
			break;
		missed = (missed + 1) & 0xff;
	}
	if (vb + T32_FE_VB_QNODE != head) {
		vb[T32_FE_VB_MARK] = 101;
		t32_g_wr32(vb, 120, t32_g_rd32(ev, 32));
	} else {
		vb = NULL;
	}
	if (missed) {
		u8 *p;

		isp_printf(1, "mscaler: index = %d, buffer idx = %d\n", t32_g_rd32(ch, T32_FE_INDEX), missed);
		for (p = T32_PTR(t32_g_rd32(head, 0)); p != head; p = T32_PTR(t32_g_rd32(p, 0)))
			isp_printf(1, "buflist:0x%08lx\n", t32_g_rd32(p - T32_FE_VB_QNODE, 52));
		isp_printf(1, "buf get:0x%08x isp_index = %d\n", t32_g_rd32(ev, 8), t32_g_rd32(ev, 36));
		os->dump();
	}
	if (vb) {
		for (i = 0; i <= missed; i = (i + 1) & 0xff)
			t32_fe_take(os, ch, ev, t32_g_rd32(ev, 36) - (missed - i));
	} else {
		chn = t32_g_rd32(ch, T32_FE_INDEX);
		if (!t32_g_rd32(ch, T32_FE_MODE))
			isp_printf(1, "vb is null, buf get:0x%08x lastaddr = 0x%08x idx=%d ch = %d isp_index = %d chan %d\n",
				   t32_g_rd32(ev, 8), os->d9bd4[chn], missed, chn, t32_g_rd32(ev, 36), chn);
		t32_g_wr32(ch, T32_FE_MISS, t32_g_rd32(ch, T32_FE_MISS) + 1);
	}
	os->d9bd4[t32_g_rd32(ch, T32_FE_INDEX)] = t32_g_rd32(ev, 8);
	os->unlock(ch + T32_FE_LOCK, flags);
	return 0;
}

#endif
