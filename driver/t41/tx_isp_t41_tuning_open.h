/* SPDX-License-Identifier: GPL-2.0 */
/*
 * /dev/isp-m0 handle counting (pure logic, host-testable).
 *
 * Core tuning state (tuning + 148): 2 = initialised, tuning off;
 * 3 = tuning enabled (tisp_tattr allocated).  The first open in state 2
 * enables tuning, further opens share it, the last close disables it.
 */
#ifndef TX_ISP_T41_TUNING_OPEN_H
#define TX_ISP_T41_TUNING_OPEN_H

#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/types.h>
#else
#include <errno.h>
#include <stdint.h>
#endif

enum t41_tuning_open_action {
	T41_TUNING_OPEN_ENABLE = 1,	/* caller enables, then state = 3 */
	T41_TUNING_OPEN_SHARE = 2,	/* count only */
};

/* Returns an action (> 0) or a negative errno; count is not changed. */
static inline int t41_tuning_open_decide(uint32_t state, unsigned int count)
{
	if (state == 2)
		return T41_TUNING_OPEN_ENABLE;
	if (state == 3 && count)
		return T41_TUNING_OPEN_SHARE;
	return -EPERM;	/* vendor: -1 outside state 2 */
}

/* Called with the lock held after a successful open. */
static inline void t41_tuning_open_done(unsigned int *count)
{
	(*count)++;
}

/* Returns 1 when the caller must disable tuning (last handle, state 3). */
static inline int t41_tuning_release_decide(uint32_t state, unsigned int *count)
{
	if (*count)
		(*count)--;
	return !*count && state == 3;
}

#endif
