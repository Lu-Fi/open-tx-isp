/* SPDX-License-Identifier: MIT */
#ifndef TX_ISP_T41_GAMMA_CTL_H
#define TX_ISP_T41_GAMMA_CTL_H
/*
 * IMP_ISP_Tuning_Set/GetGammaAttr (0x08000025).  Wire struct (264 bytes):
 * u32 Curve_type (0 default, 1 sRGB, 2 Rec709, 3 HDR, 4 user), u16 gamma[129].
 * Stock tisp_gamma_set_attr (H20250310a 0x43068) keeps the 264-byte request
 * at info+4.  Type 0 clears the "fixed curve" flag (info+0x420) and lets the
 * exposure-driven calibration curve run again.  Types 1..4 set the flag,
 * load the 129-point curve into info+0x21c and into the calibration RGB
 * curve (params+0x12c), set the strength to 255 (info+0x218, params+0x22e
 * x10) so the strength transform is neutral.  The caller then re-runs the
 * strength transform and LUT writer.  Get (0x43314) refreshes the stored
 * request's curve from info+0x21c and returns it.
 */
#define T41_GAMMA_ATTR_BYTES	264U
#define T41_GAMMA_CURVE_BYTES	258U
#define T41_GAMMA_INFO_ATTR	4U
#define T41_GAMMA_INFO_STRENGTH	0x218U
#define T41_GAMMA_INFO_CURVE	0x21cU
#define T41_GAMMA_INFO_FIXED	0x420U
#define T41_GAMMA_PARAM_CURVE	0x12cU
#define T41_GAMMA_PARAM_STRENGTH 0x22eU
#define T41_GAMMA_INFO_BYTES	0x430U

static inline unsigned int t41_gamma_ctl_le16(const unsigned char *p)
{ return p[0] | (p[1] << 8); }

/* Validates a user request: type < 5 and (user curve) every point a
 * 12-bit value, monotonically non-decreasing is not required by stock. */
static inline int t41_gamma_ctl_check(const unsigned char *attr)
{
	unsigned int type = attr[0] | (attr[1] << 8) | (attr[2] << 16) |
			    ((unsigned int)attr[3] << 24), i;
	if (type > 4)
		return -1;
	if (type == 4)
		for (i = 0; i < 129; ++i)
			if (t41_gamma_ctl_le16(attr + 4 + i * 2) > 4095)
				return -1;
	return 0;
}

/* Stores the request; returns 1 when a fixed curve was installed, 0 for
 * the exposure-driven default.  tables[0..2] = sRGB, Rec709, HDR. */
static inline int t41_gamma_ctl_store(unsigned char *info, unsigned char *params,
		const unsigned char *attr, const unsigned char *const *tables)
{
	unsigned int type = attr[0] | (attr[1] << 8) | (attr[2] << 16) |
			    ((unsigned int)attr[3] << 24), i;
	const unsigned char *curve;

	for (i = 0; i < T41_GAMMA_ATTR_BYTES; ++i)
		info[T41_GAMMA_INFO_ATTR + i] = attr[i];
	if (type == 0) {
		info[T41_GAMMA_INFO_FIXED] = 0;
		return 0;
	}
	info[T41_GAMMA_INFO_FIXED] = 1;
	curve = type == 4 ? attr + 4 : tables[type - 1];
	for (i = 0; i < T41_GAMMA_CURVE_BYTES; ++i) {
		info[T41_GAMMA_INFO_CURVE + i] = curve[i];
		params[T41_GAMMA_PARAM_CURVE + i] = curve[i];
	}
	for (i = 0; i < 10; ++i)
		params[T41_GAMMA_PARAM_STRENGTH + i] = 255;
	info[T41_GAMMA_INFO_STRENGTH] = 255;
	info[T41_GAMMA_INFO_STRENGTH + 1] = 0;
	info[T41_GAMMA_INFO_STRENGTH + 2] = 0;
	info[T41_GAMMA_INFO_STRENGTH + 3] = 0;
	return 1;
}

static inline void t41_gamma_ctl_load(const unsigned char *info, unsigned char *out)
{
	unsigned int i;
	for (i = 0; i < T41_GAMMA_ATTR_BYTES; ++i)
		out[i] = info[T41_GAMMA_INFO_ATTR + i];
	for (i = 0; i < T41_GAMMA_CURVE_BYTES; ++i)
		out[4 + i] = info[T41_GAMMA_INFO_CURVE + i];
}
#endif
