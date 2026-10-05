/* T41 HV flip helpers (driver/t41/tx_isp_t41_hvflip.h): request checks,
 * MSCA byte, LSC arguments, and the sensor write plan against a model of
 * the T41 SDK gc5603 sensor_set_hvflip (OR into 0x022c, OTP from the value
 * read before the write). */
#include <assert.h>
#include <stdio.h>
#include "../driver/t41/tx_isp_t41_hvflip.h"

struct gc5603_model {
	unsigned char reg_022c;
	unsigned char otp_0a73;
};

/* gc5603.c sensor_set_hvflip (common/sensor/t41, T41 SDK) */
static void gc5603_set_hvflip(struct gc5603_model *s, unsigned int mode)
{
	unsigned char val = s->reg_022c;

	s->reg_022c = mode ? (unsigned char)(val | mode) : 0;
	s->otp_0a73 = (unsigned char)(0x60 | val);
}

static void apply(struct gc5603_model *s, int *prev, unsigned int mode)
{
	unsigned int seq[T41_HVFLIP_MAX_SENSOR_WRITES];
	unsigned int n = t41_hvflip_sensor_plan(*prev, mode, seq), i;

	assert(n >= 1 && n <= T41_HVFLIP_MAX_SENSOR_WRITES);
	assert(seq[n - 1] == mode);
	for (i = 0; i < n; ++i)
		gc5603_set_hvflip(s, seq[i]);
	*prev = (int)mode;
	assert(s->reg_022c == mode);
	assert(s->otp_0a73 == (0x60 | mode));
}

int main(void)
{
	unsigned int a[4], seq[T41_HVFLIP_MAX_SENSOR_WRITES];
	unsigned int f, m, s, i0, i1, i2;
	struct gc5603_model gc = { 0x00, 0x60 };
	int prev = -1;
	unsigned int from, to;

	/* request checks: 0..3 each, no bit on the sensor and an ISP channel */
	for (s = 0; s < 5; ++s)
		for (i0 = 0; i0 < 5; ++i0)
			for (i1 = 0; i1 < 4; ++i1)
				for (i2 = 0; i2 < 4; ++i2) {
					int ok = s <= 3 && i0 <= 3 &&
						 !((i0 | i1 | i2) & s);
					a[0] = s; a[1] = i0; a[2] = i1; a[3] = i2;
					assert(t41_hvflip_valid(a) == ok);
				}

	/* MSCA byte: channel, kept bits 6/7, mirror 2/3, flip 4/5 */
	assert(t41_hvflip_msca_byte(0, 0x00, 0) == 0x00);
	assert(t41_hvflip_msca_byte(1, 0x00, 1) == 0x0d);
	assert(t41_hvflip_msca_byte(2, 0x40, 2) == 0x72);
	assert(t41_hvflip_msca_byte(2, 0xff, 3) == 0xfe);
	assert(t41_hvflip_msca_byte(5, 0x80, 0) == 0x81);

	/* LSC: stock passes (flip, mirror) = (bit 1, bit 0) */
	t41_hvflip_lsc_args(0, &f, &m); assert(f == 0 && m == 0);
	t41_hvflip_lsc_args(1, &f, &m); assert(f == 0 && m == 1);
	t41_hvflip_lsc_args(2, &f, &m); assert(f == 1 && m == 0);
	t41_hvflip_lsc_args(3, &f, &m); assert(f == 1 && m == 1);

	/* a re-assertion is one write, a change clears first */
	assert(t41_hvflip_sensor_plan(2, 2, seq) == 1 && seq[0] == 2);
	assert(t41_hvflip_sensor_plan(0, 0, seq) == 1 && seq[0] == 0);
	assert(t41_hvflip_sensor_plan(1, 2, seq) == 3 &&
	       seq[0] == 0 && seq[1] == 2 && seq[2] == 2);
	assert(t41_hvflip_sensor_plan(3, 0, seq) == 2 &&
	       seq[0] == 0 && seq[1] == 0);
	assert(t41_hvflip_sensor_plan(-1, 1, seq) == 3 && seq[0] == 0);

	/* cam-F: H then V used to leave 0x022c = 0x03; every transition from
	 * every state (including an unknown start) now lands exactly */
	apply(&gc, &prev, 1);
	apply(&gc, &prev, 2);
	for (from = 0; from < 4; ++from)
		for (to = 0; to < 4; ++to) {
			apply(&gc, &prev, from);
			apply(&gc, &prev, to);
			apply(&gc, &prev, to);  /* timps re-assertion */
		}
	/* unknown sensor state after a failed write */
	gc.reg_022c = 0x03;
	gc.otp_0a73 = 0x61;
	prev = -1;
	apply(&gc, &prev, 1);

	puts("t41 hvflip: passed");
	return 0;
}
