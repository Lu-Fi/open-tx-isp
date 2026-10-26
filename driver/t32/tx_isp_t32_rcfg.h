/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32/T33 ISP register lists ("RCFG"): lists of register writes the ISP core
 * loads by itself.
 *
 * Format (QEMU model notes + the list the T32 vendor driver runs at stream-on,
 * dumped from guest RAM in S0 run r20 and decoded completely):
 *   word 0             length of the list in words (not counting word 0)
 *   then commands, each a header and count values:
 *     bit 31 set:      (count - 1) << 18 | word offset of the first register;
 *                      the values go to consecutive registers
 *     bit 31 clear:    the same fields; all values go to the one register
 *                      (a table port such as 0x32024 behind its select
 *                      register 0x32020)
 * The vendor's list: 0x5010 = 1, 13 values at 0x5040, 0x32020 = 0x101, 3456
 * values into the port 0x32024, 0x32020 = 0, 0x2f4 = 0 (3479 words).  Its
 * values come from the IQ bin through tisp (S4); the format, the table and
 * the trigger are what S3 implements.
 *
 * Table: T32_RDMA_TABLE holds the physical address of a table of 64 list
 * pointers.  Writing bit N of T32_RDMA_TRIG (0x22014) starts the list in
 * table word 32 + N; the T32 vendor driver puts its stream-on list in word
 * 49 (and word 0), starts it with bit 17 in 0x22014 and 0x2201c and reads
 * 0x22014 back until bits 17:16 clear.
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
#define T32_RCFG_TABLE_WORDS		64
#define T32_RCFG_STREAM_ON_LIST		49		/* table word of the stream-on list */
#define T32_RCFG_STREAM_ON_BIT		(1U << (T32_RCFG_STREAM_ON_LIST - 32))

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
 * Append n writes to one register (a table port): its own command, bit 31
 * clear.
 */
static inline void t32_rcfg_add_port(struct t32_rcfg_list *l, u32 reg,
				     const u32 *vals, u32 n)
{
	u32 len = l->words[0], k;

	if (l->overflow || !n)
		return;
	if (n > T32_RCFG_COUNT_MAX || len + 1 + n + 1 > l->cap || (reg & 3) ||
	    (reg >> 2) > T32_RCFG_OFFSET_MASK) {
		l->overflow = 1;
		return;
	}
	l->words[len + 1] = ((n - 1) << T32_RCFG_COUNT_SHIFT) | (reg >> 2);
	for (k = 0; k < n; k++)
		l->words[len + 2 + k] = vals[k];
	l->words[0] = len + 1 + n;
	l->cmd = 0;		/* nothing extends a port command */
}

/*
 * Walk a list.  Calls fn(reg, val, priv) for every write and returns the
 * number of words consumed (excluding words[0]); *stop is set to the index
 * of a command that runs past the end of the list (0 when the list is whole).
 */
static inline u32 t32_rcfg_walk(const u32 *w, u32 avail,
				void (*fn)(u32 reg, u32 val, void *priv),
				void *priv, u32 *stop)
{
	u32 len = w[0], i = 1, k, cnt, off, inc;

	*stop = 0;
	if (len + 1 > avail)
		len = avail - 1;
	while (i <= len) {
		cnt = t32_rcfg_cmd_count(w[i]);
		off = t32_rcfg_cmd_offset(w[i]);
		inc = (w[i] & T32_RCFG_CMD) ? 4 : 0;
		if (i + cnt > len) {
			*stop = i;
			break;
		}
		for (k = 0; k < cnt; k++)
			if (fn)
				fn(off + inc * k, w[i + 1 + k], priv);
		i += 1 + cnt;
	}
	return i - 1;
}

#endif /* TX_ISP_T32_RCFG_H */
