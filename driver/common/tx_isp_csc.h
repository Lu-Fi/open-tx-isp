/* SPDX-License-Identifier: GPL-2.0 */
/*
 * CSC presets and front crop for T10/T20/T21 (beyond vendor).
 *
 *   0x080000a6 CSC_ATTR    IMP_ISP_Tuning_Set/GetCsc_Attr   (tuning ioctl,
 *                          pointer to IMPISPCscAttr, 0x40 bytes)
 *   0x080000e3 FRONT_CROP  IMP_ISP_Tuning_Set/GetFrontCrop  (tuning ioctl,
 *                          pointer to 5 dwords: enable, top, left, width,
 *                          height = IMPISPFrontCrop)
 *
 * The T10/T20 3.12.0 and T21 1.0.33 libimp have neither API, and the stock
 * tx-isp-t10/t20/t21.ko reject or ignore both control IDs.  The ABI is the
 * T31 one (OpenIMP sends the same IDs on every T-series SoC), so a caller
 * written against T31 works unchanged.
 *
 * CSC attribute (16 dwords): [0] mode, [1..9] 3x3 matrix (Q10, signed,
 * |c| <= 0x3ff), [10] Y offset, [11] UV offset, [12] Y min, [13] Y max,
 * [14] UV min, [15] UV max (8 bit).  Modes 0..3 are the OEM T31 presets
 * (BT601 full, BT601 TV range, BT709 full, BT709 TV range), mode 4 takes
 * the matrix from the attribute.  This is the word order the OEM T31
 * kernel consumes (tisp_set_csc_version); the comments in the vendor
 * header name the offset/clip words in a different order than the data
 * the OEM presets carry, the presets are authoritative.
 *
 *  - T21: the CSC block at 0x1700 has the T31 0x6000 layout.  tisp_init
 *    writes T31 preset 0 there word for word (0x1710 = 0x07596532 ...),
 *    so the T31 packing applies unchanged.
 *  - T10/T20: the Apical firmware composes the YUV matrix from brightness,
 *    contrast, saturation, hue and calibration table RGB2YUV_CONVERSION
 *    (Q8 sign-magnitude, 10-bit offsets).  A preset replaces that table
 *    through the vendor calibration API, so BCSH and the night mono path
 *    keep working on top of it.  The SDK default table is T31 preset 3
 *    converted exactly (checked by the host test).
 *
 * Front crop: T10/T20/T21 have no crop stage in front of the scalers (T31
 * has one in its mscaler, 0x9860).  The per-channel crop sits behind the
 * scaler (T21) or replaces the output size (T20 FR), and neither scaler
 * enlarges, so a T31-style zoom cannot be built from them without
 * changing what the channels deliver.  The control is therefore answered
 * honestly: disabling and the full frame are accepted, any smaller window
 * is refused with -EOPNOTSUPP (OpenIMP returns -1) instead of the
 * silent success the cache-only OpenIMP path gave before.
 *
 * Nothing changes until CSC_ATTR is set: the default state is the stock
 * register / calibration content.
 */
#ifndef TX_ISP_CSC_H
#define TX_ISP_CSC_H

#define TX_ISP_CID_CSC_ATTR	0x080000a6
#define TX_ISP_CID_FRONT_CROP	0x080000e3

#define TX_ISP_CSC_ATTR_WORDS	16
#define TX_ISP_CSC_ATTR_BYTES	(TX_ISP_CSC_ATTR_WORDS * 4)
#define TX_ISP_CSC_WORDS	15
#define TX_ISP_CSC_MODE_USER	4
#define TX_ISP_FCROP_WORDS	5
#define TX_ISP_FCROP_BYTES	(TX_ISP_FCROP_WORDS * 4)

/* OEM tx-isp-t31.ko presets 0..3 (see driver/t31/tx_isp_tuning.c). */
static const int32_t tx_isp_csc_presets[4][TX_ISP_CSC_WORDS] = {
	{ 0x132, 0x259, 0x75, -0xad, -0x153, 0x200, 0x200, -0x1ad, -0x53,
	  0x00, 0x80, 0x00, 0xff, 0x00, 0xff },
	{ 0x106, 0x203, 0x64, -0x97, -0x129, 0x1c0, 0x1c0, -0x178, -0x49,
	  0x10, 0x80, 0x10, 0xeb, 0x10, 0xf0 },
	{ 0xda, 0x2dc, 0x4a, -0x75, -0x18b, 0x200, 0x200, -0x1d1, -0x2f,
	  0x00, 0x80, 0x00, 0xff, 0x00, 0xff },
	{ 0xba, 0x273, 0x3f, -0x67, -0x15a, 0x1c0, 0x1c0, -0x197, -0x29,
	  0x10, 0x80, 0x10, 0xeb, 0x10, 0xf0 },
};

/*
 * Validate a CSC attribute.  Like the open T31 driver: mode above 4 and
 * user values the hardware would truncate are refused.  Additionally a
 * user clip range with min > max is refused (it would blank the picture).
 */
static inline int tx_isp_csc_check(const uint32_t *attr)
{
	const uint32_t *p = attr + 1;
	int i;

	if (attr[0] > TX_ISP_CSC_MODE_USER)
		return -EINVAL;
	if (attr[0] != TX_ISP_CSC_MODE_USER)
		return 0;
	for (i = 0; i < 9; i++) {
		int32_t c = (int32_t)p[i];

		if (c < -0x3ff || c > 0x3ff)
			return -EINVAL;
	}
	for (i = 9; i < TX_ISP_CSC_WORDS; i++)
		if (p[i] > 0xff)
			return -EINVAL;
	if (p[11] > p[12] || p[13] > p[14])
		return -EINVAL;
	return 0;
}

/* The 15 parameter words a checked attribute selects. */
static inline void tx_isp_csc_params(const uint32_t *attr, int32_t *out)
{
	int i;

	for (i = 0; i < TX_ISP_CSC_WORDS; i++)
		out[i] = attr[0] == TX_ISP_CSC_MODE_USER ?
			(int32_t)attr[1 + i] : tx_isp_csc_presets[attr[0]][i];
}

/* ---- tiziano (T21 0x1700, T31 0x6000) ------------------------------ */

static inline uint32_t tx_isp_csc_abs10(int32_t v)
{
	uint32_t mag = (uint32_t)v;

	if (v < 0)
		mag = 0U - mag;
	return mag & 0x3ff;
}

static inline uint32_t tx_isp_csc_pack_triplet(int32_t c0, int32_t c1,
					       int32_t c2)
{
	return tx_isp_csc_abs10(c0) | (tx_isp_csc_abs10(c1) << 10) |
	       (tx_isp_csc_abs10(c2) << 20);
}

/*
 * Register words for base + 0x10, 0x14, 0x18 (matrix rows), 0x20 (offsets)
 * and 0x30 (clip).  The signs are implied by the fixed mode word 0x1f at
 * base + 0x00, as in the OEM T31 tisp_set_csc_version.
 */
static inline void tx_isp_csc_tiziano_regs(const int32_t *p, uint32_t *reg)
{
	reg[0] = tx_isp_csc_pack_triplet(p[0], p[1], p[2]);
	reg[1] = tx_isp_csc_pack_triplet(p[3], p[4], p[5]);
	reg[2] = tx_isp_csc_pack_triplet(p[6], p[7], p[8]);
	reg[3] = ((uint32_t)p[9] & 0xff) | (((uint32_t)p[10] & 0xff) << 8);
	reg[4] = ((uint32_t)p[13] & 0xff) | (((uint32_t)p[14] & 0xff) << 8) |
		 (((uint32_t)p[11] & 0xff) << 16) |
		 (((uint32_t)p[12] & 0xff) << 24);
}

/* Night (mono) clip word: chroma pinned to 0x80, the preset's Y range.
 * Preset 0 gives the OEM T21 night word 0xff008080. */
static inline uint32_t tx_isp_csc_tiziano_mono_clip(uint32_t clip)
{
	return (clip & 0xffff0000U) | 0x8080U;
}

/* ---- Apical (T10/T20) ----------------------------------------------- */

/* Q10 -> Q8 sign-magnitude (bit 15 = sign), rounded half away from 0. */
static inline uint16_t tx_isp_csc_apical_coef(int32_t q10)
{
	uint32_t mag = tx_isp_csc_abs10(q10);

	mag = (mag + 2) >> 2;
	return (uint16_t)(q10 < 0 && mag ? 0x8000 | mag : mag);
}

static inline int32_t tx_isp_csc_apical_coef_q10(uint16_t v)
{
	int32_t mag = (int32_t)(v & 0x7fff) << 2;

	return v & 0x8000 ? -mag : mag;
}

/* 8-bit clip limit -> 10 bit; 0xff becomes 1023 like the stock day path. */
static inline uint16_t tx_isp_csc_apical_clip(int32_t v8)
{
	uint32_t v = (uint32_t)v8 & 0xff;

	return (uint16_t)(v == 0xff ? 0x3ff : v << 2);
}

/*
 * RGB2YUV_CONVERSION table (9 coefficients, Y/U/V offsets) and the
 * cs_conv clip registers (Y min, Y max, UV min, UV max) for a parameter set.
 */
static inline void tx_isp_csc_to_apical(const int32_t *p, uint16_t *lut,
					uint16_t *clip)
{
	int i;

	for (i = 0; i < 9; i++)
		lut[i] = tx_isp_csc_apical_coef(p[i]);
	lut[9] = (uint16_t)(((uint32_t)p[9] & 0xff) << 2);
	lut[10] = (uint16_t)(((uint32_t)p[10] & 0xff) << 2);
	lut[11] = lut[10];
	for (i = 0; i < 4; i++)
		clip[i] = tx_isp_csc_apical_clip(p[11 + i]);
}

/*
 * Attribute (mode + 15 words) for an Apical table and clip registers, as
 * the getter reports it: a table equal to a converted preset reports that
 * preset, anything else mode 4 with the table converted back to Q10 (Q8
 * precision).  The clip words always come from the registers.
 */
static inline void tx_isp_csc_from_apical(const uint16_t *lut,
					  const uint16_t *clip, uint32_t *attr)
{
	uint16_t plut[12], pclip[4];
	int m, i;

	attr[0] = TX_ISP_CSC_MODE_USER;
	for (m = 0; m < 4; m++) {
		tx_isp_csc_to_apical(tx_isp_csc_presets[m], plut, pclip);
		for (i = 0; i < 12 && plut[i] == lut[i]; i++)
			;
		if (i == 12) {
			attr[0] = (uint32_t)m;
			break;
		}
	}
	for (i = 0; i < 9; i++)
		attr[1 + i] = (uint32_t)tx_isp_csc_apical_coef_q10(lut[i]);
	attr[10] = (uint32_t)(lut[9] >> 2) & 0xff;
	attr[11] = (uint32_t)(lut[10] >> 2) & 0xff;
	for (i = 0; i < 4; i++)
		attr[12 + i] = (uint32_t)(clip[i] >> 2);
	/* matrix and offsets exactly as the preset; clip as in the registers */
	if (attr[0] != TX_ISP_CSC_MODE_USER)
		for (i = 0; i < 11; i++)
			attr[1 + i] = (uint32_t)tx_isp_csc_presets[attr[0]][i];
}

/* ---- front crop ----------------------------------------------------- */

/*
 * f = {enable, top, left, width, height} (enable: low byte, like the OEM
 * T31 kernel).  0 = accepted (disable or full frame), -EINVAL = malformed
 * or outside the frame, -EOPNOTSUPP = a real sub-window (no hardware
 * stage for it on T10/T20/T21).
 */
static inline int tx_isp_fcrop_check(const uint32_t *f, uint32_t w,
				     uint32_t h)
{
	if ((f[0] & 0xff) == 0)
		return 0;
	if (!w || !h || !f[3] || !f[4])
		return -EINVAL;
	if ((uint64_t)f[2] + f[3] > w || (uint64_t)f[1] + f[4] > h)
		return -EINVAL;
	if (f[1] || f[2] || f[3] != w || f[4] != h)
		return -EOPNOTSUPP;
	return 0;
}

/* Getter: the (always full-frame) window in the set layout. */
static inline void tx_isp_fcrop_get(uint32_t enabled, uint32_t w, uint32_t h,
				    uint32_t *f)
{
	f[0] = enabled ? 1 : 0;
	f[1] = 0;
	f[2] = 0;
	f[3] = w;
	f[4] = h;
}

#endif /* TX_ISP_CSC_H */
