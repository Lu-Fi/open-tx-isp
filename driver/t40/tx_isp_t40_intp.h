/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T40 11-knot gain/EV table interpolation as the OEM tisp_simple_intp_int8 /
 * tisp_simple_intp_int16 compute it (used by the C chains tx_isp_t40_*.h).
 * idx = value >> 16 (signed), frac = value & 0xffff.  An index of 10 or
 * more, or a negative one (unsigned compare in the OEM), takes the last knot.
 * The step |b - a| * frac is rounded half up: bit 15 of the product.
 */
#ifndef TX_ISP_T40_INTP_H
#define TX_ISP_T40_INTP_H

static inline uint32_t t40_intp_step(uint32_t a, uint32_t b, uint32_t frac)
{
	uint32_t d = a < b ? b - a : a - b, m = d * frac;

	return (m >> 16) + ((m >> 15) & 1);
}

static inline uint32_t t40_intp_u8(int32_t idx, uint32_t frac, const uint8_t *t)
{
	uint32_t a, b, s;

	if ((uint32_t)idx >= 10)
		return t[10];
	a = t[idx];
	b = t[idx + 1];
	s = t40_intp_step(a, b, frac) & 0xff;
	return (a < b ? a + s : a - s) & 0xff;
}

static inline uint32_t t40_intp_u16(int32_t idx, uint32_t frac, const uint8_t *t)
{
	uint32_t a, b, s;

	if ((uint32_t)idx >= 10)
		return t[20] | (t[21] << 8);
	a = t[2 * idx] | (t[2 * idx + 1] << 8);
	b = t[2 * idx + 2] | (t[2 * idx + 3] << 8);
	s = t40_intp_step(a, b, frac) & 0xffff;
	return (a < b ? a + s : a - s) & 0xffff;
}

/* value in 16.16: table index in the high half, fraction in the low half */
static inline uint32_t t40_intp_u8_at(uint32_t v, const uint8_t *t)
{
	return t40_intp_u8((int32_t)v >> 16, v & 0xffff, t);
}

static inline uint32_t t40_intp_u16_at(uint32_t v, const uint8_t *t)
{
	return t40_intp_u16((int32_t)v >> 16, v & 0xffff, t);
}

#endif
