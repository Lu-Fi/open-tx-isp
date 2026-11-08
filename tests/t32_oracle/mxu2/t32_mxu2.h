/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Host model of the MXU2 instructions the T32 isp module uses, written from the
 * pseudo-code of the Ingenic XBurst MXU2 programming manual (MXU2_PM, section 3 and
 * Appendix A), not from Matt Davis' translation.  One routine per mnemonic family; the
 * element size is a parameter (1, 2, 4, 8 bytes).  Registers: 32 x 128 bit vector regs
 * (vr0..vr31 in the manual's VPR), the MIPS GPRs are the caller's.
 *
 * Where the manual is silent the choice is marked ASSUMPTION and checked by
 * mxu2_check against the module's real use:
 *  - DIVU by zero is not defined (not tested, 0xffffffff like the host's translation);
 *  - the SHUFV selector uses bit 7 of the byte as "zero" flag (the manual writes
 *    sel[esize]);
 *  - the 10-bit offset of LU1Q/SU1Q is added unscaled, as the manual's address
 *    formula says.
 */
#ifndef T32_MXU2_H
#define T32_MXU2_H

#include <stdint.h>
#include <string.h>

typedef union { uint8_t b[16]; uint16_t h[8]; uint32_t w[4]; uint64_t d[2]; } mx_v;

static inline uint64_t mx_get(const mx_v *v, int es, int i)
{
	switch (es) {
	case 1: return v->b[i];
	case 2: return v->h[i];
	case 4: return v->w[i];
	default: return v->d[i];
	}
}

static inline void mx_set(mx_v *v, int es, int i, uint64_t x)
{
	switch (es) {
	case 1: v->b[i] = (uint8_t)x; break;
	case 2: v->h[i] = (uint16_t)x; break;
	case 4: v->w[i] = (uint32_t)x; break;
	default: v->d[i] = x; break;
	}
}

static inline int64_t mx_sx(uint64_t x, int es)
{
	switch (es) {
	case 1: return (int8_t)x;
	case 2: return (int16_t)x;
	case 4: return (int32_t)x;
	default: return (int64_t)x;
	}
}

static inline uint64_t mx_mask(int es) { return es == 8 ? ~0ULL : (1ULL << (8 * es)) - 1; }

/* sats / satu: saturate a (wide) value to esize */
static inline uint64_t mx_sats(__int128 x, int es)
{
	__int128 hi = ((__int128)1 << (8 * es - 1)) - 1, lo = -hi - 1;

	return (uint64_t)(x > hi ? hi : x < lo ? lo : x) & mx_mask(es);
}

static inline uint64_t mx_satu(__int128 x, int es)
{
	__int128 hi = ((__int128)1 << (8 * es)) - 1;

	return (uint64_t)(x > hi ? hi : x < 0 ? 0 : x);
}

enum mx_op {
	MX_ADD, MX_SUB, MX_MUL, MX_MADD, MX_DIVU, MX_ADDSS, MX_ADDUU, MX_SUBSS, MX_SUBUU, MX_SUBSA, MX_SUBUA,
	MX_CEQ, MX_CLTS, MX_CLTU, MX_CLEU,
	MX_SLL, MX_SRA, MX_SRL, MX_SRLR,	/* shift count from vrt elements */
};

/* three register element-wise operations: vrd = f(vrs, vrt) */
static inline void mx_3r(mx_v *vr, int op, int es, int rd, int rs, int rt)
{
	mx_v out = vr[rd], a = vr[rs], b = vr[rt];
	int i, n = 16 / es, lg = es == 1 ? 3 : es == 2 ? 4 : es == 4 ? 5 : 6;

	for (i = 0; i < n; i++) {
		uint64_t x = mx_get(&a, es, i), y = mx_get(&b, es, i), r = 0;
		int64_t sx = mx_sx(x, es), sy = mx_sx(y, es);
		int sh = (int)(y & ((1u << lg) - 1));

		switch (op) {
		case MX_ADD: r = x + y; break;
		case MX_SUB: r = x - y; break;
		case MX_MUL: r = x * y; break;
		case MX_MADD: r = mx_get(&out, es, i) + x * y; break;
		case MX_DIVU: r = y ? x / y : mx_mask(es); break;	/* ASSUMPTION for y == 0 */
		case MX_ADDSS: r = mx_sats((__int128)sx + sy, es); break;
		case MX_ADDUU: r = mx_satu((__int128)x + y, es); break;
		case MX_SUBSS: r = mx_sats((__int128)sx - sy, es); break;
		case MX_SUBUU: r = mx_satu((__int128)x - y, es); break;
		case MX_SUBSA: r = (uint64_t)((__int128)sx - sy < 0 ? -((__int128)sx - sy) : (__int128)sx - sy); break;
		case MX_SUBUA: r = x > y ? x - y : y - x; break;
		case MX_CEQ: r = x == y ? mx_mask(es) : 0; break;
		case MX_CLTS: r = sx < sy ? mx_mask(es) : 0; break;
		case MX_CLTU: r = x < y ? mx_mask(es) : 0; break;
		case MX_CLEU: r = x <= y ? mx_mask(es) : 0; break;
		case MX_SLL: r = x << sh; break;
		case MX_SRA: r = (uint64_t)(sx >> sh); break;
		case MX_SRL: r = x >> sh; break;
		case MX_SRLR: r = (x >> sh) + (sh ? (x >> (sh - 1)) & 1 : 0); break;
		}
		mx_set(&out, es, i, r & mx_mask(es));
	}
	vr[rd] = out;
}

/* shifts by immediate: SLLI, SRAI, SRLI, SRARI */
enum { MX_SLLI, MX_SRAI, MX_SRLI, MX_SRARI };

static inline void mx_shi(mx_v *vr, int op, int es, int rd, int rs, int imm)
{
	mx_v out, a = vr[rs];
	int i, n = 16 / es, lg = es == 1 ? 3 : es == 2 ? 4 : es == 4 ? 5 : 6, sh = imm & ((1 << lg) - 1);

	for (i = 0; i < n; i++) {
		uint64_t x = mx_get(&a, es, i), r = 0;
		int64_t sx = mx_sx(x, es);

		switch (op) {
		case MX_SLLI: r = x << sh; break;
		case MX_SRAI: r = (uint64_t)(sx >> sh); break;
		case MX_SRLI: r = x >> sh; break;
		case MX_SRARI: r = (uint64_t)(sx >> sh) + (sh ? (x >> (sh - 1)) & 1 : 0); break;
		}
		mx_set(&out, es, i, r & mx_mask(es));
	}
	vr[rd] = out;
}

static inline void mx_andv(mx_v *vr, int rd, int rs, int rt) { int i; mx_v o; for (i = 0; i < 4; i++) o.w[i] = vr[rs].w[i] & vr[rt].w[i]; vr[rd] = o; }
static inline void mx_orv(mx_v *vr, int rd, int rs, int rt) { int i; mx_v o; for (i = 0; i < 4; i++) o.w[i] = vr[rs].w[i] | vr[rt].w[i]; vr[rd] = o; }

/* BSELV: vrd = vrr ? vrt : vrs, bitwise */
static inline void mx_bselv(mx_v *vr, int rd, int rs, int rt, int rr)
{
	mx_v o;
	int i;

	for (i = 0; i < 4; i++)
		o.w[i] = (vr[rr].w[i] & vr[rt].w[i]) | (~vr[rr].w[i] & vr[rs].w[i]);
	vr[rd] = o;
}

/* SHUFV: 256-bit op = pairs {vrs byte i (high), vrt byte i (low)}; byte sel of vrr indexes it */
static inline void mx_shufv(mx_v *vr, int rd, int rs, int rt, int rr)
{
	uint8_t op[32];
	mx_v o, sel = vr[rr];
	int i;

	for (i = 0; i < 16; i++) {
		op[2 * i] = vr[rt].b[i];
		op[2 * i + 1] = vr[rs].b[i];
	}
	for (i = 0; i < 16; i++)
		o.b[i] = (sel.b[i] & 0x80) ? 0 : op[sel.b[i] & 31];	/* ASSUMPTION: bit 7 = zero */
	vr[rd] = o;
}

static inline void mx_repi(mx_v *vr, int es, int rd, int rs, int imm)
{
	mx_v o, a = vr[rs];
	int i, n = 16 / es, sel = imm & (n - 1);

	for (i = 0; i < n; i++)
		mx_set(&o, es, i, mx_get(&a, es, sel));
	vr[rd] = o;
}

static inline void mx_insfmxu(mx_v *vr, int es, int rd, int rs, int imm)
{
	mx_v o = vr[rd];

	mx_set(&o, es, imm & (16 / es - 1), mx_get(&vr[rs], es, 0));
	vr[rd] = o;
}

static inline void mx_mfcpu(mx_v *vr, int es, int rd, uint32_t gpr)
{
	mx_v o;
	int i;

	for (i = 0; i < 16 / es; i++)
		mx_set(&o, es, i, gpr & mx_mask(es));
	vr[rd] = o;
}

static inline uint32_t mx_mtcpu(const mx_v *vr, int es, int rs, int imm, int sign)
{
	uint64_t x = mx_get(&vr[rs], es, imm & (16 / es - 1));

	return (uint32_t)(sign ? (uint64_t)mx_sx(x, es) : x);
}

static inline void mx_li(mx_v *vr, int es, int rd, int imm15)
{
	mx_v o;
	int i;
	uint64_t data = es == 1 ? (uint64_t)(imm15 & 0xff) : (uint64_t)(int64_t)(int16_t)((imm15 & 0x7fff) | ((imm15 & 0x4000) ? 0x8000 : 0));

	for (i = 0; i < 16 / es; i++)
		mx_set(&o, es, i, data & mx_mask(es));
	vr[rd] = o;
}

/* CEQZ / CNEZ / CLTZ: compare each element of vrs with zero */
enum { MX_CEQZ, MX_CNEZ, MX_CLTZ };

static inline void mx_2rz(mx_v *vr, int op, int es, int rd, int rs)
{
	mx_v o, a = vr[rs];
	int i;

	for (i = 0; i < 16 / es; i++) {
		uint64_t x = mx_get(&a, es, i);
		int c = op == MX_CEQZ ? x == 0 : op == MX_CNEZ ? x != 0 : mx_sx(x, es) < 0;

		mx_set(&o, es, i, c ? mx_mask(es) : 0);
	}
	vr[rd] = o;
}

#endif /* T32_MXU2_H */
