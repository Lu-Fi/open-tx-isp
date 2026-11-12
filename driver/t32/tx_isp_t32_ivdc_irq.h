/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 IVDC: interrupt service routine from the vendor module (Matt Davis'
 * recovered C, PR #15): ivdc_core_interrupt_service_routine (0xe234).
 *
 * The status register 68 of the IVDC (subdev +0x118 -> private data, its
 * register base +0x11c) is read and written back to the clear register 84;
 * every set status bit except 10 and 22 counts up one word of the counter
 * block d98bc..d9930 (bit 0 -> d992c, ... see t32_ivdc_irq_slot).  Bit 17
 * (frame done) also sends the frame-done event 0x3000006 to the pad
 * (subdev +0x110) + 36 * channel of the channel register 240 bits 0..2,
 * with the channel's fps, CSC-CR output and full-range flag; with d989c
 * set the event's word +32 is 8.  Returns 1 (IRQ_HANDLED).
 *
 * The counter words d98e8..d98f4 (bits 19..16) are the same words that
 * struct t32_ivdc keeps as save_src[].
 * Vendor: the event fields not named here are uninitialised stack bytes
 * (ours: 0).
 *
 * Checked bit-exact against the vendor flow by tests/t32_oracle
 * (t32_ivdci_oracle).
 */
#ifndef TX_ISP_T32_IVDC_IRQ_H
#define TX_ISP_T32_IVDC_IRQ_H

#include "tx_isp_t32_ivdc.h"

#define T32_IVDCI_SD_PADS	0x110U
#define T32_IVDCI_PAD_BYTES	36U
#define T32_IVDCI_REG_STATUS	68U
#define T32_IVDCI_REG_CLEAR	84U
#define T32_IVDCI_REG_CHAN	240U
#define T32_IVDCI_BIT_DONE	17U
#define T32_IVDCI_EV_DONE	0x3000006U
#define T32_IVDCI_EV_BYTES	56U
#define T32_IVDCI_COUNTERS	30U	/* d98bc .. d9930 */

struct t32_ivdci_os {
	void (*get_fps)(u32 ch, u32 *fps);		/* tisp_get_fps */
	u32 (*csccr_output)(u32 ch);			/* lib_tisp_api_get_csccr_output */
	u32 (*full_range)(u32 ch);			/* tisp_csc_get_full_range */
	int (*send_event)(u32 pad, u32 cmd, u8 *data);	/* tx_isp_send_event_to_remote */
};

/* counter word (index into d98bc..) of a status bit, -1: not counted */
static inline int t32_ivdc_irq_slot(u32 bit)
{
	static const signed char slot[32] = {
		28, 29, 26, 27, 25, 24, 23, 22, 21, 20, -1, 19, 18, 17, 16, 15,
		14, 13, 12, 11, 10, 9, -1, 8, 7, 6, 5, 4, 3, 2, 1, 0,
	};

	return slot[bit];
}

static inline int t32_ivdc_core_interrupt_service_routine(const struct t32_ivdci_os *os, u32 *cnt,
							  const u32 *d989c, u8 *sd)
{
	u8 *priv = T32_PTR(t32_g_rd32(sd, T32_IVDC_SD_PRIV)), ev[T32_IVDCI_EV_BYTES];
	u32 st = *t32_ivdc_reg(priv, T32_IVDCI_REG_STATUS), bit, ch, fps;
	int k;

	*t32_ivdc_reg(priv, T32_IVDCI_REG_CLEAR) = st;
	for (bit = 0; bit < 32; bit++) {
		if (!(st >> bit & 1))
			continue;
		if (bit == T32_IVDCI_BIT_DONE) {
			ch = *t32_ivdc_reg(priv, T32_IVDCI_REG_CHAN) & 7;
			memset(ev, 0, sizeof(ev));
			t32_g_wr32(ev, 8, 0x80000000U);
			t32_g_wr32(ev, 12, 0x81000000U);
			if (*d989c)
				t32_g_wr32(ev, 32, 8);
			os->get_fps(ch, &fps);
			t32_g_wr32(ev, 24, fps);
			t32_g_wr32(ev, 28, os->csccr_output(ch));
			t32_g_wr32(ev, 48, os->full_range(ch));
			os->send_event(ch * T32_IVDCI_PAD_BYTES + t32_g_rd32(sd, T32_IVDCI_SD_PADS), T32_IVDCI_EV_DONE, ev);
		}
		k = t32_ivdc_irq_slot(bit);
		if (k >= 0)
			cnt[k]++;
	}
	return 1;
}

#endif
