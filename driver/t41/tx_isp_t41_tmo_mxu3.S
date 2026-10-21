/* SPDX-License-Identifier: GPL-2.0 */
/*
 * MXU3 (XBurst2 SIMD512) kernel for the T41 local TMO map spatial filter.
 *
 * Written from the XBurst ISA MXU3 Programming Manual 1.0. binutils does not
 * know MXU3 mnemonics, so every instruction is emitted as .word by a macro
 * below; each macro documents its encoding. The register file has 32
 * 512-bit vector registers (vr0..vr31), 16 32-bit lanes each on T41.
 *
 * Callers must bracket calls with kernel_mxuv3_begin_partial() and
 * kernel_mxuv3_end() (process context, preemption off).
 *
 * void t41_tmo_mxu3_chunk(const struct t41_tmo_mxu3_job *job);
 *
 *   struct t41_tmo_mxu3_job {			offset
 *	const unsigned int *c;			 0  counts, lane 0 of the chunk
 *	const unsigned int *l;			 4  luma, same layout
 *	const int *offset;			 8  byte offset per term
 *	const unsigned int *weight;		12  16 weights per term (64 B)
 *	unsigned int terms;			16
 *	unsigned int extra;			20  0, or 2: shift each luma product
 *	unsigned int *out;			24  n[16] then s[16]
 *   };
 *
 * For every term t and lane i:
 *	n[i] += weight[t][i] * c[offset[t]/4 + i]
 *	s[i] += (weight[t][i] * l[offset[t]/4 + i]) >> extra
 * all in 32-bit unsigned arithmetic with wrap-around, exactly as the C
 * reference t41_tmo_map() sums them.
 */

#include <asm/asm.h>
#include <asm/regdef.h>

/* GPR numbers used inside the .word encodings. */
#define R_T0	8
#define R_T1	9
#define R_T2	10
#define R_T3	11
#define R_T4	12
#define R_T5	13
#define R_T6	14
#define R_T7	15
#define R_T8	24
#define R_T9	25

/*
 * LAO vrd[n], off*32(base): load 32 bytes (no alignment restriction) into
 * the n-th 256-bit half of vrd.  SPECIAL2 (manual 3.15.2):
 *   011100 | base(5) | offset(5) | fmt_n(5)=n,n,0,1,1 | vrd(5) | 01000 | fmt=1
 */
#define LAO(vrd, n, off, base) \
	.word (0x1c << 26) | ((base) << 21) | (((off) & 0x1f) << 16) | \
	      ((((n) << 3) | 0x3) << 11) | ((vrd) << 6) | (0x08 << 1) | 1

/*
 * SAO vrp[n], off*32(base): store the n-th 256-bit half of vrp.
 * SPECIAL2 (manual 3.15.4):
 *   011100 | base(5) | offset(5) | vrp(5) | fmt_n(5)=n,n,0,1,1 | 01010 | fmt=1
 */
#define SAO(vrp, n, off, base) \
	.word (0x1c << 26) | ((base) << 21) | (((off) & 0x1f) << 16) | \
	      ((vrp) << 11) | ((((n) << 3) | 0x3) << 6) | (0x0a << 1) | 1

/*
 * ADDW vrd, vrs, vrp: 32-bit lane add.  COP2 (manual 3.7.1):
 *   010010 | 10100 | vrs | vrp | vrd | 0000 | fmt=10
 */
#define ADDW(vrd, vrs, vrp) \
	.word (0x12 << 26) | (0x14 << 21) | ((vrs) << 16) | ((vrp) << 11) | \
	      ((vrd) << 6) | (0x0 << 2) | 2

/*
 * MULW vrd, vrs, vrp: 32-bit lane multiply, low 32 bits kept.
 * COP2 (manual 3.7.13):
 *   010010 | 10011 | vrs | vrp | vrd | 1000 | fmt=10
 */
#define MULW(vrd, vrs, vrp) \
	.word (0x12 << 26) | (0x13 << 21) | ((vrs) << 16) | ((vrp) << 11) | \
	      ((vrd) << 6) | (0x8 << 2) | 2

/*
 * SRLIW vrd, vrs, imm: 32-bit lane logical shift right.
 * COP2 (manual 3.12.8):
 *   010010 | 10101 | vrs | imm | vrd | 1100 | fmt=10
 */
#define SRLIW(vrd, vrs, imm) \
	.word (0x12 << 26) | (0x15 << 21) | ((vrs) << 16) | (((imm) & 0x1f) << 11) | \
	      ((vrd) << 6) | (0xc << 2) | 2

/*
 * XORV vrd, vrs, vrp: bitwise XOR of the whole register (used to zero).
 * COP2 (manual 3.8.7):
 *   010010 | 10011 | vrs | vrp | vrd | 000110
 */
#define XORV(vrd, vrs, vrp) \
	.word (0x12 << 26) | (0x13 << 21) | ((vrs) << 16) | ((vrp) << 11) | \
	      ((vrd) << 6) | 0x06

/* Vector registers: vr0 = n, vr1 = s, vr2 = weights, vr3 = c, vr4 = l,
 * vr5/vr6 = products. */

	.text
	.set	push
	.set	noreorder
	.set	noat

LEAF(t41_tmo_mxu3_chunk)
	lw	t0, 0(a0)		/* c */
	lw	t1, 4(a0)		/* l */
	lw	t2, 8(a0)		/* offset[] */
	lw	t3, 12(a0)		/* weight[] */
	lw	t4, 16(a0)		/* terms */
	lw	t5, 20(a0)		/* extra */
	lw	t6, 24(a0)		/* out */
	XORV(0, 0, 0)			/* n = 0 */
	XORV(1, 1, 1)			/* s = 0 */
	beqz	t4, 3f
	 nop
	bnez	t5, 2f
	 nop

1:	/* extra == 0 */
	lw	t7, 0(t2)
	addiu	t2, t2, 4
	addu	t8, t0, t7
	addu	t9, t1, t7
	LAO(2, 0, 0, R_T3)
	LAO(2, 1, 1, R_T3)
	LAO(3, 0, 0, R_T8)
	LAO(3, 1, 1, R_T8)
	LAO(4, 0, 0, R_T9)
	LAO(4, 1, 1, R_T9)
	MULW(5, 2, 3)
	MULW(6, 2, 4)
	ADDW(0, 0, 5)
	ADDW(1, 1, 6)
	addiu	t4, t4, -1
	bnez	t4, 1b
	 addiu	t3, t3, 64
	b	3f
	 nop

2:	/* extra == 2: shift every luma product before summing */
	lw	t7, 0(t2)
	addiu	t2, t2, 4
	addu	t8, t0, t7
	addu	t9, t1, t7
	LAO(2, 0, 0, R_T3)
	LAO(2, 1, 1, R_T3)
	LAO(3, 0, 0, R_T8)
	LAO(3, 1, 1, R_T8)
	LAO(4, 0, 0, R_T9)
	LAO(4, 1, 1, R_T9)
	MULW(5, 2, 3)
	MULW(6, 2, 4)
	SRLIW(6, 6, 2)
	ADDW(0, 0, 5)
	ADDW(1, 1, 6)
	addiu	t4, t4, -1
	bnez	t4, 2b
	 addiu	t3, t3, 64

3:	SAO(0, 0, 0, R_T6)
	SAO(0, 1, 1, R_T6)
	SAO(1, 0, 2, R_T6)
	SAO(1, 1, 3, R_T6)
	jr	ra
	 nop
	END(t41_tmo_mxu3_chunk)

	.set	pop
