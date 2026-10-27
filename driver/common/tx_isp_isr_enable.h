/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ISP core interrupt enable word (register 0xb0) shared by the stream
 * enable/disable path and the run-time module parameter (t23_isr_enable,
 * t31_isr_enable).  The parameter used to write 0xb0 after checking "is the
 * core interrupt on?" outside any lock, so a write racing with the stream
 * disable could switch the sources back on after the driver had cleared 0xb0
 * (issue 108).  Both paths now decide under one spinlock, held by the caller;
 * these helpers only keep the state and return what to write.
 */
#ifndef TX_ISP_ISR_ENABLE_H
#define TX_ISP_ISR_ENABLE_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdbool.h>
#include <stdint.h>
typedef uint32_t u32;
#endif

struct tx_isp_isr_word {
	bool hw_on;		/* 0xb0 currently holds the enable word, not 0 */
};

/* Stream path: returns the value to write to 0xb0. */
static inline u32 tx_isp_isr_word_switch(struct tx_isp_isr_word *w,
					 bool on, u32 enable_word)
{
	w->hw_on = on;
	return on ? enable_word : 0;
}

/* Parameter path: true (and *reg the value) when 0xb0 has to be rewritten,
 * false while the core interrupts are off. */
static inline bool tx_isp_isr_word_update(const struct tx_isp_isr_word *w,
					  u32 enable_word, u32 *reg)
{
	if (!w->hw_on)
		return false;
	*reg = enable_word;
	return true;
}

#endif
