/* SPDX-License-Identifier: MIT */
/*
 * T41 MSCA staged registers (measured on a T41LQ/gc5603, 2026-10-06).
 *
 * The per-output geometry (0xf0100 + ch * 0x100 and its crop words), the
 * scaler ratios (0xf071c..0xf0730), the global input size (0xf0084) and the
 * mirror/flip/algorithm word (0xf002c) are staged: a write only takes effect
 * at the next input frame after an update request (0xf0010 = 1), and a read
 * returns the ACTIVE value, not the staged one.  Address FIFOs, strides and
 * the output enable (0xf0008) are immediate.
 *
 * Consequences handled here, kept free of kernel state for the host test:
 *  - a channel programmed without a following update request never gets its
 *    geometry: it keeps the reset default (1920x1080 / 1280x720 / 640x480,
 *    ratio 1:1, input 1920x1080) and writes that size into buffers sized for
 *    the requested one;
 *  - a read-modify-write of 0xf002c must start from the last staged value,
 *    not from the read-back, or a still-pending flip change is lost (set then
 *    restore within one frame left the picture flipped while the read-back
 *    already said "not flipped").
 */
#ifndef TX_ISP_T41_MSCA_SHADOW_H
#define TX_ISP_T41_MSCA_SHADOW_H

#define T41_MSCA_ALGO_KEEP_MASK 0xc00001ffU   /* bits not owned by flip */

struct t41_msca_staged {
	unsigned int value;   /* last value written to 0xf002c */
	unsigned int valid;   /* 0 after MSCA init: fall back to read-back */
};

/* The value a read-modify-write of 0xf002c has to start from. */
static inline unsigned int t41_msca_staged_base(const struct t41_msca_staged *s,
						unsigned int readback)
{
	return s && s->valid ? s->value : readback;
}

static inline void t41_msca_staged_store(struct t41_msca_staged *s,
					 unsigned int value)
{
	if (!s)
		return;
	s->value = value;
	s->valid = 1;
}

/*
 * HVFLIP: computes the new 0xf002c word from the staged base and the flip
 * bits.  Returns 1 when it differs from what is staged (write it and
 * request an update), 0 when the request is a no-op (skip the update
 * request, see driver/t41/README.md "Output restart hang").
 */
static inline int t41_msca_flip_word(const struct t41_msca_staged *s,
				     unsigned int readback, unsigned int bits,
				     unsigned int *value)
{
	unsigned int base = t41_msca_staged_base(s, readback);
	unsigned int next = (base & T41_MSCA_ALGO_KEEP_MASK) |
			    (bits & ~T41_MSCA_ALGO_KEEP_MASK);

	if (value)
		*value = next;
	return next != base;
}

#endif /* TX_ISP_T41_MSCA_SHADOW_H */
