/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 ISP library teardown (lib_tisp_deinit callees that have no T32 namesake) and the T33 full reset:
 *
 *   t33_all_reset        tisp_all_reset: interrupt words 0x22028 / 0x2202c = 0xffffffff, stop request 0x200 = 1,
 *                        poll 0x204 for 1 (20000 reads; timeout: error log line 289, end); core reset
 *                        0x24 = 0x40000001, poll register 0 until it reads nonzero (20000 reads; timeout: error
 *                        log line 301); info log line 304 (level 0).  Returns the last log's result.  The T32
 *                        (lib_tisp_all_reset) polls 10000 times and waits for 0x24 & 0xf == 0 instead.
 *   t33_blc_deinit       tisp_blc_deinit: frees blc_para[ch] (vfree), releases list 4; error -> printk.
 *   t33_ldc_deinit       tisp_ldc_t33_deinit: LDC interrupt mask 0x27088 = 0xffffffff, frees stLDCAttr[ch]
 *                        (kfree, ch < 4), releases list 25 and returns its result.
 *   t33_msca_deinit      tisp_msca_t33_deinit: frees stMscaAttr[ch] and msca_info[ch] (kfree, ch not masked
 *                        for the arrays, masked for the release), releases list 23; error -> printk; returns 0.
 *   t33_normal_ae_deinit tisp_normal_ae_deinit: stAeInfo / fliker_info / fliker_para[ch] = 0 (no free).
 *   t33_dma_deinit       tisp_dma_deinit (no caller in the vendor module): g_wdmaVinum = 0xff, stop wdma_thread.
 *
 * Channel arguments are u8 in the vendor (a0 & 0xff) unless noted.  OS and register calls are hooks.
 * Checked against the vendor instructions by tests/t33_binoracle (block deinit, openimp #99).
 */
#ifndef TX_ISP_T33_DEINIT_H
#define TX_ISP_T33_DEINIT_H

#ifndef __KERNEL__
#include <stdint.h>
#ifndef T32_HOST_U32
#define T32_HOST_U32
typedef uint32_t u32;
#endif
#ifndef T32_HOST_U8
#define T32_HOST_U8
typedef uint8_t u8;
#endif
#endif

#define T33_RST_INTP0		0x22028U
#define T33_RST_INTP1		0x2202cU
#define T33_RST_REQ		0x200U
#define T33_RST_ACK		0x204U
#define T33_RST_CORE		0x24U
#define T33_RST_CORE_VAL	0x40000001U
#define T33_RST_STATE		0x0U
#define T33_RST_POLLS		20000U
#define T33_LDC_INT_MASK	0x27088U
#define T33_DEIN_LIST_BLC	4
#define T33_DEIN_LIST_MSCA	23
#define T33_DEIN_LIST_LDC	25

struct t33_deinit {
	u32 *blc_para, *ldc_attr, *msca_attr, *msca_info;	/* per channel pointers (4 each) */
	u32 *ae_info, *fliker_info, *fliker_para;
	u8 *wdma_vinum;
	u32 *wdma_thread;
	u32 (*rd)(u32 reg);					/* system_reg_read */
	void (*wr)(u32 reg, u32 val);				/* system_reg_write */
	void (*vfree)(u32 p);					/* private_vfree */
	void (*kfree)(u32 p);					/* private_kfree */
	int (*release)(u32 ch, u32 id);				/* tisp_rcfg_module_release */
	void (*kthread_stop)(u32 task);				/* private_kthread_stop */
	u32 (*log)(u32 level, u32 line);			/* isp_printf(level, .., "tisp_all_reset", line) */
	void (*err)(u32 which, int ret);			/* printk: 0 BLC (line 478), 1 MSCA (line 1677) */
};

/* tisp_all_reset */
static inline u32 t33_all_reset(const struct t33_deinit *d)
{
	unsigned int n;

	d->wr(T33_RST_INTP0, 0xffffffffU);
	d->wr(T33_RST_INTP1, 0xffffffffU);
	d->wr(T33_RST_REQ, 1);
	for (n = T33_RST_POLLS; d->rd(T33_RST_ACK) != 1;)
		if (!--n)
			return d->log(2, 289);
	d->wr(T33_RST_CORE, T33_RST_CORE_VAL);
	for (n = T33_RST_POLLS; !d->rd(T33_RST_STATE);)
		if (!--n) {
			d->log(2, 301);
			break;
		}
	return d->log(0, 304);
}

/* tisp_blc_deinit */
static inline void t33_blc_deinit(const struct t33_deinit *d, u32 ch)
{
	int r;

	ch &= 0xff;
	if (d->blc_para[ch]) {
		d->vfree(d->blc_para[ch]);
		d->blc_para[ch] = 0;
	}
	r = d->release(ch, T33_DEIN_LIST_BLC);
	if (r < 0)
		d->err(0, r);
}

/* tisp_ldc_t33_deinit */
static inline int t33_ldc_deinit(const struct t33_deinit *d, u32 ch)
{
	ch &= 0xff;
	d->wr(T33_LDC_INT_MASK, 0xffffffffU);
	if (ch < 4 && d->ldc_attr[ch]) {
		d->kfree(d->ldc_attr[ch]);
		d->ldc_attr[ch] = 0;
	}
	return d->release(ch, T33_DEIN_LIST_LDC);
}

/* tisp_msca_t33_deinit */
static inline int t33_msca_deinit(const struct t33_deinit *d, u32 ch)
{
	int r;

	if (d->msca_attr[ch]) {
		d->kfree(d->msca_attr[ch]);
		d->msca_attr[ch] = 0;
	}
	if (d->msca_info[ch]) {
		d->kfree(d->msca_info[ch]);
		d->msca_info[ch] = 0;
	}
	r = d->release(ch & 0xff, T33_DEIN_LIST_MSCA);
	if (r < 0)
		d->err(1, r);
	return 0;
}

/* tisp_normal_ae_deinit */
static inline void t33_normal_ae_deinit(const struct t33_deinit *d, u32 ch)
{
	ch &= 0xff;
	d->ae_info[ch] = 0;
	d->fliker_info[ch] = 0;
	d->fliker_para[ch] = 0;
}

/* tisp_dma_deinit */
static inline void t33_dma_deinit(const struct t33_deinit *d)
{
	*d->wdma_vinum = 0xff;
	d->kthread_stop(*d->wdma_thread);
}

#endif /* TX_ISP_T33_DEINIT_H */
