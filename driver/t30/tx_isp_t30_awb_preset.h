/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T30 AWB scene presets (AWB_MODE_ID 0x60, Lu-Fi/openimp#202).
 *
 * IMP_ISP_Tuning_SetWB(mode N) reaches awb_mode() as apical_command(3, 0x60, N + 0x32):
 * 0x32 auto, 0x33 manual, 0x34..0x3a the seven scene presets (daylight, cloudy, incandescent,
 * fluorescent, twilight, shade, warm).  A preset takes one row of calibration table 255
 * (awb_scene_presets, two u16 per row: red, blue) and stores row >> 1 into stab[0x31]/[0x32].
 *
 * Vendor reference: SDK-built tx-isp-t30.ko 4ea12e1f
 *   get_awb_idx 0x21a08: idx = mode - 53; idx < 6 ? lbu awb_idx_table[idx] : 0,
 *                        awb_idx_table (.rodata 0xdec) = { 1, 2, 3, 4, 5, 6 }
 *   awb_mode    0x21a30: lut = _GET_MOD_ENTRY16_PTR(255); lut += get_awb_idx(mode) << 2 (BYTES:
 *                        0x21af8 sll 2, 0x21b04 addu) -> lhu 0/2 -> srl 1 -> sb stab+49 / stab+50
 * so mode 0x34 -> row 0 ... 0x3a -> row 6.  The earlier lift inverted the get_awb_idx range test
 * (every preset returned 0) and stepped 4 u16 per row: all presets read back row 0 >> 1 = 128/128.
 * tests/t30_awb_preset_host_test.c checks both helpers against the vendor ko run in Unicorn
 * (tools/ko_emu/diff_t30_awb_mode.py).
 */
#ifndef TX_ISP_T30_AWB_PRESET_H
#define TX_ISP_T30_AWB_PRESET_H

#define T30_AWB_MODE_AUTO	0x32
#define T30_AWB_MODE_MANUAL	0x33
#define T30_AWB_MODE_PRESET0	0x34	/* daylight, row 0 of table 255 */
#define T30_AWB_MODE_END	0x3b	/* first id past warm (0x3a, row 6) */

/* get_awb_idx (ko 0x21a08): row of table 255 for an AWB mode id */
static inline uint32_t t30_get_awb_idx(int32_t mode)
{
	static const uint8_t t30_awb_idx_table[6] = { 1, 2, 3, 4, 5, 6 };
	uint32_t idx = (uint32_t)mode - 53;

	if (idx < 6)
		return t30_awb_idx_table[idx];
	return 0;
}

/* awb_mode preset arm (ko 0x21ad8-0x21b20): gains of one preset row, two u16 per row */
static inline void t30_awb_preset_gains(const uint16_t *lut, int32_t mode,
					uint8_t *red, uint8_t *blue)
{
	const uint16_t *row = lut + 2 * t30_get_awb_idx(mode);

	*red = (uint8_t)(row[0] >> 1);
	*blue = (uint8_t)(row[1] >> 1);
}

#endif
