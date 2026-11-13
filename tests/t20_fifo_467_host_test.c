/*
 * openimp #467: T10/T20 core channel FIFO must never link a vb2 buffer twice
 * and must forget a buffer that vb2 frees.  Replays the crash sequence of
 * the device oops (frame IRQ list_del on a poisoned entry, BadVA 00200200):
 * a buffer left on the FIFO by the previous streamer, freed by REQBUFS,
 * re-allocated at the same address and queued again by the next streamer.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../driver/t20/sdk/source/tx-isp-t2x-fifo.h"

static int fails;
#define CHECK(c, m) do { if (!(c)) { printf("FAIL %s\n", m); fails++; } else printf("ok   %s\n", m); } while (0)

struct buf { struct list_head entry; int id; };
static struct list_head fifo = LIST_HEAD_INIT(fifo);

/* driver code paths (tx-isp-core.c / tx-isp-frame-channel.c after #467) */
static void buf_init(struct buf *b) { INIT_LIST_HEAD(&b->entry); }
static void queue_buffer(struct buf *b) { if (!tx_isp_t2x_entry_linked(&b->entry)) list_add_tail(&b->entry, &fifo); }
static void free_buffer(struct buf *b) { if (tx_isp_t2x_entry_linked(&b->entry)) list_del_init(&b->entry); }
static struct buf *pop(void)
{
	struct buf *b;
	if (list_empty(&fifo)) return NULL;
	b = (struct buf *)fifo.next;
	if (b->entry.prev == LIST_POISON2 || b->entry.next == LIST_POISON1) { printf("poisoned entry popped\n"); exit(2); }
	list_del_init(&b->entry);
	return b;
}
static int count(void) { int n = 0; struct list_head *p; for (p = fifo.next; p != &fifo && n < 100; p = p->next) n++; return n; }

int main(void)
{
	static struct buf slot[4];   /* "slab": REQBUFS of the next streamer reuses the same addresses */
	int i;

	/* streamer 1 queues 4 buffers; it exits with them still on the FIFO */
	for (i = 0; i < 4; i++) { buf_init(&slot[i]); slot[i].id = i; queue_buffer(&slot[i]); }
	CHECK(count() == 4, "streamer 1: 4 buffers on the FIFO");
	/* vb2 frees them (buf_cleanup) */
	for (i = 0; i < 4; i++) free_buffer(&slot[i]);
	CHECK(count() == 0 && list_empty(&fifo), "buf_cleanup took every freed buffer off the FIFO");
	/* streamer 2: same addresses, kzalloc'd then buf_init, queued */
	memset(slot, 0, sizeof(slot));
	CHECK(!tx_isp_t2x_entry_linked(&slot[0].entry), "zeroed entry counts as unlinked");
	for (i = 0; i < 4; i++) { buf_init(&slot[i]); slot[i].id = 10 + i; queue_buffer(&slot[i]); }
	CHECK(count() == 4, "streamer 2: 4 buffers, none linked twice");
	/* a second QBUF of a linked buffer must not corrupt the list */
	queue_buffer(&slot[1]);
	CHECK(count() == 4, "double queue ignored");
	for (i = 0; i < 4; i++) { struct buf *b = pop(); CHECK(b && b->id == 10 + i, "frame IRQ pops in order"); }
	CHECK(pop() == NULL, "FIFO empty after 4 pops");
	/* popped entries are reusable (list_del_init, not poison) */
	CHECK(!tx_isp_t2x_entry_linked(&slot[2].entry), "popped entry unlinked, not poisoned");
	queue_buffer(&slot[2]); CHECK(count() == 1, "requeue after pop");
	{ struct list_head p = { LIST_POISON1, LIST_POISON2 }; CHECK(!tx_isp_t2x_entry_linked(&p), "poisoned entry counts as unlinked"); }
	printf("%s\n", fails ? "t20_fifo_467: FAIL" : "t20_fifo_467: PASS");
	return fails != 0;
}
