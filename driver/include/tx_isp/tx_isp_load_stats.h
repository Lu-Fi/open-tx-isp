/* SPDX-License-Identifier: GPL-2.0 */
#ifndef TX_ISP_LOAD_STATS_H
#define TX_ISP_LOAD_STATS_H

/*
 * Optional per-frame load statistics shared by the per-SoC drivers.
 *
 * Build with TX_ISP_LOAD_STATS=y (the SoC Kbuild then adds
 * -DTX_ISP_LOAD_STATS). Without it every helper below compiles to nothing,
 * so release builds carry no cost.
 *
 * Exactly one translation unit per module defines the storage and the
 * module parameters:
 *
 *	#define TX_LS_STORAGE
 *	#define TX_LS_PREFIX	t31_
 *	#define TX_LS_ISR_WORDS	2
 *	#define TX_LS_WORKS	3
 *	#include "../include/tx_isp/tx_isp_load_stats.h"
 *
 * Other files only include the header (TX_LS_ISR_WORDS/TX_LS_WORKS are not
 * needed there). Parameters, all under /sys/module/<module>/parameters/:
 *
 *	<prefix>load_stats	1 = count (default), 0 = off at runtime
 *	<prefix>isr_bits[]	hits per ISR status bit, word w bit b at w*32+b
 *	<prefix>isr_calls[2]	ISR entries, entries with no status bit set
 *	<prefix>work_count[]	runs per work item
 *	<prefix>work_us[]	summed run time in microseconds (wraps)
 *	<prefix>work_max_us[]	longest run in microseconds
 *
 * The SoC file documents which status word and which work item each index
 * stands for. The arrays live in .data: several recovered drivers still
 * address legacy BSS objects by relative layout, so new zero-filled BSS can
 * perturb them.
 */

#include <linux/types.h>
#include <linux/compiler.h>

#ifndef READ_ONCE	/* 3.10 kernels only have ACCESS_ONCE */
#define READ_ONCE(x) ACCESS_ONCE(x)
#endif

#ifdef TX_ISP_LOAD_STATS

#include <linux/ktime.h>
#include <linux/hrtimer.h>	/* ktime_get() lives here on 3.10 */
#include <linux/moduleparam.h>
#include <linux/bitops.h>
#include <asm/div64.h>

#define TX_LS_DATA __attribute__((section(".data")))

extern int tx_ls_enable;
extern unsigned int tx_ls_isr_bits[];
extern unsigned int tx_ls_isr_bits_max;
extern unsigned int tx_ls_isr_calls[2];
extern unsigned int tx_ls_work_count[];
extern unsigned int tx_ls_work_us[];
extern unsigned int tx_ls_work_max_us[];
extern unsigned int tx_ls_works_max;

#ifdef TX_LS_STORAGE
#define __TX_LS_NAME(prefix, name) prefix##name
#define _TX_LS_NAME(prefix, name) __TX_LS_NAME(prefix, name)
#define TX_LS_NAME(name) _TX_LS_NAME(TX_LS_PREFIX, name)
/* One more level so TX_LS_NAME() is expanded before the kernel macros paste it. */
#define TX_LS_PARAM(name, value, type, perm) \
	module_param_named(name, value, type, perm)
#define TX_LS_PARAM_ARRAY(name, array, type, nump, perm) \
	module_param_array_named(name, array, type, nump, perm)
#define TX_LS_PARAM_DESC(name, desc) MODULE_PARM_DESC(name, desc)

int tx_ls_enable TX_LS_DATA = 1;
unsigned int tx_ls_isr_bits[TX_LS_ISR_WORDS * 32] TX_LS_DATA;
unsigned int tx_ls_isr_bits_max TX_LS_DATA = TX_LS_ISR_WORDS * 32;
unsigned int tx_ls_isr_calls[2] TX_LS_DATA;
unsigned int tx_ls_work_count[TX_LS_WORKS] TX_LS_DATA;
unsigned int tx_ls_work_us[TX_LS_WORKS] TX_LS_DATA;
unsigned int tx_ls_work_max_us[TX_LS_WORKS] TX_LS_DATA;
unsigned int tx_ls_works_max TX_LS_DATA = TX_LS_WORKS;
static unsigned int tx_ls_isr_bits_num TX_LS_DATA = TX_LS_ISR_WORDS * 32;
static unsigned int tx_ls_isr_calls_num TX_LS_DATA = 2;
static unsigned int tx_ls_works_num TX_LS_DATA = TX_LS_WORKS;

TX_LS_PARAM(TX_LS_NAME(load_stats), tx_ls_enable, int, 0644);
TX_LS_PARAM_DESC(TX_LS_NAME(load_stats),
		 "count ISR status bits and time per-frame work (1=on)");
TX_LS_PARAM_ARRAY(TX_LS_NAME(isr_bits), tx_ls_isr_bits, uint,
			 &tx_ls_isr_bits_num, 0444);
TX_LS_PARAM_ARRAY(TX_LS_NAME(isr_calls), tx_ls_isr_calls, uint,
			 &tx_ls_isr_calls_num, 0444);
TX_LS_PARAM_ARRAY(TX_LS_NAME(work_count), tx_ls_work_count, uint,
			 &tx_ls_works_num, 0444);
TX_LS_PARAM_ARRAY(TX_LS_NAME(work_us), tx_ls_work_us, uint,
			 &tx_ls_works_num, 0444);
TX_LS_PARAM_ARRAY(TX_LS_NAME(work_max_us), tx_ls_work_max_us, uint,
			 &tx_ls_works_num, 0444);
#endif /* TX_LS_STORAGE */

/* Count one ISR entry; words[] are the status words the ISR read. */
static inline void tx_ls_isr(const u32 *words, unsigned int count)
{
	unsigned int w, any = 0;

	if (READ_ONCE(tx_ls_enable) <= 0)
		return;
	tx_ls_isr_calls[0]++;
	for (w = 0; w < count && (w + 1) * 32 <= tx_ls_isr_bits_max; ++w) {
		u32 word = words[w];

		any |= word;
		while (word) {
			tx_ls_isr_bits[w * 32 + __ffs(word)]++;
			word &= word - 1;
		}
	}
	if (!any)
		tx_ls_isr_calls[1]++;
}

static inline u64 tx_ls_work_begin(void)
{
	return READ_ONCE(tx_ls_enable) > 0 ? ktime_to_ns(ktime_get()) : 0;
}

static inline void tx_ls_work_end(unsigned int id, u64 start)
{
	u64 ns;
	unsigned int us;

	if (!start || id >= tx_ls_works_max)
		return;
	ns = ktime_to_ns(ktime_get()) - start;
	do_div(ns, 1000);
	us = (unsigned int)ns;
	tx_ls_work_count[id]++;
	tx_ls_work_us[id] += us;
	if (us > tx_ls_work_max_us[id])
		tx_ls_work_max_us[id] = us;
}

#else /* !TX_ISP_LOAD_STATS */

static inline void tx_ls_isr(const u32 *words, unsigned int count) { }
static inline u64 tx_ls_work_begin(void) { return 0; }
static inline void tx_ls_work_end(unsigned int id, u64 start) { }

#endif /* TX_ISP_LOAD_STATS */

/* Time a void work function under index id. */
#define TX_LS_TIMED_WORK(wrapper, fn, id)				\
	static void wrapper(struct work_struct *work)			\
	{								\
		u64 tx_ls_start = tx_ls_work_begin();			\
									\
		fn(work);						\
		tx_ls_work_end(id, tx_ls_start);			\
	}

#endif /* TX_ISP_LOAD_STATS_H */
