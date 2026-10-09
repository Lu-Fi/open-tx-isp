/* Host test of driver/t31/tx_isp_t31_fcrop.h (T31 front crop decisions). */
#include <assert.h>
#include <stdio.h>

#include "../driver/t31/tx_isp_t31_fcrop.h"

int main(void)
{
	u32 w, h;

	assert(t31_fcrop_axis_ok(1280, 1280, 0));
	assert(!t31_fcrop_axis_ok(1279, 1280, 0));
	assert(t31_fcrop_axis_ok(1000, 1100, 10));
	assert(!t31_fcrop_axis_ok(1000, 1101, 10));

	/* running ch0 main 1920x1080 scaled from 2560x1440 */
	assert(t31_fcrop_chan_fits(1, 0, (1920u << 16) | 1080, 1920, 1080,
				   1920, 1080, 0, &w, &h) && w == 1920 && h == 1080);
	assert(!t31_fcrop_chan_fits(1, 0, (1920u << 16) | 1080, 0, 0,
				    320, 180, 0, &w, &h));
	/* unscaled main = sensor size: any real window is refused (S2) */
	assert(!t31_fcrop_chan_fits(1, 0, (2560u << 16) | 1440, 2560, 1440,
				    1920, 1080, 0, 0, 0));
	/* stopped channel with a stale large size does not veto */
	assert(t31_fcrop_chan_fits(1, 1, (2560u << 16) | 1440, 2560, 1440,
				   320, 180, 0, 0, 0));
	/* running channel without a known size does not veto */
	assert(t31_fcrop_chan_fits(2, 1, 0, 0, 0, 320, 180, 0, 0, 0));
	/* cache larger than the register wins */
	assert(!t31_fcrop_chan_fits(1, 0, (640u << 16) | 360, 1920, 1080,
				    1280, 720, 0, &w, &h) && w == 1920);

	/* set-format under a 1280x720 window on a 2560x1440 sensor */
	assert(!t31_fcrop_format_misfit(1, 1280, 720, 2560, 1440, 640, 360, 0));
	assert(t31_fcrop_format_misfit(1, 1280, 720, 2560, 1440, 1920, 1080, 0));
	/* no scaler: output = window 1:1, only the sensor size is misfit */
	assert(!t31_fcrop_format_misfit(0, 1280, 720, 1280, 720, 0, 0, 0));
	assert(t31_fcrop_format_misfit(0, 1280, 720, 2560, 1440, 0, 0, 0));
	assert(t31_fcrop_format_misfit(0, 2560, 700, 2560, 1440, 0, 0, 0));

	/* last close drops any trace of a window */
	assert(!t31_fcrop_release_needed(0, 0, 0));
	assert(t31_fcrop_release_needed(1, 0, 0));
	assert(t31_fcrop_release_needed(0, 1, 0));
	assert(t31_fcrop_release_needed(0, 0, 1));

	printf("tx_isp_t31_fcrop_test: PASS\n");
	return 0;
}
