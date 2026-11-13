/* host shim for t20_fifo_467_host_test: the kernel 3.10 list primitives used by the T10/T20 frame FIFO */
#include <linux/poison.h>
struct list_head { struct list_head *next, *prev; };
#define LIST_HEAD_INIT(n) { &(n), &(n) }
static inline void INIT_LIST_HEAD(struct list_head *l) { l->next = l; l->prev = l; }
static inline void list_add_tail(struct list_head *n, struct list_head *h)
{ struct list_head *p = h->prev; h->prev = n; n->next = h; n->prev = p; p->next = n; }
static inline void __list_del(struct list_head *p, struct list_head *n) { n->prev = p; p->next = n; }
static inline void list_del(struct list_head *e)
{ __list_del(e->prev, e->next); e->next = LIST_POISON1; e->prev = LIST_POISON2; }
static inline void list_del_init(struct list_head *e) { __list_del(e->prev, e->next); INIT_LIST_HEAD(e); }
static inline int list_empty(const struct list_head *h) { return h->next == h; }
