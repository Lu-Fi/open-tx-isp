/* Host test: T30 AWB scene presets (Lu-Fi/openimp#202).
 *
 * Runs driver/t30/tx_isp_t30_awb_preset.h (t30_get_awb_idx, t30_awb_preset_gains)
 * on the inputs of tests/t30_awb_mode_oracle.h, which tools/ko_emu/diff_t30_awb_mode.py
 * recorded from the SDK-built tx-isp-t30.ko 4ea12e1f in Unicorn (get_awb_idx 0x21a08,
 * awb_mode 0x21a30 with a synthetic table 255).  Part 2 applies the default calibration
 * table 255 (awb_scene_presets, 7 rows x 2 u16) and expects the read-backs of the stock
 * vendor stack on the T30X (audit b26/b27/b29 vpic: cloudy 146/101, incandescent 73/230 ...).
 * The lift before this fix returned row 0 for every preset (128/128): part 1 and 2 fail on it
 * (check_t30_awb_preset.sh mutations). */
#include <stdint.h>
#include <stdio.h>

#ifndef AWB_HDR
#define AWB_HDR "../driver/t30/tx_isp_t30_awb_preset.h"
#endif
#include AWB_HDR
#include "t30_awb_mode_oracle.h"

#define CK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); bad++; } } while (0)
#define N(a) (int)(sizeof(a) / sizeof((a)[0]))

int main(void)
{
	int bad = 0, i, np = 0;
	uint16_t test_lut[16];
	/* SDK apical_static_calibrations.c awb_scene_presets = driver _calibration_awb_scene_presets */
	static const uint16_t presets[14] = { 256, 257, 293, 202, 147, 461, 210, 307, 210, 307, 313, 187, 160, 376 };
	static const uint8_t vendor_rb[7][2] = {	/* stock read-back mode 2..8 (b26/b27/b29 vpic) */
		{ 128, 128 }, { 146, 101 }, { 73, 230 }, { 105, 153 }, { 105, 153 }, { 156, 93 }, { 80, 188 } };

	for (i = 0; i < 8; i++) {	/* same synthetic table as diff_t30_awb_mode.py */
		test_lut[2 * i] = (uint16_t)(1000 + 16 * i);
		test_lut[2 * i + 1] = (uint16_t)(2000 + 16 * i + 3);
	}
	/* part 1a: get_awb_idx for every recorded input */
	for (i = 0; i < N(t30_vendor_awb_idx); i++)
		if ((int)t30_get_awb_idx(t30_vendor_awb_idx[i][0]) != t30_vendor_awb_idx[i][1]) {
			printf("FAIL get_awb_idx(%d) = %u, vendor %d\n", t30_vendor_awb_idx[i][0],
			       t30_get_awb_idx(t30_vendor_awb_idx[i][0]), t30_vendor_awb_idx[i][1]);
			bad++;
		}
	/* part 1b: preset gains of awb_mode for every mode the vendor accepts as a preset */
	for (i = 0; i < N(t30_vendor_awb_mode); i++) {
		const int *v = t30_vendor_awb_mode[i];
		uint8_t r, b;

		if (v[0] < T30_AWB_MODE_PRESET0 || v[0] >= T30_AWB_MODE_END) {
			CK(v[8] == -1);		/* no table read outside the preset range */
			continue;
		}
		CK(v[1] == 0 && v[2] == 1 && v[5] == 0 && v[6] == v[0] && v[8] == 255);
		t30_awb_preset_gains(test_lut, v[0], &r, &b);
		if (r != v[3] || b != v[4]) {
			printf("FAIL awb_mode(%#x) gains %u/%u, vendor %d/%d\n", v[0], r, b, v[3], v[4]);
			bad++;
		}
		np++;
	}
	CK(np == 7);
	/* part 2: the default table gives the stock read-backs */
	for (i = 0; i < 7; i++) {
		uint8_t r, b;

		t30_awb_preset_gains(presets, T30_AWB_MODE_PRESET0 + i, &r, &b);
		if (r != vendor_rb[i][0] || b != vendor_rb[i][1]) {
			printf("FAIL preset mode %d: %u/%u, stock %u/%u\n", i + 2, r, b, vendor_rb[i][0], vendor_rb[i][1]);
			bad++;
		}
	}
	printf("t30_awb_preset: %d get_awb_idx inputs, %d awb_mode modes (%d presets), 7 default presets: %s\n",
	       N(t30_vendor_awb_idx), N(t30_vendor_awb_mode), np, bad ? "FAIL" : "ok");
	return bad ? 1 : 0;
}
