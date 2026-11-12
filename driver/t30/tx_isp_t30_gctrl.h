/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T30 g_ctrl jump table 1 (vendor tx-isp-t30.ko .rodata+556, id 0x8000000 + idx) and the AE attribute
 * block of id 0x8000020 (vendor arm 0x8bec).  Pure helpers so a host test can check them.
 * 0x8000020: no libimp 1.0.5 call sends it; the vendor driver still serves it (a 0xf0 byte copy of tuning
 * +0x828 after three words are set to all ones).  0x800002c (arm 0x8ec8) is apical_isp_stab_g_attr
 * (MoveState, integration time).
 */
#ifndef TX_ISP_T30_GCTRL_H
#define TX_ISP_T30_GCTRL_H

#define T30_GCTRL_AE_ATTR_OFF	0x828
#define T30_GCTRL_AE_ATTR_LEN	0xf0

enum { T30_GCTRL_JT_ERR, T30_GCTRL_JT_ZERO, T30_GCTRL_JT_TUNING_3776, T30_GCTRL_JT_AE_ATTR, T30_GCTRL_JT_STAB };

static inline int t30_g_ctrl_jt(int tbl, uint32_t idx)
{
	if (tbl == 1)	/* 0x8000000 + idx */
		return (idx == 0 || idx == 3 || idx == 0x21 || idx == 0x40 || idx == 0x41) ? T30_GCTRL_JT_ZERO :
			idx == 0x20 ? T30_GCTRL_JT_AE_ATTR : idx == 0x2c ? T30_GCTRL_JT_STAB : T30_GCTRL_JT_ERR;
	if (tbl == 3)	/* 0x8000160 + idx */
		return idx == 0 ? T30_GCTRL_JT_ZERO : idx == 7 ? T30_GCTRL_JT_TUNING_3776 : T30_GCTRL_JT_ERR;
	return T30_GCTRL_JT_ERR;	/* 0x980914 + idx: unsupported or served by the wrapper */
}

/* the three words the vendor arm sets before the copy: u16 +0x828, u16 +0x82a, u32 +0x830 */
static inline void t30_gctrl_ae_attr_mark(uint8_t *tuning)
{
	uint16_t ff16 = 0xffff;
	uint32_t ff32 = 0xffffffffu;

	memcpy(tuning + 0x828, &ff16, 2);
	memcpy(tuning + 0x82a, &ff16, 2);
	memcpy(tuning + 0x830, &ff32, 4);
}

#endif /* TX_ISP_T30_GCTRL_H */
