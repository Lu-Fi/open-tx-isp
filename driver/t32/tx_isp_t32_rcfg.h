/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32/T33 ISP register lists ("RCFG"): lists of register writes the ISP core
 * loads by itself.
 *
 * Format (QEMU model notes + the list the T32 vendor driver runs at stream-on,
 * dumped from guest RAM in S0 run r20):
 *   word 0             length of the list in words (not counting word 0)
 *   then commands:     bit 31 | (count - 1) << 18 | word offset of the first
 *                      register, followed by count values for consecutive
 *                      registers.
 * The table at T32_RDMA_TABLE holds one list pointer per list; the T32 driver
 * started list 0 at stream-on by writing bit 17 to 0x22014 and 0x2201c and
 * waits for 0x22014 bits 17:16 to clear.
 *
 * Decoded so far: the first commands of the vendor list follow this format
 * (0x5010 = 1, 0x5040.. 13 values); a later part (a block at 0x32020) does
 * not, so the decoder stops at the first word without bit 31 and reports
 * where.  The builder only emits the documented format.
 */
#ifndef TX_ISP_T32_RCFG_H
#define TX_ISP_T32_RCFG_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#ifndef T32_HOST_U32
#define T32_HOST_U32
typedef uint32_t u32;
#endif
#endif

#define T32_RCFG_CMD			(1U << 31)
#define T32_RCFG_COUNT_SHIFT		18
#define T32_RCFG_COUNT_MAX		0x2000U		/* 13-bit count - 1 */
#define T32_RCFG_OFFSET_MASK		0x3ffffU	/* word offset */

struct t32_rcfg_list {
	u32 *words;		/* words[0] = length */
	u32 cap;		/* capacity in words, including words[0] */
	u32 cmd;		/* index of the open command, 0 = none */
	int overflow;
};

static inline void t32_rcfg_init(struct t32_rcfg_list *l, u32 *buf, u32 cap)
{
	l->words = buf;
	l->cap = cap;
	l->cmd = 0;
	l->overflow = cap < 1;
	if (cap)
		buf[0] = 0;
}

static inline u32 t32_rcfg_cmd_count(u32 cmd)
{
	return ((cmd >> T32_RCFG_COUNT_SHIFT) & (T32_RCFG_COUNT_MAX - 1)) + 1;
}

static inline u32 t32_rcfg_cmd_offset(u32 cmd)
{
	return (cmd & T32_RCFG_OFFSET_MASK) << 2;
}

/*
 * Append one register write.  Consecutive registers extend the open command,
 * anything else opens a new one.
 */
static inline void t32_rcfg_add(struct t32_rcfg_list *l, u32 reg, u32 val)
{
	u32 n = l->words[0], c;

	if (l->overflow)
		return;
	if (l->cmd) {
		c = l->words[l->cmd];
		if (t32_rcfg_cmd_offset(c) + 4 * t32_rcfg_cmd_count(c) == reg &&
		    t32_rcfg_cmd_count(c) < T32_RCFG_COUNT_MAX && n + 2 <= l->cap) {
			l->words[l->cmd] = c + (1U << T32_RCFG_COUNT_SHIFT);
			l->words[n + 1] = val;
			l->words[0] = n + 1;
			return;
		}
	}
	if (n + 3 > l->cap || (reg & 3) || (reg >> 2) > T32_RCFG_OFFSET_MASK) {
		l->overflow = 1;
		return;
	}
	l->cmd = n + 1;
	l->words[n + 1] = T32_RCFG_CMD | (reg >> 2);
	l->words[n + 2] = val;
	l->words[0] = n + 2;
}

/*
 * Walk a list.  Calls fn(reg, val, priv) for every write and returns the
 * number of words consumed (excluding words[0]); if a word inside the list is
 * not a command, *stop is set to its index (0 otherwise).
 */
static inline u32 t32_rcfg_walk(const u32 *w, u32 avail,
				void (*fn)(u32 reg, u32 val, void *priv),
				void *priv, u32 *stop)
{
	u32 len = w[0], i = 1, k, cnt, off;

	*stop = 0;
	if (len + 1 > avail)
		len = avail - 1;
	while (i <= len) {
		if (!(w[i] & T32_RCFG_CMD)) {
			*stop = i;
			break;
		}
		cnt = t32_rcfg_cmd_count(w[i]);
		off = t32_rcfg_cmd_offset(w[i]);
		for (k = 0; k < cnt && i + 1 + k <= len; k++)
			if (fn)
				fn(off + 4 * k, w[i + 1 + k], priv);
		i += 1 + cnt;
	}
	return i - 1;
}

#endif /* TX_ISP_T32_RCFG_H */
