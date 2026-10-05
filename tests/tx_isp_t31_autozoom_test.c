/* Host test of driver/t31/tx_isp_t31_autozoom.h (tuning 0x80000e8). */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../driver/t31/tx_isp_t31_autozoom.h"

int main(void)
{
	/* channel 0: 1920x1080 input, scaler off, crop off -> 1920x1080 */
	uint32_t cur[13] = { 0 };
	uint32_t next[13];
	uint32_t w, h;
	/* zoom 2x into the centre: scale to 3840x2160, crop 1920x1080 */
	uint32_t req[T31_AUTOZOOM_WORDS] = { 0, 1, 3840, 2160, 1, 960, 540,
					     1920, 1080 };

	cur[8] = 0xdead;                 /* frame-crop words are kept */
	assert(t31_msca_out_size(cur, 1920, 1080, &w, &h) == 0 &&
	       w == 1920 && h == 1080);
	assert(t31_autozoom_attr(cur, req, 1920, 1080, next) == 0);
	assert(next[0] == 1 && next[1] == 3840 && next[2] == 2160 &&
	       next[3] == 1 && next[4] == 960 && next[5] == 540 &&
	       next[6] == 1920 && next[7] == 1080 && next[8] == 0xdead);

	/* a different written size would overrun the channel's buffers */
	req[7] = 1280; req[8] = 720;
	assert(t31_autozoom_attr(cur, req, 1920, 1080, next) == -EINVAL);
	/* crop window outside the scaler output */
	req[7] = 1920; req[8] = 1080; req[5] = 1921;
	assert(t31_autozoom_attr(cur, req, 1920, 1080, next) == -EINVAL);
	/* overflow of x + w must not wrap */
	req[5] = 0xffffff00U;
	assert(t31_autozoom_attr(cur, req, 1920, 1080, next) == -EINVAL);
	/* zero scaler size */
	req[5] = 0; req[2] = 0;
	assert(t31_autozoom_attr(cur, req, 1920, 1080, next) == -EINVAL);
	/* zoom back out: scaler off, crop off */
	memset(req, 0, sizeof(req));
	assert(t31_autozoom_attr(cur, req, 1920, 1080, next) == 0);
	/* scaled channel (640x360): scaler on, no crop */
	memset(cur, 0, sizeof(cur));
	cur[0] = 1; cur[1] = 640; cur[2] = 360;
	{
		uint32_t z[T31_AUTOZOOM_WORDS] = { 1, 1, 1280, 720, 1, 320,
						   180, 640, 360 };

		assert(t31_autozoom_attr(cur, z, 1920, 1080, next) == 0);
		z[1] = 0;                /* scaler off: 1920x1080 crop base */
		assert(t31_autozoom_attr(cur, z, 1920, 1080, next) == 0);
		z[4] = 0;                /* crop off: writes 1920x1080 */
		assert(t31_autozoom_attr(cur, z, 1920, 1080, next) == -EINVAL);
	}
	puts("tx_isp_t31_autozoom_test: ok");
	return 0;
}
