/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "r11G": event queue (driver/t32/tx_isp_t33_r11G.h) with the vendor calling convention.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_r11G.h"

extern void private_spin_lock_init(void *lock);
extern void __init_waitqueue_head(void *wq, const char *name, void *key);
extern void __private_spin_lock_irqsave(void *lock, unsigned long *flags);
extern void private_spin_unlock_irqrestore(void *lock, unsigned long flags);
extern void private_complete(void *c);
extern int private_wait_for_completion_timeout(void *c, int timeout);
extern int isp_printf(int level, const char *fmt, const char *fn, int line);

/* vendor .bss layout: cb at +0x6300 (72 words), tevent_info right after (0x6420) */
struct {
	u8 pad[0x6300];
	u32 cb[72];
	struct t33_ev_chan tevent_info[2];
} bo_bss = { { 0 }, { 0 }, { { 0 } } };
u32 event_empty[4] = { 0 };
u32 event_busy[4] = { 0 };
static u32 ev_lock = 0;

static void op_lock_init(void) { private_spin_lock_init(&ev_lock); }
static void op_waitq_init(struct t33_ev_chan *c) { __init_waitqueue_head(c->waitq, 0, 0); }
static void op_lock(unsigned long *flags) { __private_spin_lock_irqsave(&ev_lock, flags); }
static void op_unlock(unsigned long flags) { private_spin_unlock_irqrestore(&ev_lock, flags); }
static void op_complete(struct t33_ev_chan *c) { private_complete(c); }
static int op_wait(struct t33_ev_chan *c, u32 t) { return private_wait_for_completion_timeout(c, (int)t); }
static void op_print(void) { isp_printf(2, "", "", 104); }

/* filled at run time (no data relocations) */
static void setup(struct t33_event_q *q, struct t33_event_ops *o)
{
	o->lock_init = op_lock_init;
	o->waitq_init = op_waitq_init;
	o->lock = op_lock;
	o->unlock = op_unlock;
	o->complete = op_complete;
	o->wait = op_wait;
	o->print_restart = op_print;
	q->ch = bo_bss.tevent_info;
	q->cb = bo_bss.cb;
	q->empty = event_empty;
	q->full = event_busy;
	q->ops = o;
}

int bo_tisp_event_init(u32 ch)
{
	struct t33_event_ops o;
	struct t33_event_q q;

	setup(&q, &o);
	return t33_event_init(&q, ch);
}

int bo_tisp_event_exit(u32 ch)
{
	struct t33_event_ops o;
	struct t33_event_q q;

	setup(&q, &o);
	return t33_event_exit(&q, ch);
}

int bo_tisp_event_push(u32 ch, u8 *ev)
{
	struct t33_event_ops o;
	struct t33_event_q q;

	setup(&q, &o);
	return t33_event_push(&q, ch, (const struct t33_event *)ev);
}

int bo_tisp_event_process(u32 ch)
{
	struct t33_event_ops o;
	struct t33_event_q q;

	setup(&q, &o);
	return t33_event_process(&q, ch);
}

int bo_tisp_event_set_cb(u32 ch, u32 type, u32 fn)
{
	struct t33_event_ops o;
	struct t33_event_q q;

	setup(&q, &o);
	return t33_event_set_cb(&q, ch, type, fn);
}
