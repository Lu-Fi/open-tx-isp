/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 denoise block teardown: tisp_ydns_deinit / tisp_sdns_deinit / tisp_cdns_deinit / tisp_rdns_deinit.
 * Each frees the per-channel info block (kfree), clears the slot, releases the register list of the module
 * and logs (printk) a negative release result.  Channel is u8 (a0 & 0xff).
 * OS calls are hooks.  Checked by tests/t33_binoracle (block i9K).
 */
#ifndef TX_ISP_T33_I9K_H
#define TX_ISP_T33_I9K_H

#include "tx_isp_t33_blk.h"

#define T33_YDNS_DEINIT_LIST	17
#define T33_SDNS_DEINIT_LIST	20
#define T33_CDNS_DEINIT_LIST	21
#define T33_RDNS_DEINIT_LIST	3

/* shared body of the four vendor deinit functions; slot = <blk>Info (pointer per channel) */
static inline int t33_dns_deinit(u32 *slot, u32 ch, u32 id, void (*kfree)(void *p),
				 int (*rel)(u32 ch, u32 id), int (*err)(u32 id, int r))
{
	int r;

	ch &= 0xff;
	if (slot[ch]) {
		kfree((void *)(unsigned long)slot[ch]);
		slot[ch] = 0;
	}
	r = rel(ch, id);
	if (r < 0)
		return err(id, r);
	return r;
}

/* tisp_ydns_deinit */
static inline int t33_ydns_deinit_ch(u32 *slot, u32 ch, void (*kfree)(void *p),
				     int (*rel)(u32 ch, u32 id), int (*err)(u32 id, int r))
{
	return t33_dns_deinit(slot, ch, T33_YDNS_DEINIT_LIST, kfree, rel, err);
}

/* tisp_sdns_deinit */
static inline int t33_sdns_deinit_ch(u32 *slot, u32 ch, void (*kfree)(void *p),
				     int (*rel)(u32 ch, u32 id), int (*err)(u32 id, int r))
{
	return t33_dns_deinit(slot, ch, T33_SDNS_DEINIT_LIST, kfree, rel, err);
}

/* tisp_cdns_deinit */
static inline int t33_cdns_deinit_ch(u32 *slot, u32 ch, void (*kfree)(void *p),
				     int (*rel)(u32 ch, u32 id), int (*err)(u32 id, int r))
{
	return t33_dns_deinit(slot, ch, T33_CDNS_DEINIT_LIST, kfree, rel, err);
}

/* tisp_rdns_deinit */
static inline int t33_rdns_deinit_ch(u32 *slot, u32 ch, void (*kfree)(void *p),
				     int (*rel)(u32 ch, u32 id), int (*err)(u32 id, int r))
{
	return t33_dns_deinit(slot, ch, T33_RDNS_DEINIT_LIST, kfree, rel, err);
}

#endif /* TX_ISP_T33_I9K_H */
