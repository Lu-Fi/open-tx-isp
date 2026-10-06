/* Host test for the T23 MSCA geometry check (driver/t23/tx_isp_t23_msca_geom.h). */
#include <stdio.h>

#include "../driver/t23/tx_isp_t23_msca_geom.h"

static int failures;

#define CHECK(cond) do { \
	if (!(cond)) { \
		fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
		failures++; \
	} \
} while (0)

static struct t23_msca_geom geom(u32 wl, u32 wt, u32 ww, u32 wh,
				 u32 sw, u32 sh,
				 u32 cl, u32 ct, u32 cw, u32 ch)
{
	struct t23_msca_geom g = { wl, wt, ww, wh, sw, sh, cl, ct, cw, ch };

	return g;
}

int main(void)
{
	struct t23_msca_geom g;

	/* plain streams: full sensor, ch0 1:1, ch1 scaled to 640x360 */
	g = geom(0, 0, 1920, 1080, 1920, 1080, 0, 0, 1920, 1080);
	CHECK(t23_msca_geom_check(&g, 1920, 1080, 1920, 1080) == T23_MSCA_GEOM_OK);
	g = geom(0, 0, 1920, 1080, 640, 360, 0, 0, 640, 360);
	CHECK(t23_msca_geom_check(&g, 1920, 1080, 640, 360) == T23_MSCA_GEOM_OK);
	/* buffer not known yet: not checked */
	CHECK(t23_msca_geom_check(&g, 1920, 1080, 0, 0) == T23_MSCA_GEOM_OK);

	/* vorne fcrop-mid50: 960x540 window under the 1920x1080 ch0 = upscale */
	g = geom(480, 270, 960, 540, 1920, 1080, 0, 0, 1920, 1080);
	CHECK(t23_msca_geom_check(&g, 1920, 1080, 0, 0) == T23_MSCA_GEOM_UPSCALE);
	/* ... the same window is fine for the 640x360 ch1 */
	g = geom(480, 270, 960, 540, 640, 360, 0, 0, 640, 360);
	CHECK(t23_msca_geom_check(&g, 1920, 1080, 0, 0) == T23_MSCA_GEOM_OK);
	/* Jooan fcrop-topleft50: 640x360 under 1280x720 ch0 */
	g = geom(0, 0, 640, 360, 1280, 720, 0, 0, 1280, 720);
	CHECK(t23_msca_geom_check(&g, 1280, 720, 0, 0) == T23_MSCA_GEOM_UPSCALE);
	/* only one axis upscaled */
	g = geom(0, 0, 1920, 540, 1920, 1080, 0, 0, 1920, 1080);
	CHECK(t23_msca_geom_check(&g, 1920, 1080, 0, 0) == T23_MSCA_GEOM_UPSCALE);

	/* vorne fs1-crop-mid50: crop 960x540 @480,270 inside a 640x360 scaler
	 * output, 640x360 buffer */
	g = geom(0, 0, 1920, 1080, 640, 360, 480, 270, 960, 540);
	CHECK(t23_msca_geom_check(&g, 1920, 1080, 640, 360) == T23_MSCA_GEOM_CROP);
	/* Jooan fs1-crop-mid50: crop 640x360 @320,180 in 640x360 */
	g = geom(0, 0, 1280, 720, 640, 360, 320, 180, 640, 360);
	CHECK(t23_msca_geom_check(&g, 1280, 720, 640, 360) == T23_MSCA_GEOM_CROP);
	/* top-left variant fits on the Jooan (crop == scaler output) */
	g = geom(0, 0, 1280, 720, 640, 360, 0, 0, 640, 360);
	CHECK(t23_msca_geom_check(&g, 1280, 720, 640, 360) == T23_MSCA_GEOM_OK);
	/* 2x zoom: scale to 1280x720, crop the middle 640x360 */
	g = geom(0, 0, 1920, 1080, 1280, 720, 320, 180, 640, 360);
	CHECK(t23_msca_geom_check(&g, 1920, 1080, 640, 360) == T23_MSCA_GEOM_OK);
	/* crop bigger than the buffer: overrun */
	g = geom(0, 0, 1920, 1080, 1280, 720, 0, 0, 1280, 720);
	CHECK(t23_msca_geom_check(&g, 1920, 1080, 640, 360) == T23_MSCA_GEOM_BUFFER);
	/* crop smaller than the buffer: allowed (frame part stays old) */
	g = geom(0, 0, 1920, 1080, 640, 360, 0, 0, 320, 180);
	CHECK(t23_msca_geom_check(&g, 1920, 1080, 640, 360) == T23_MSCA_GEOM_OK);

	/* window outside the sensor, wrap-around */
	g = geom(1000, 0, 1000, 1080, 640, 360, 0, 0, 640, 360);
	CHECK(t23_msca_geom_check(&g, 1920, 1080, 0, 0) == T23_MSCA_GEOM_WINDOW);
	g = geom(0xffffff00U, 0, 0x200, 1080, 64, 64, 0, 0, 64, 64);
	CHECK(t23_msca_geom_check(&g, 1920, 1080, 0, 0) == T23_MSCA_GEOM_WINDOW);
	g = geom(0, 0, 1920, 1080, 640, 360, 0xfffffff0U, 0, 0x20, 360);
	CHECK(t23_msca_geom_check(&g, 1920, 1080, 0, 0) == T23_MSCA_GEOM_CROP);

	/* zero sizes */
	g = geom(0, 0, 0, 1080, 640, 360, 0, 0, 640, 360);
	CHECK(t23_msca_geom_check(&g, 1920, 1080, 0, 0) == T23_MSCA_GEOM_EMPTY);
	g = geom(0, 0, 1920, 1080, 640, 360, 0, 0, 640, 0);
	CHECK(t23_msca_geom_check(&g, 1920, 1080, 0, 0) == T23_MSCA_GEOM_EMPTY);

	CHECK(t23_msca_geom_reason(T23_MSCA_GEOM_UPSCALE)[0] == 's');
	CHECK(t23_msca_geom_reason(99)[0] == '?');

	if (failures) {
		fprintf(stderr, "tx_isp_t23_msca_geom_test: %d failure(s)\n", failures);
		return 1;
	}
	printf("tx_isp_t23_msca_geom_test: ok\n");
	return 0;
}
