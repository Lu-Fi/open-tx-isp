#ifndef TX_ISP_T2X_FIFO_H
#define TX_ISP_T2X_FIFO_H

#include <linux/list.h>
#include <linux/poison.h>

/*
 * openimp #467: private core ioctl of the T10/T20 frame channel: unlink a
 * vb2 buffer from the core channel FIFO before vb2 frees it (buf_cleanup).
 * The value follows the vendor ioctl enum (tx-isp-common.h ends with
 * FRAME_CHAN_QUEUE_BUFFER); it never leaves the module.  The later vendor
 * generations (T30+) have the same step as their FREE_BUFFER event.
 */
#define TX_ISP_T2X_PRIVATE_IOCTL_FRAME_CHAN_FREE_BUFFER	0x7467

/*
 * True when a frame-channel list entry is on a list.  Entries start
 * INIT_LIST_HEAD'ed (buf_init) and are removed with list_del_init; a zeroed
 * or poisoned entry counts as unlinked.
 */
static inline int tx_isp_t2x_entry_linked(const struct list_head *e)
{
	return e->next && e->next != e && e->next != LIST_POISON1;
}

#endif
