/* SPDX-License-Identifier: MIT */
#ifndef TX_ISP_T41_CSC_CTL_H
#define TX_ISP_T41_CSC_CTL_H
/*
 * IMP_ISP_Tuning_Set/GetISPCSCAttr (0x08000096).  Kernel wire (92 bytes, the
 * layout of the stock CSC tables, tisp_csc_api_set/get): word 0 = version
 * (0 BT601 full, 1 BT601 limited, 2 BT709 full, 3 BT709 limited, 4 BT2020
 * full, 5 BT2020 limited, 6 user), words 1..9 = RGB->YUV matrix Q16,
 * word 10 = bytes {Y offset, UV offset, Y min, Y max}, word 11 = bytes
 * {UV min, UV max, 0, 0}, words 12..20 = YUV->RGB matrix Q16 and words
 * 21/22 = copies of words 10/11.  Versions 0..5 select a preset (the rest
 * of the request is ignored), version 6 installs the request as the user
 * table.  The BCSH and CCM code multiplies the forward and inverse matrix,
 * so both are range checked (|x| <= 4.0).
 */
#define T41_CSC_ATTR_BYTES	92U
#define T41_CSC_VERSION_USER	6U

static inline int t41_csc_ctl_s32(const unsigned char *p)
{
	return (int)(p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned int)p[3] << 24));
}

/* Returns the version, or -1 for a request the driver refuses. */
static inline int t41_csc_ctl_check(const unsigned char *w)
{
	unsigned int version = (unsigned int)t41_csc_ctl_s32(w), i;

	if (version > T41_CSC_VERSION_USER)
		return -1;
	if (version != T41_CSC_VERSION_USER)
		return (int)version;
	for (i = 0; i < 9; ++i) {
		int f = t41_csc_ctl_s32(w + 4 + i * 4);
		int r = t41_csc_ctl_s32(w + 48 + i * 4);
		if (f < -262144 || f > 262144 || r < -262144 || r > 262144)
			return -1;
	}
	if (w[42] > w[43] || w[44] > w[45])	/* clip min <= max */
		return -1;
	return (int)version;
}
#endif
