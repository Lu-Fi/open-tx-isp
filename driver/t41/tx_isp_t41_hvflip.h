/* SPDX-License-Identifier: MIT */
/*
 * T41 HV flip (tuning control 0x08000073, IMPISPHVFLIPAttr
 * { sensor_mode; isp_mode[3]; }): mode bit 0 = mirror (H), bit 1 = flip
 * (V).  Pure helpers shared by tx_isp_t41_recovered.c and the host test.
 */
#ifndef TX_ISP_T41_HVFLIP_H
#define TX_ISP_T41_HVFLIP_H

#define T41_HVFLIP_ISP_CHANNELS 3U
#define T41_HVFLIP_MAX_SENSOR_WRITES 3U

/*
 * Stock tx_isp_core_ops_s_ctrl: every mode is 0..3, and the sensor and an
 * ISP channel must not both mirror (or both flip).
 */
static inline int t41_hvflip_valid(const unsigned int attr[4])
{
	unsigned int i;

	for (i = 0; i < 4; ++i)
		if (attr[i] > 3)
			return 0;
	return !((attr[1] | attr[2] | attr[3]) & attr[0]);
}

/*
 * The g_hv_flip byte tisp_s_hv_flip takes for one MSCA channel (stock:
 * tisp_g_hv_flip, then ins bits 2/3 = mirror, 4/5 = flip): bits 0-1 the
 * channel, bits 6/7 kept from the current state.
 */
static inline unsigned char t41_hvflip_msca_byte(unsigned int channel,
						 unsigned char keep,
						 unsigned int mode)
{
	unsigned char bits = (unsigned char)((channel & 3U) | (keep & 0xc0U));

	if (mode & 1U)
		bits |= 0x0c;
	if (mode & 2U)
		bits |= 0x30;
	return bits;
}

/* Stock tisp_lsc_hvflip(.., .., flip, mirror) arguments for a sensor mode. */
static inline void t41_hvflip_lsc_args(unsigned int mode, unsigned int *flip,
				       unsigned int *mirror)
{
	*flip = (mode >> 1) & 1U;
	*mirror = mode & 1U;
}

/*
 * Sensor flip writes (event 0x02000010) for a request.  prev is the mode
 * last written, -1 when unknown.
 *
 * The T41 SDK gc5603 driver ORs the new bits into the value it reads back
 * from 0x022c and arms the OTP auto-load with the bits read *before* the
 * write.  A change between two different non-zero modes (cam-F: H, then V)
 * therefore left mirror and flip both set (0x022c = 0x03), and the OTP
 * orientation lags one write behind.  For a change the mode is cleared
 * first and the target written twice, so register and OTP orientation
 * both converge on the requested mode; a sensor driver that writes exact
 * bits only sees idempotent repeats.  A re-assertion of the current mode
 * stays one write (timps re-asserts the flip after every chn0 restart).
 *
 * Returns the number of entries filled in seq.
 */
static inline unsigned int t41_hvflip_sensor_plan(int prev, unsigned int mode,
		unsigned int seq[T41_HVFLIP_MAX_SENSOR_WRITES])
{
	unsigned int n = 0;

	if (prev >= 0 && (unsigned int)prev == mode) {
		seq[n++] = mode;
		return n;
	}
	seq[n++] = 0;
	seq[n++] = mode;
	if (mode)
		seq[n++] = mode;
	return n;
}

/*
 * Beyond the vendor driver: after a sensor (re)start the sensor registers
 * come from its init table, so the flip the application asked for is
 * gone until the application sets it again.  desired is the last
 * accepted sensor mode (-1 = never set: leave the sensor alone); the
 * sensor state counts as unknown, so the full NORMAL-then-target
 * sequence runs.  Returns the number of entries filled in seq.
 */
static inline unsigned int t41_hvflip_reinit_plan(int desired,
		unsigned int seq[T41_HVFLIP_MAX_SENSOR_WRITES])
{
	if (desired < 0 || desired > 3)
		return 0;
	return t41_hvflip_sensor_plan(-1, (unsigned int)desired, seq);
}

#endif /* TX_ISP_T41_HVFLIP_H */
