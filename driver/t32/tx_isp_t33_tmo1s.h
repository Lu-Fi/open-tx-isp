/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) TMO one-shot / AOV / multi-sensor V-table glue (openimp #99),
 * binary oracle block "tmo1s" (tests/t33_binoracle/tmo1s.conf):
 *
 *  - tisp_tmo_oneshot_enable / _disable / _tick (lib_tisp_tmo_oneshot_*,
 *    from the core ioctls 0x100000f / 0x100000e / 0x1000010 of the running
 *    mode switch = IMP_ISP_OneShotEnable / Disable / Tick): the TMO V-ram
 *    word 0x70dc (slot 0x58) on / off, list 8 restarted (single sensor) or
 *    the second channel's slot (two sensors); disable also shortens the list
 *    again (tisp_tmo_rcfg_reg_num_disV);
 *  - tisp_tmo_sleep / tisp_tmo_wake_up (lib_tisp_aov_sleep / _wake_up):
 *    the AOV tick flag, at wake-up the V table again (single sensor:
 *    tisp_tmo_ramV_vi0_reg_refresh, else the long list,
 *    tisp_tmo_rcfg_reg_num_enV, and the multi-sensor V-ram path);
 *  - tisp_tmo_ramV_vi23_reg_ctrl / _refresh: the multi-sensor (stIspInitPar
 *    byte 904 >= 2) V-ram control words and the 1500-word V table through
 *    the list (slots 199.., the 0x32260 port start in slot 0x6a4);
 *  - tisp_tmo_dma_interrupt (system irq 11): returns 1.
 *
 * Vendor globals: tmoCfgMode, aovTikFlag, mulsensornum,
 * tisp_t33_tmo_rcfg_reg_num, tmoInfo[] (block state, bytes 0x2a50 V table
 * pointer, 0x2a54 its DMA address, 0x2a5d "started"), stModuleAttr (list
 * page pointer at +0xe8 of each 0x700-byte channel record).  The includer
 * passes them in struct t33_tmo1s.  The last list writes are tail calls of
 * tisp_reg_write_t33 (the decompiler inlines them as stores); its
 * "slot == 0x3fff" print can never fire here (constant slots).
 */
#ifndef TX_ISP_T33_TMO1S_H
#define TX_ISP_T33_TMO1S_H

#include "tx_isp_t33_blk.h"

#define T33_TMO1S_MOD		8
#define T33_TMO1S_LIST_SHORT	0xc4U	/* tisp_tmo_rcfg_reg_num_disV */
#define T33_TMO1S_LIST_LONG	0x6a5U	/* tisp_tmo_rcfg_reg_num_enV */
#define T33_TMO1S_V_WORDS	1500U

struct t33_tmo1s {
	u8 *cfg_mode;			/* tmoCfgMode: slot indexes instead of addresses */
	u8 *aov_tik;			/* aovTikFlag */
	u8 *mulsensor;			/* mulsensornum */
	u16 *reg_num;			/* tisp_t33_tmo_rcfg_reg_num (.data u16) */
	const u8 *initpar;		/* stIspInitPar (LE u32 at byte 904: sensor mode) */
	u8 *(*info)(u32 ch);		/* tmoInfo[ch] */
	u32 *(*list)(u32 ch);		/* *(stModuleAttr + ch * 0x700 + 0xe8): the list page (word 0 = length) */
	void (*wr)(u32 ch, u32 mod, u32 reg, u32 val);	/* tisp_reg_write_t33 */
	void (*trig)(u32 ch, u32 mod);			/* tisp_rcfg_module_cfg_trig */
	void (*vi0_refresh)(u32 ch);			/* tisp_tmo_ramV_vi0_reg_refresh */
};

#define T33_TMO1S_R(t, slot, addr)	(*(t)->cfg_mode ? (slot) : (addr))

static inline u32 t33_tmo1s_mode(const struct t33_tmo1s *t)
{
	return t33_le32(t->initpar + 904);
}

/* tisp_tmo_rcfg_reg_num_disV / _enV */
static inline int t33_tmo1s_reg_num(const struct t33_tmo1s *t, u32 ch, u32 n)
{
	*t->reg_num = n;
	t->list(ch & 0xff)[0] = n;
	return 0;
}

/* tisp_tmo_oneshot_disable */
static inline void t33_tmo1s_disable(const struct t33_tmo1s *t)
{
	if (t33_tmo1s_mode(t) == 1) {
		t->wr(0, T33_TMO1S_MOD, T33_TMO1S_R(t, 0x58, 0x70dc), 0);
		t33_tmo1s_reg_num(t, 0, T33_TMO1S_LIST_SHORT);
		t->trig(0, T33_TMO1S_MOD);
	}
	if (t33_tmo1s_mode(t) == 2) {
		t33_tmo1s_reg_num(t, 0, T33_TMO1S_LIST_SHORT);
		t33_tmo1s_reg_num(t, 1, T33_TMO1S_LIST_SHORT);
		t->wr(0, T33_TMO1S_MOD, T33_TMO1S_R(t, 0x58, 0x70dc), 0);
		t->wr(1, T33_TMO1S_MOD, T33_TMO1S_R(t, 0x58, 0x70dc), 0);
	}
	*t->aov_tik = 0;
}

/* tisp_tmo_oneshot_enable */
static inline void t33_tmo1s_enable(const struct t33_tmo1s *t)
{
	if (t33_tmo1s_mode(t) == 1) {
		t->wr(0, T33_TMO1S_MOD, T33_TMO1S_R(t, 0x58, 0x70dc), 1);
		t->trig(0, T33_TMO1S_MOD);
	}
	if (t33_tmo1s_mode(t) != 2)
		return;
	t->wr(0, T33_TMO1S_MOD, T33_TMO1S_R(t, 0x58, 0x70dc), 1);
	t->wr(1, T33_TMO1S_MOD, T33_TMO1S_R(t, 0x58, 0x70dc), 0x11);
}

/* tisp_tmo_oneshot_tick */
static inline void t33_tmo1s_tick(const struct t33_tmo1s *t)
{
	if (t33_tmo1s_mode(t) == 2)
		*t->mulsensor = 0;
}

/* tisp_tmo_sleep */
static inline void t33_tmo1s_sleep(const struct t33_tmo1s *t)
{
	*t->aov_tik = 0;
}

/* tisp_tmo_dma_interrupt */
static inline int t33_tmo1s_dma_irq(void)
{
	return 1;
}

/* tisp_tmo_ramV_vi23_reg_ctrl */
static inline void t33_tmo1s_vi23_ctrl(const struct t33_tmo1s *t, u32 ch)
{
	u8 ms;

	if (!t->info(ch)[0x2a5d])
		t->wr(ch, T33_TMO1S_MOD, T33_TMO1S_R(t, 0x20, 0x70f0), 1);
	t->wr(ch, T33_TMO1S_MOD, T33_TMO1S_R(t, 0x22, 0x70f8), *t->mulsensor);
	t->wr(ch, T33_TMO1S_MOD, T33_TMO1S_R(t, 0x1f, 0x70ec), *t->mulsensor);
	if (t33_tmo1s_mode(t) > 2 || *t->aov_tik == 1)
		t->wr(ch, T33_TMO1S_MOD, T33_TMO1S_R(t, 0x58, 0x70dc), *t->mulsensor ? 0x11 : 1);
	ms = *t->mulsensor;
	*t->mulsensor = ms != 1;
	if (t33_tmo1s_mode(t) < 2 || t->info(ch)[0x2a5d])
		return;
	t->wr(ch, T33_TMO1S_MOD, T33_TMO1S_R(t, 0x59, 0x70e0), t33_le32(t->info(ch) + 0x2a54));
}

/* tisp_tmo_ramV_vi23_reg_refresh: the V table (tmoInfo +0x2a50) word k into slot / register base + k */
static inline void t33_tmo1s_vi23_refresh(const struct t33_tmo1s *t, u32 ch)
{
	u32 base = T33_TMO1S_R(t, 199U, 0x32264U), k;
	const u8 *v = (const u8 *)(uintptr_t)t33_le32(t->info(ch) + 0x2a50);

	t->wr(ch, T33_TMO1S_MOD, T33_TMO1S_R(t, 0xc5, 0x32260), 0x101);
	for (k = 0; k < T33_TMO1S_V_WORDS; k++)
		t->wr(ch, T33_TMO1S_MOD, base + k, t33_le32(v + 4 * k));
	t->wr(ch, T33_TMO1S_MOD, T33_TMO1S_R(t, 0x6a4, 0x32260), 0x10102);
}

/* tisp_tmo_wake_up */
static inline void t33_tmo1s_wake_up(const struct t33_tmo1s *t, u32 ch)
{
	u32 m = t33_tmo1s_mode(t);

	*t->aov_tik = 1;
	if (m > 2)
		return;
	t->info(ch)[0x2a5d] = 0;
	if (m == 1) {
		t->vi0_refresh(ch);
		return;
	}
	t33_tmo1s_reg_num(t, ch & 0xff, T33_TMO1S_LIST_LONG);
	t33_tmo1s_vi23_ctrl(t, ch);
	t33_tmo1s_vi23_refresh(t, ch);
}

#endif /* TX_ISP_T33_TMO1S_H */
