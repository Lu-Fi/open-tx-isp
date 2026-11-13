/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP core event queue: readable C of the vendor's tisp_event_init,
 * tisp_event_push, tisp_event_work_empty, tisp_event_exit and
 * tisp_event_process.
 *
 * Per channel the vendor keeps 3872 B at t32_data_dd7e0: a completion
 * (+0), 80 event nodes of 48 B (+16; list link, type at +8, eight argument
 * words at +16 .. +44) and two list heads, "busy" (+3856) and "free"
 * (+3864).  push takes a free node, copies the event and signals the
 * completion; process waits for it (20 jiffies), takes the oldest busy node,
 * calls the handler registered for (channel, type) (table t32_data_dd6c0,
 * 18 per channel) outside the lock and puts the node back on the free list.
 * A full queue (push) or an empty one (process) counts in t32_data_dd6b0 /
 * t32_data_dd6a0 and returns -1.  One spin lock (vendor t32_small_bss)
 * covers all channels.
 *
 * The OS calls (lock, completion, printf) are hooks (struct t32_event_ops).
 * Lists are plain circular lists with the kernel's list_del poison values.
 * Checked against Matt Davis' recovered C (PR #15 @ 2e2e06bc) by
 * tests/t32_oracle (t32_event_oracle; the lists are compared as node order).
 *
 * Vendor behaviour kept: the handler gets (channel, flags word, arg0 ..
 * arg7) in the vendor (a1 is the left-over spin lock flags register, not a
 * parameter); ours passes (channel, arg0 .. arg7).  Word 3 of an event is
 * never copied (the node keeps its old value).  The handler index is
 * channel * 18 + type without a range check (a type >= 18 reads the next
 * channel's entries); ours reads the same flat table and refuses (no
 * handler) an index beyond its end.
 */
#ifndef TX_ISP_T32_EVENT_H
#define TX_ISP_T32_EVENT_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#include <stddef.h>
#ifndef T32_HOST_U32
#define T32_HOST_U32
typedef uint32_t u32;
#endif
#endif

#define T32_EV_CHANS		4
#define T32_EV_NODES		80
#define T32_EV_TYPES		18	/* handlers per channel */
#define T32_EV_ARGS		8
#define T32_EV_TIMEOUT		20	/* jiffies */
#define T32_EV_ERESTARTSYS	(-512L)
#define T32_EV_POISON1		((struct t32_ev_link *)0x100)
#define T32_EV_POISON2		((struct t32_ev_link *)0x200)

/* the event as the callers build it (48 B; words 0, 1 and 3 are not used) */
struct t32_event {
	u32 rsvd0[2];
	u32 type;		/* +8 */
	u32 rsvd1;		/* +12, never copied */
	u32 arg[T32_EV_ARGS];	/* +16 .. +44 */
};

struct t32_ev_link {
	struct t32_ev_link *next, *prev;
};

struct t32_ev_node {
	struct t32_ev_link l;
	u32 type;
	u32 rsvd1;
	u32 arg[T32_EV_ARGS];
};

struct t32_ev_chan {
	struct t32_ev_node node[T32_EV_NODES];
	struct t32_ev_link busy, free;
};

typedef int (*t32_ev_handler)(u32 ch, u32 arg0, u32 arg1, u32 arg2, u32 arg3, u32 arg4, u32 arg5,
			      u32 arg6, u32 arg7);

struct t32_event_ops {
	void (*lock_init)(void);
	void (*lock)(unsigned long *flags);
	void (*unlock)(unsigned long flags);
	void (*completion_init)(u32 ch);		/* done = 0, init_waitqueue_head */
	void (*complete)(u32 ch);
	long (*wait)(u32 ch, u32 timeout);		/* wait_for_completion_timeout */
	void (*print_restart)(void);			/* isp_printf(2, ..., __func__, 107) */
};

struct t32_event_q {
	struct t32_ev_chan ch[T32_EV_CHANS];		/* t32_data_dd7e0 */
	u32 process_empty[T32_EV_CHANS];		/* t32_data_dd6a0 */
	u32 push_full[T32_EV_CHANS];			/* t32_data_dd6b0 */
	t32_ev_handler handler[T32_EV_CHANS * T32_EV_TYPES];	/* t32_data_dd6c0 */
	const struct t32_event_ops *ops;
};

static inline void t32_ev_list_init(struct t32_ev_link *h)
{
	h->next = h;
	h->prev = h;
}

static inline void t32_ev_list_add_tail(struct t32_ev_link *n, struct t32_ev_link *h)
{
	struct t32_ev_link *prev = h->prev;

	h->prev = n;
	n->next = h;
	n->prev = prev;
	prev->next = n;
}

static inline void t32_ev_list_del(struct t32_ev_link *n)
{
	n->next->prev = n->prev;
	n->prev->next = n->next;
	n->next = T32_EV_POISON1;
	n->prev = T32_EV_POISON2;
}

/* tisp_event_init (0x28cf0): empty lists, all nodes free, completion and lock initialised */
static inline int t32_event_init(struct t32_event_q *q, u32 ch)
{
	struct t32_ev_chan *c = &q->ch[ch];
	u32 k;

	t32_ev_list_init(&c->busy);
	t32_ev_list_init(&c->free);
	for (k = 0; k < T32_EV_NODES; k++)
		t32_ev_list_init(&c->node[k].l);
	for (k = 0; k < T32_EV_NODES; k++)
		t32_ev_list_add_tail(&c->node[k].l, &c->free);
	q->ops->completion_init(ch);
	q->ops->lock_init();
	return 0;
}

/* tisp_event_push (0x28dec): queue a copy of the event, signal the worker */
static inline int t32_event_push(struct t32_event_q *q, u32 ch, const struct t32_event *ev)
{
	struct t32_ev_chan *c = &q->ch[ch];
	struct t32_ev_node *n;
	unsigned long flags;
	u32 k;

	q->ops->lock(&flags);
	if (c->free.next == &c->free) {
		q->push_full[ch]++;
		q->ops->unlock(flags);
		return -1;
	}
	n = (struct t32_ev_node *)c->free.next;
	t32_ev_list_del(&n->l);
	n->type = ev->type;
	for (k = 0; k < T32_EV_ARGS; k++)
		n->arg[k] = ev->arg[k];
	t32_ev_list_add_tail(&n->l, &c->busy);
	q->ops->complete(ch);
	q->ops->unlock(flags);
	return 0;
}

/* tisp_event_work_empty (0x28f50): 1 when the channel (8 bits) has no queued event */
static inline int t32_event_work_empty(struct t32_event_q *q, u32 ch)
{
	struct t32_ev_chan *c = &q->ch[ch & 0xff];
	unsigned long flags;
	int empty;

	q->ops->lock(&flags);
	empty = c->busy.next == &c->busy;
	q->ops->unlock(flags);
	return empty;
}

/* tisp_event_exit (0x28fe0): queue the type-0 (exit) event; the other words are not set */
static inline int t32_event_exit(struct t32_event_q *q, u32 ch)
{
	struct t32_event ev = { .type = 0 };

	t32_event_push(q, ch, &ev);
	return 0;
}

/* tisp_event_process (0x2900c): wait for one event and run its handler */
static inline int t32_event_process(struct t32_event_q *q, u32 ch)
{
	struct t32_ev_chan *c = &q->ch[ch];
	struct t32_ev_node *n;
	t32_ev_handler h = NULL;
	unsigned long flags;
	long r;
	u32 idx;

	r = q->ops->wait(ch, T32_EV_TIMEOUT);
	if (r == T32_EV_ERESTARTSYS) {
		q->ops->print_restart();
		return 0;
	}
	if (r == 0)
		return 0;
	q->ops->lock(&flags);
	if (c->busy.next == &c->busy) {
		q->process_empty[ch]++;
		q->ops->unlock(flags);
		return -1;
	}
	n = (struct t32_ev_node *)c->busy.next;
	t32_ev_list_del(&n->l);
	q->ops->unlock(flags);
	idx = ch * T32_EV_TYPES + n->type;
	if (idx < T32_EV_CHANS * T32_EV_TYPES)
		h = q->handler[idx];
	if (h)
		h(ch, n->arg[0], n->arg[1], n->arg[2], n->arg[3], n->arg[4], n->arg[5], n->arg[6], n->arg[7]);
	q->ops->lock(&flags);
	t32_ev_list_add_tail(&n->l, &c->free);
	q->ops->unlock(flags);
	return 0;
}

#endif /* TX_ISP_T32_EVENT_H */
