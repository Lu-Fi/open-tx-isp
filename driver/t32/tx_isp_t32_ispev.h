/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP core glue, part 3: the core event queue.
 * Readable C of the vendor functions
 *   isp_core_event_init (0x14580), isp_core_event_set_cb (0x14668),
 *   isp_core_event_work_empty (0x146cc), isp_core_event_push (0x1475c),
 *   isp_core_event_exit (0x148b8), isp_core_event_process (0x148e8).
 *
 * Per channel a 2268 B queue block (t32_data_d9e78 + 2268 * ch): +0 completion (done word,
 * wait queue at +4), +12 80 nodes of 28 B { list_head, type, 4 argument words }, +2252 the busy
 * list, +2260 the free list.  Callbacks: t32_data_d9d38, 80 words per channel, indexed by the
 * event type (< 10).  Drop counters: t32_data_d9d34[ch] (push, no free node) and
 * t32_data_d9d30[ch] (process, no busy node).  One global spinlock (t32_small_bss) guards all
 * channels.  The vendor tables are laid out for one channel: for ch > 0 the counters, the
 * callback rows and the queue blocks overlap their neighbours (kept, the oracle uses the vendor
 * layout).  isp_core_event_init initialises the completion's wait queue at +4 after the node
 * lists, so on the device node 0's list words are overwritten by it (kept).
 *
 * Vendor behaviour kept: set_cb accepts negative types (writes before the row); a callback that
 * fails (non-zero) loses its node (it is not returned to the free list); the event of
 * isp_core_event_exit carries only the type (0), the vendor leaves its other words uninitialised
 * (0 here).
 *
 * Checked against the vendor functions (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_ispev_oracle).
 */
#ifndef TX_ISP_T32_ISPEV_H
#define TX_ISP_T32_ISPEV_H

#include "tx_isp_t32_ispcore.h"

#define T32_EVQ_SIZE		2268
#define T32_EVQ_NODES		12
#define T32_EVQ_NODE_SIZE	28
#define T32_EVQ_NNODES		80
#define T32_EVQ_BUSY		2252
#define T32_EVQ_FREE		2260
#define T32_EVQ_NTYPES		10
#define T32_EVQ_CB_ROW		80
#define T32_LIST_POISON1	0x100
#define T32_LIST_POISON2	0x200
#define T32_ENOMEM		(-12)
#define T32_EINVAL		(-22)
#define T32_ERESTARTSYS		(-512)

struct t32_ev_ops {
	void (*waitqueue_init)(u32 wq);			/* __init_waitqueue_head(wq, name, key) */
	void (*spin_lock_init)(u32 lock);
	u32 (*lock)(u32 lock);				/* __private_spin_lock_irqsave: returns the flags */
	void (*unlock)(u32 lock, u32 flags);
	void (*complete)(u32 c);
	int (*wait)(u32 c, u32 timeout);		/* private_wait_for_completion_timeout */
	int (*call5)(u32 fn, u32 a0, u32 a1, u32 a2, u32 a3, u32 a4);
	void (*log)(u32 level, u32 line, u32 a4, u32 a5);	/* isp_printf(level, fmt, func, line, a4, a5) */
};

struct t32_ev_glob {
	u32 drop_proc;		/* t32_data_d9d30 */
	u32 drop_push;		/* t32_data_d9d34 */
	u32 cbs;		/* t32_data_d9d38 */
	u32 q;			/* t32_data_d9e78 */
	u32 lock;		/* t32_small_bss */
};

static inline void t32_list_init(u32 h)
{
	t32_ic_w32(h, 0, h);
	t32_ic_w32(h, 4, h);
}

static inline void t32_list_add_tail(u32 n, u32 head)
{
	u32 prev = t32_ic_r32(head, 4);

	t32_ic_w32(head, 4, n);
	t32_ic_w32(n, 0, head);
	t32_ic_w32(n, 4, prev);
	t32_ic_w32(prev, 0, n);
}

static inline void t32_list_del(u32 n)
{
	u32 next = t32_ic_r32(n, 0), prev = t32_ic_r32(n, 4);

	t32_ic_w32(next, 4, prev);
	t32_ic_w32(prev, 0, next);
	t32_ic_w32(n, 0, T32_LIST_POISON1);
	t32_ic_w32(n, 4, T32_LIST_POISON2);
}

static inline u32 t32_evq(const struct t32_ev_glob *g, u32 ch)
{
	return g->q + (ch & 0xff) * T32_EVQ_SIZE;
}

/* isp_core_event_init(ch) */
static inline int t32_isp_core_event_init(const struct t32_ev_ops *ops, const struct t32_ev_glob *g, u32 ch)
{
	u32 q = t32_evq(g, ch), k;

	t32_list_init(q + T32_EVQ_BUSY);
	t32_list_init(q + T32_EVQ_FREE);
	for (k = 0; k < T32_EVQ_NNODES; k++)
		t32_list_init(q + T32_EVQ_NODES + k * T32_EVQ_NODE_SIZE);
	for (k = 0; k < T32_EVQ_NNODES; k++)
		t32_list_add_tail(q + T32_EVQ_NODES + k * T32_EVQ_NODE_SIZE, q + T32_EVQ_FREE);
	t32_ic_w32(q, 0, 0);
	ops->waitqueue_init(q + 4);
	ops->spin_lock_init(g->lock);
	return 0;
}

/* isp_core_event_set_cb(ch, type, cb) */
static inline int t32_isp_core_event_set_cb(const struct t32_ev_ops *ops, const struct t32_ev_glob *g,
					    u32 ch, int type, u32 cb)
{
	if (type >= T32_EVQ_NTYPES) {
		ops->log(2, 0, 0, 0);
		return -1;
	}
	t32_ic_w32(g->cbs, ((ch & 0xff) * T32_EVQ_CB_ROW + type) << 2, cb);
	return 0;
}

/* isp_core_event_work_empty(ch): 1 when no event is queued */
static inline int t32_isp_core_event_work_empty(const struct t32_ev_ops *ops, const struct t32_ev_glob *g, u32 ch)
{
	u32 busy = t32_evq(g, ch) + T32_EVQ_BUSY, fl = ops->lock(g->lock);
	int empty = t32_ic_r32(busy, 0) == busy;

	ops->unlock(g->lock, fl);
	return empty;
}

/* isp_core_event_push(ch, ev): ev = 28 B event, words 2..6 (type, 4 arguments) are queued */
static inline int t32_isp_core_event_push(const struct t32_ev_ops *ops, const struct t32_ev_glob *g, u32 ch, u32 ev)
{
	u32 q = t32_evq(g, ch), fl = ops->lock(g->lock), n, k;

	n = t32_ic_r32(q + T32_EVQ_FREE, 0);
	if (n == q + T32_EVQ_FREE) {
		t32_ic_w32(g->drop_push, (ch & 0xff) << 2, t32_ic_r32(g->drop_push, (ch & 0xff) << 2) + 1);
		ops->unlock(g->lock, fl);
		ops->log(1, 71, 0, 0);
		return T32_ENOMEM;
	}
	t32_list_del(n);
	for (k = 8; k < T32_EVQ_NODE_SIZE; k += 4)
		t32_ic_w32(n, k, t32_ic_r32(ev, k));
	t32_list_add_tail(n, q + T32_EVQ_BUSY);
	ops->unlock(g->lock, fl);
	ops->complete(q);
	return 0;
}

/* isp_core_event_exit(ch): queue the exit event (type 0) */
static inline int t32_isp_core_event_exit(const struct t32_ev_ops *ops, const struct t32_ev_glob *g, u32 ch)
{
	u32 ev[9] = { 0 };

	t32_isp_core_event_push(ops, g, ch, T32_IC_A(ev));
	return 0;
}

/* isp_core_event_process(ch): wait up to 20 jiffies for an event and run its callback */
static inline int t32_isp_core_event_process(const struct t32_ev_ops *ops, const struct t32_ev_glob *g, u32 ch)
{
	u32 q = t32_evq(g, ch), busy = q + T32_EVQ_BUSY, fl, n, type, cb;
	int r = ops->wait(q, 20);

	ch &= 0xff;
	if (r == T32_ERESTARTSYS) {
		ops->log(2, 120, 0, 0);
		return 0;
	}
	if (!r)
		return 0;
	fl = ops->lock(g->lock);
	n = t32_ic_r32(busy, 0);
	if (n == busy) {
		t32_ic_w32(g->drop_proc, ch << 2, t32_ic_r32(g->drop_proc, ch << 2) + 1);
		ops->unlock(g->lock, fl);
		ops->log(2, 135, 0, 0);
		return -1;
	}
	t32_list_del(n);
	ops->unlock(g->lock, fl);
	type = t32_ic_r32(n, 8);
	cb = t32_ic_r32(g->cbs, (ch * T32_EVQ_CB_ROW + type) << 2);
	if (cb) {
		if (ops->call5(cb, ch, t32_ic_r32(n, 12), t32_ic_r32(n, 16), t32_ic_r32(n, 20), t32_ic_r32(n, 24))) {
			ops->log(2, 148, t32_ic_r32(n, 24), type);	/* a4: the callback's 5th argument slot */
			return T32_EINVAL;
		}
	} else {
		ops->log(2, 152, type, 0);
	}
	fl = ops->lock(g->lock);
	t32_list_add_tail(n, q + T32_EVQ_FREE);
	ops->unlock(g->lock, fl);
	return 0;
}

#endif /* TX_ISP_T32_ISPEV_H */
