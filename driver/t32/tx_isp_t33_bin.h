/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) default IQ bin path table and bin switch (openimp #99).
 *
 * The vendor keeps a table of default bin paths in the ISP core context:
 * ctx->priv (word 0 of the context block) -> word 276 -> the path table
 * block.  The table has 4 entries of 544 bytes (idx), each with 4 slots of
 * 136 bytes (sel).  The get/set ioctls copy a 136-byte request from user
 * space (word 0 = idx, word 1 = sel) and then read/write the slot.
 *
 * The kernel services are passed as callbacks, like tx_isp_t33_attr1.h.
 */
#ifndef TX_ISP_T33_BIN_H
#define TX_ISP_T33_BIN_H

#include "tx_isp_t33_blk.h"

#define T33_BIN_REQ_BYTES	136
#define T33_BIN_IDX_MAX		4
#define T33_BIN_ENTRY_BYTES	544
#define T33_BIN_GET_OFF		1840	/* table + idx * 544 + sel * 136 */
#define T33_BIN_SET_OFF		1832
#define T33_BIN_STATE_OFF	296	/* state word per channel */
#define T33_BIN_STATE_READY	4
#define T33_BIN_DONE_OFF	1160	/* completion done flag per channel */
#define T33_BIN_CMPL_OFF	152	/* completion address in the context */
#define T33_BIN_CH_OFF		32	/* per-channel word */
#define T33_BIN_CH_STRIDE	32
#define T33_BIN_TIMEOUT_MS	1000

struct t33_bin_ops {
	int (*cfu)(void *to, const void *from, unsigned long n);
	int (*ctu)(void *to, const void *from, unsigned long n);
	int (*sprintf)(char *buf, const char *fmt, const char *arg);
	void *(*memcpy)(void *to, const void *from, unsigned long n);
	int (*isp_printf)(int level, const char *fmt, ...);
	unsigned long (*msecs_to_jiffies)(unsigned int ms);
	unsigned long (*wait_for_completion_timeout)(void *done, unsigned long timeout);
	int (*switch_bin)(unsigned int ch, const u8 *req);
};

/* the path table block: ctx->priv->word276 */
static inline u8 *t33_bin_table(u8 *ctx)
{
	u8 *p = (u8 *)(unsigned long)t33_le32(ctx);

	return (u8 *)(unsigned long)t33_le32(p + 276);
}

/* tx_isp_get_default_bin_path.isra.28 */
static inline int t33_bin_get_path(u8 *ctx, u8 *user, const struct t33_bin_ops *ops)
{
	u8 req[T33_BIN_REQ_BYTES];
	u32 idx, sel;
	u8 *tab;

	if (ops->cfu(req, user, T33_BIN_REQ_BYTES)) {
		ops->isp_printf(2, "copy_from_user error");
		return -14;
	}
	idx = t33_le32(req);
	if (idx >= T33_BIN_IDX_MAX) {
		ops->isp_printf(2, "idx error");
		return 0;
	}
	sel = t33_le32(req + 4);
	tab = t33_bin_table(ctx);
	ops->sprintf((char *)(req + 8),
		     (const char *)(tab + T33_BIN_GET_OFF + idx * T33_BIN_ENTRY_BYTES +
				    sel * T33_BIN_REQ_BYTES), 0);
	if (ops->ctu(user, req, T33_BIN_REQ_BYTES)) {
		ops->isp_printf(2, "copy_to_user error");
		return -14;
	}
	return 0;
}

/* tx_isp_set_default_bin_path.isra.25 */
static inline int t33_bin_set_path(u8 *ctx, u8 *user, const struct t33_bin_ops *ops)
{
	u8 req[T33_BIN_REQ_BYTES];
	u32 idx, sel;
	u8 *tab;

	if (ops->cfu(req, user, T33_BIN_REQ_BYTES)) {
		ops->isp_printf(2, "copy_from_user error");
		return -14;
	}
	idx = t33_le32(req);
	if (idx >= T33_BIN_IDX_MAX) {
		ops->isp_printf(2, "idx error");
		return 0;
	}
	sel = t33_le32(req + 4);
	tab = t33_bin_table(ctx);
	ops->memcpy(tab + T33_BIN_SET_OFF + idx * T33_BIN_ENTRY_BYTES + sel * T33_BIN_REQ_BYTES, req,
		    T33_BIN_REQ_BYTES);
	return 0;
}

/* tx_isp_switch_bin.isra.24 */
static inline int t33_bin_switch(unsigned int ch, u8 *ctx, u8 *user, const struct t33_bin_ops *ops)
{
	u8 req[T33_BIN_REQ_BYTES];
	u8 *tab = t33_bin_table(ctx);
	u8 *done;
	int ret, cret;
	unsigned long to;

	cret = ops->cfu(req, user, T33_BIN_REQ_BYTES);
	if (cret) {
		ops->isp_printf(2, "copy_from_user error");
		return cret;
	}
	if (t33_le32(tab + T33_BIN_STATE_OFF + 4 * ch) != T33_BIN_STATE_READY) {
		ops->isp_printf(2, "state error");
		return 0;
	}
	to = ops->msecs_to_jiffies(T33_BIN_TIMEOUT_MS);
	ret = ops->switch_bin(ch, req);
	if (t33_le32(ctx + T33_BIN_CH_OFF + T33_BIN_CH_STRIDE * ch) != req[4]) {
		if (!ret)
			return 0;
		ops->isp_printf(1, "switch error");
		return ret;
	}
	done = tab + T33_BIN_DONE_OFF + 4 * ch;
	done[0] = 1;
	done[1] = 0;
	done[2] = 0;
	done[3] = 0;
	if (!ops->wait_for_completion_timeout(ctx + T33_BIN_CMPL_OFF, to)) {
		ops->isp_printf(1, "timeout");
		return -145;
	}
	return 0;
}

#endif /* TX_ISP_T33_BIN_H */
