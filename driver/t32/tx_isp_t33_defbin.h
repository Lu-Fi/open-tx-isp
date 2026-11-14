/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) default IQ bin request: the ioctl copies an 880-byte request
 * from user space, fills in the sensor settings, bin info and day/night / WDR
 * state, and copies it back (openimp #99).  Kernel services are callbacks.
 */
#ifndef TX_ISP_T33_DEFBIN_H
#define TX_ISP_T33_DEFBIN_H

#include "tx_isp_t33_blk.h"

#define T33_DB_REQ_BYTES	880
#define T33_DB_NSEC		76	/* section count byte */
#define T33_DB_FLAG		861
#define T33_DB_SENSOR		862
#define T33_DB_FIRST		860
#define T33_DB_WDR		852
#define T33_DB_DAYNIGHT		856
#define T33_DB_EFAULT		-14
#define T33_DB_PRIV_TAB		276	/* priv word: table block */
#define T33_DB_CUR_OFF		1192	/* table word: current sensor + 1 */

struct t33_defbin_ops {
	int (*cfu)(void *to, const void *from, unsigned long n);
	int (*ctu)(void *to, const void *from, unsigned long n);
	u32 (*start_idx)(u32 s);
	int (*nsec)(u32 s, u32 start);
	unsigned long (*setting)(u32 s, u32 start, u32 i);
	unsigned long (*setting_len)(u32 s, u32 start, u32 i);
	unsigned long (*setting_md5)(u32 s, u32 start, u32 i);
	void (*release)(u32 s, u32 start, u32 i);
	void (*bin_info)(u32 s, void *req);
	u32 (*day_night)(u32 s);
	u32 (*wdr)(u32 s);
	int (*isp_printf)(int level, const char *fmt, ...);
};

static inline void t33_db_put32(u8 *p, u32 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
	p[2] = (u8)(v >> 16);
	p[3] = (u8)(v >> 24);
}

/* tx_isp_default_bin.isra.34 */
static inline int t33_default_bin(u8 *ctx, u8 *user, const u32 *zopen, u8 *zfirst,
				  const struct t33_defbin_ops *ops)
{
	u8 req[T33_DB_REQ_BYTES];
	u8 *tab;
	u32 s0, iq;
	int i, n;

	if (ops->cfu(req, user, T33_DB_REQ_BYTES)) {
		ops->isp_printf(2, "copy_from_user error");
		return T33_DB_EFAULT;
	}
	tab = (u8 *)(unsigned long)t33_le32((u8 *)(unsigned long)t33_le32(ctx) + T33_DB_PRIV_TAB);
	s0 = req[T33_DB_SENSOR];
	if (*zopen) {
		iq = ops->start_idx(s0);
		t33_db_put32(req, 2);
		req[T33_DB_NSEC] = (u8)ops->nsec(s0, iq);
		ops->isp_printf(1, "sensor setting");
		for (i = 0; i < req[T33_DB_NSEC]; i++) {
			unsigned long set = ops->setting(s0, iq, (u32)i);

			t33_db_put32(req + 4 + 4 * i, (u32)set);
			t33_db_put32(req + 28 + 4 * i, (u32)set + 0x80000000u);
			t33_db_put32(req + 52 + 4 * i, (u32)ops->setting_len(s0, iq, (u32)i));
			ops->setting_md5(s0, iq, (u32)i);
		}
	}
	if (req[T33_DB_FLAG] && t33_le32(tab + T33_DB_CUR_OFF) - 1 == s0) {
		*zfirst = 0;
		n = ops->nsec(s0, 0);
		iq = ops->start_idx(s0);
		for (i = 0; i < n; i++)
			ops->release(s0, iq, (u32)i);
	}
	ops->bin_info(s0, req);
	req[T33_DB_FIRST] = *zopen ? (*zfirst != 0) : 0;
	t33_db_put32(req + T33_DB_DAYNIGHT, ops->day_night(s0));
	t33_db_put32(req + T33_DB_WDR, ops->wdr(s0));
	if (ops->ctu(user, req, T33_DB_REQ_BYTES)) {
		ops->isp_printf(2, "copy_to_user error");
		return T33_DB_EFAULT;
	}
	return 0;
}

#endif /* TX_ISP_T33_DEFBIN_H */
