/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 block "i9C": tisp_gsm_init, tisp_dma_init, tisp_rcfg_init (openimp #99), binary oracle
 * tests/t33_binoracle/i9C.conf.  OS calls (irq table, events, register write, allocation, threads,
 * prints) are hooks; the module statics are passed in.
 */
#ifndef TX_ISP_T33_I9C_H
#define TX_ISP_T33_I9C_H

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif
#include "tx_isp_t33_blk.h"

/* ---- GSM ---------------------------------------------------------------- */
#define T33_GSM_IRQ_HIST 7
#define T33_GSM_IRQ_FRAME 63
#define T33_GSM_EV_PROCESS 16
#define T33_GSM_REG_CFG 0x1904c

struct t33_gsm_ops {
	void (*lock_init)(void);
	void (*irq_set)(u32 ch, u32 irq, void *handler);
	void (*event_cb)(u32 ch, u32 event, void *handler);
	void (*reg_write)(u32 reg, u32 val);
	void *hist, *frame, *process;
};

/* tisp_gsm_init: attr = sensor/ISP attributes (width u16 at 12, height u16 at 14) */
static inline int t33_gsm_init(const struct t33_gsm_ops *o, u32 ch, const u8 *attr, u32 *area_div,
			       u32 *area, const u8 *hist_para)
{
	u32 w = t32_tp_le16(attr + 12), h = t32_tp_le16(attr + 14), a;

	o->lock_init();
	o->lock_init();
	o->lock_init();
	o->irq_set(ch & 0xff, T33_GSM_IRQ_HIST, o->hist);
	o->irq_set(ch & 0xff, T33_GSM_IRQ_FRAME, o->frame);
	o->event_cb(ch, T33_GSM_EV_PROCESS, o->process);
	a = w * h;
	area_div[1] = 0;
	*area = a;
	area_div[0] = a ? 0x80000000u / a : 0xffffffffu;
	o->reg_write(T33_GSM_REG_CFG, t32_tp_le16(hist_para + 3072));
	return 0;
}

/* ---- DMA ---------------------------------------------------------------- */
#define T33_DMA_IRQ_WDMA 68
#define T33_DMA_IRQ_RDMA 69
#define T33_DMA_IRQ_IPC 32

/* one st_dma_cfg record (92 bytes) */
struct t33_dma_cfg {
	u32 w0;
	u32 f4;
	u32 f8;
	u32 pad12[2];
	u32 f20;
	u32 pad24;
	u32 f28;
	u32 f32;
	u32 pad36;
	u32 f40;
	u32 pad44[7];
	u32 f72;
	u32 f76;
	u32 pad80[2];
	u8 pad88[2];
	u8 f90;
	u8 pad91;
};

struct t33_dma_ops {
	void (*irq_set)(u32 ch, u32 irq, void *handler);
	u32 (*kthread)(void);
	void (*init_wq)(void);
	void (*err)(void);
	void *wdma_irq, *rdma_irq, *ipc_irq;
};

/* tisp_dma_init */
static inline u32 t33_dma_init(const struct t33_dma_ops *o, u32 ch, const u8 *attr, struct t33_dma_cfg *cfgs,
			       u8 *sensor_num, u32 *sp_wdma, u32 *snap_compl, u32 *thread)
{
	struct t33_dma_cfg *c;

	ch &= 0xff;
	*sensor_num = (u8)t33_le32(attr + 904);
	c = cfgs + ch;
	c->f72 = t32_tp_le16(attr + 12);
	c->f76 = t32_tp_le16(attr + 14);
	c->f28 = 16;
	c->f90 = attr[40];
	c->f4 = 17;
	c->f20 = 1;
	c->f32 = 1;
	c->f40 = 1;
	c->f8 = 1;
	o->irq_set(ch, T33_DMA_IRQ_WDMA, o->wdma_irq);
	o->irq_set(ch, T33_DMA_IRQ_RDMA, o->rdma_irq);
	if (ch == 0)
		o->irq_set(0, T33_DMA_IRQ_IPC, o->ipc_irq);
	sp_wdma[0] = 0;
	sp_wdma[1] = (u32)(unsigned long)&sp_wdma[1];
	sp_wdma[2] = sp_wdma[1];
	*thread = o->kthread();
	*snap_compl = 0;
	o->init_wq();
	if (*thread >= 0xfffff001u) {
		o->err();
		return *thread;
	}
	return 0;
}

/* ---- RCFG --------------------------------------------------------------- */
#define T33_RCFG_REG_TBL 0x22020
#define T33_RCFG_REG_EN 0x22024
#define T33_RCFG_REG_A 0x22018
#define T33_RCFG_REG_B 0x2201c

struct t33_rcfg_ops {
	void *(*vmalloc)(u32 n);
	void *(*kmalloc)(u32 n, u32 flags);
	void (*log)(int level);
	void (*reg_write)(u32 reg, u32 val);
};

/*
 * tisp_rcfg_init: channel 0 allocates the list table (40 bytes: list, its DMA address) and the first list
 * (256 bytes); all channels program the table registers and clear the trigger counters.
 * mode = word 904 of stIspInitPar.
 */
static inline void t33_rcfg_init(const struct t33_rcfg_ops *o, u32 ch, u32 **info, u32 mode, u32 *trig_cnt)
{
	u32 *blk;
	void *list;

	ch &= 0xff;
	if (!ch) {
		blk = o->vmalloc(40);
		*info = blk;
		if (!blk)
			o->log(2);
		list = o->kmalloc(256, 208);
		if (!list)
			o->log(2);
		memset(list, 0, 256);
		blk = *info;
		blk[0] = (u32)(unsigned long)list;
		blk[1] = (u32)(unsigned long)list + 0x80000000u;
		o->log(0);
		o->log(0);
		o->log(0);
		o->log(0);
		o->reg_write(752, mode == 1 ? 0 : 5);
	}
	o->reg_write(T33_RCFG_REG_TBL, (*info)[1]);
	o->reg_write(T33_RCFG_REG_EN, 1);
	o->reg_write(T33_RCFG_REG_A, 0xffffffffu);
	o->reg_write(T33_RCFG_REG_B, 0xffffffffu);
	memset(trig_cnt, 0, 16);
}

#endif /* TX_ISP_T33_I9C_H */
