/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 ISP core event queue (vendor tisp_event_init / tisp_event_exit /
 * tisp_event_process / tisp_event_push / tisp_event_set_cb).
 *
 * Per channel the vendor keeps 3872 bytes: a completion (+0) and wait queue
 * (+4), 80 nodes of 48 bytes (+16: list link, type at +8, eight argument words
 * at +16..+44 of the event) and two list heads, busy (+3856) and free (+3864).
 * push moves a free node to the busy list and signals the completion, process
 * waits for it, takes the oldest busy node, runs the handler registered for
 * (channel, type) (table of 18 per channel, no range check) and returns the
 * node to the free list.  OS calls are hooks (struct t33_event_ops).
 *
 * Vendor behaviour kept: the handler gets (ch, arg3, arg0 .. arg7), the first
 * arg3 being a left-over register; node word 3 is never copied.
 */
#ifndef TX_ISP_T33_R11G_H
#define TX_ISP_T33_R11G_H

#include "tx_isp_t33_blk.h"

#define T33_EV_NODES 80
#define T33_EV_TYPES 18
#define T33_EV_ARGS 8
#define T33_EV_TIMEOUT 20
#define T33_EV_ERESTARTSYS (-512)
#define T33_EV_POISON_NEXT ((struct t33_ev_link *)0x100100)
#define T33_EV_POISON_PREV ((struct t33_ev_link *)0x200200)

struct t33_ev_link {
	struct t33_ev_link *next, *prev;
};

struct t33_ev_node {
	struct t33_ev_link l;
	u32 type;
	u32 rsvd;
	u32 arg[T33_EV_ARGS];
};

struct t33_ev_chan {
	u32 done;
	u32 waitq[3];
	struct t33_ev_node node[T33_EV_NODES];
	struct t33_ev_link busy, free;
};

/* the event as callers build it (48 bytes; words 0, 1 and 3 are not used) */
struct t33_event {
	u32 rsvd0[2];
	u32 type;
	u32 rsvd1;
	u32 arg[T33_EV_ARGS];
};

typedef void (*t33_ev_handler)(u32, u32, u32, u32, u32, u32, u32, u32, u32, u32);

struct t33_event_ops {
	void (*lock_init)(void);
	void (*waitq_init)(struct t33_ev_chan *c);
	void (*lock)(unsigned long *flags);
	void (*unlock)(unsigned long flags);
	void (*complete)(struct t33_ev_chan *c);
	int (*wait)(struct t33_ev_chan *c, u32 timeout);
	void (*print_restart)(void);
};

struct t33_event_q {
	struct t33_ev_chan *ch;
	u32 *cb;		/* handler table, 18 per channel */
	u32 *empty;		/* process: queue empty count per channel */
	u32 *full;		/* push: queue full count per channel */
	const struct t33_event_ops *ops;
};

static inline void t33_ev_list_init(struct t33_ev_link *h)
{
	h->next = h;
	h->prev = h;
}

static inline void t33_ev_list_add_tail(struct t33_ev_link *n, struct t33_ev_link *h)
{
	struct t33_ev_link *prev = h->prev;

	h->prev = n;
	n->next = h;
	n->prev = prev;
	prev->next = n;
}

static inline void t33_ev_list_del(struct t33_ev_link *n)
{
	n->next->prev = n->prev;
	n->prev->next = n->next;
	n->next = T33_EV_POISON_NEXT;
	n->prev = T33_EV_POISON_PREV;
}

/* tisp_event_init */
static inline int t33_event_init(struct t33_event_q *q, u32 ch)
{
	struct t33_ev_chan *c = &q->ch[ch];
	u32 k;

	t33_ev_list_init(&c->busy);
	t33_ev_list_init(&c->free);
	for (k = 0; k < T33_EV_NODES; k++)
		t33_ev_list_init(&c->node[k].l);
	for (k = 0; k < T33_EV_NODES; k++)
		t33_ev_list_add_tail(&c->node[k].l, &c->free);
	c->done = 0;
	q->ops->waitq_init(c);
	q->ops->lock_init();
	return 0;
}

/* tisp_event_set_cb: handler for (channel, type) */
static inline int t33_event_set_cb(struct t33_event_q *q, u32 ch, u32 type, u32 fn)
{
	q->cb[ch * T33_EV_TYPES + type] = fn;
	return 0;
}

/* tisp_event_push */
static inline int t33_event_push(struct t33_event_q *q, u32 ch, const struct t33_event *ev)
{
	struct t33_ev_chan *c = &q->ch[ch];
	struct t33_ev_node *n;
	unsigned long flags = 1;
	u32 k;

	q->ops->lock(&flags);
	if (c->free.next == &c->free) {
		q->full[ch]++;
		q->ops->unlock(flags);
		return -1;
	}
	n = (struct t33_ev_node *)c->free.next;
	t33_ev_list_del(&n->l);
	n->type = ev->type;
	for (k = 0; k < T33_EV_ARGS; k++)
		n->arg[k] = ev->arg[k];
	t33_ev_list_add_tail(&n->l, &c->busy);
	q->ops->complete(c);
	q->ops->unlock(flags);
	return 0;
}

/* tisp_event_exit: queue the type 0 event (other words not set) */
static inline int t33_event_exit(struct t33_event_q *q, u32 ch)
{
	struct t33_event ev = { { 0, 0 }, 0, 0, { 0, 0, 0, 0, 0, 0, 0, 0 } };

	t33_event_push(q, ch, &ev);
	return 0;
}

/* tisp_event_process */
static inline int t33_event_process(struct t33_event_q *q, u32 ch)
{
	struct t33_ev_chan *c = &q->ch[ch];
	struct t33_ev_node *n;
	t33_ev_handler h;
	unsigned long flags = 0;
	int r;

	r = q->ops->wait(c, T33_EV_TIMEOUT);
	if (r == T33_EV_ERESTARTSYS) {
		q->ops->print_restart();
		return 0;
	}
	if (!r)
		return 0;
	q->ops->lock(&flags);
	if (c->busy.next == &c->busy) {
		q->empty[ch]++;
		q->ops->unlock(flags);
		return -1;
	}
	n = (struct t33_ev_node *)c->busy.next;
	t33_ev_list_del(&n->l);
	q->ops->unlock(flags);
	h = (t33_ev_handler)(unsigned long)q->cb[ch * T33_EV_TYPES + n->type];
	if (h)
		h(ch, n->arg[3], n->arg[0], n->arg[1], n->arg[2], n->arg[3], n->arg[4], n->arg[5],
		  n->arg[6], n->arg[7]);
	q->ops->lock(&flags);
	t33_ev_list_add_tail(&n->l, &c->free);
	q->ops->unlock(flags);
	return 0;
}

#endif /* TX_ISP_T33_R11G_H */
