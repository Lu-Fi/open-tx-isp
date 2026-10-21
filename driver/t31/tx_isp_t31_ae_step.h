/* T31 AE: minimum EV step of ae0_tune2 (OEM 0x50a44..0x50b2c).
 *
 * ae_ev_step[2] is a MINIMUM step, not a clamp.  A ratio strictly between
 * 1.0 and 1.0 + step is pushed up to 1.0 + step, one strictly between
 * 1.0 - step and 1.0 is pushed down to 1.0 - step; larger steps and exactly
 * 1.0 pass unchanged.  Clamping to [1.0 - step, 1.0 + step] instead limits
 * the AE to 4/1024 = 0.4 % per frame with the stock bins (sc2336: a 2.8x
 * light change took ~20 s, a door opening ~35 s).
 *
 * All values are in Q(q*2): base_q2 is 1.0, step_q2 is ae_ev_step[2] << q.
 */
#ifndef TX_ISP_T31_AE_STEP_H
#define TX_ISP_T31_AE_STEP_H

static inline uint64_t t31_ae_min_step_q2(uint64_t interp_q2, uint64_t base_q2,
					  uint64_t step_q2)
{
	uint64_t upper_bound = base_q2 + step_q2;
	uint64_t lower_bound = base_q2 - step_q2;

	if (interp_q2 > base_q2 && interp_q2 < upper_bound)
		return upper_bound;
	if (interp_q2 < base_q2 && interp_q2 > lower_bound)
		return lower_bound;
	return interp_q2;
}

#endif /* TX_ISP_T31_AE_STEP_H */
