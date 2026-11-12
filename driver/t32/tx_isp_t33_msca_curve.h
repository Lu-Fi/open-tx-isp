/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) MSCA polyphase curves through module list 23
 * (tisp_msca_t33_curve_calc + tisp_msca_t33_ch_curve_write, CfgMode
 * slots).  Per channel the ratios are out << 14 / in (at most 0x6aaa);
 * tisp_sin samples a kernel table at n + 1 points (step ratio * 8 / n,
 * linear interpolation, segments of 1024), then each set is normalised to
 * 8 phases of T taps summing to 2048 (tap t of phase p at |pos[t] - p|; the
 * rounding error goes to the taps of the add order first, or is taken
 * from the sub order).  Channel 0: 4-tap vertical, 4-tap horizontal,
 * 2-tap vertical, 2-tap horizontal (port 0xf0560, slots 0xcc..);
 * channels 1 / 2: 4-tap vertical, 6-tap horizontal, 2-tap vertical, 3-tap
 * horizontal (ports 0xf0570 / 0xf0580, slots 0xe9.. / 0x10c..).  Each set
 * is packed on its own: word i = c[2i + 1] << 16 | c[2i], 13 bits each.
 *
 * Kernel tables: the 8 and 12 interval tables are computed
 * (tools/t33_msca_sin_tab.py, equal to the vendor's); the 16 and 24
 * interval tables are data transcribed from the vendor T33 module
 * (lut_17 / lut_25 in its .rodata), which the computed ones miss by 1 in
 * 41 / 58 entries.
 */
#ifndef TX_ISP_T33_MSCA_CURVE_H
#define TX_ISP_T33_MSCA_CURVE_H

static const short t33_msca_tab8[257] = {
	1535, 1535, 1534, 1533, 1532, 1530, 1528, 1526, 1523, 1519, 1516, 1512,
	1507, 1503, 1498, 1492, 1486, 1480, 1474, 1467, 1459, 1452, 1444, 1436,
	1427, 1418, 1409, 1399, 1389, 1379, 1369, 1358, 1347, 1335, 1324, 1312,
	1300, 1287, 1275, 1262, 1248, 1235, 1221, 1208, 1194, 1179, 1165, 1150,
	1135, 1120, 1105, 1090, 1074, 1059, 1043, 1027, 1011, 995, 979, 963,
	946, 930, 913, 896, 880, 863, 846, 830, 813, 796, 779, 762,
	745, 729, 712, 695, 678, 661, 645, 628, 612, 595, 579, 563,
	546, 530, 514, 498, 483, 467, 451, 436, 421, 406, 391, 376,
	361, 347, 332, 318, 304, 291, 277, 264, 251, 238, 225, 212,
	200, 188, 176, 164, 152, 141, 130, 119, 109, 98, 88, 78,
	69, 59, 50, 41, 32, 24, 16, 8, 0, -8, -15, -22,
	-29, -35, -42, -48, -53, -59, -65, -70, -75, -79, -84, -88,
	-92, -96, -100, -103, -106, -109, -112, -115, -117, -120, -122, -123,
	-125, -127, -128, -129, -130, -131, -132, -132, -132, -133, -133, -133,
	-132, -132, -132, -131, -130, -129, -129, -128, -126, -125, -124, -122,
	-121, -119, -118, -116, -114, -112, -110, -108, -106, -104, -102, -100,
	-98, -96, -93, -91, -89, -86, -84, -82, -79, -77, -75, -72,
	-70, -67, -65, -63, -60, -58, -56, -54, -51, -49, -47, -45,
	-43, -41, -39, -37, -35, -33, -31, -29, -27, -26, -24, -23,
	-21, -19, -18, -17, -15, -14, -13, -12, -10, -9, -8, -7,
	-7, -6, -5, -4, -4, -3, -3, -2, -2, -1, -1, -1,
	0, 0, 0, 0, 0,
};
static const short t33_msca_tab12[257] = {
	1535, 1535, 1533, 1531, 1528, 1524, 1519, 1514, 1507, 1500, 1492, 1483,
	1474, 1463, 1452, 1440, 1427, 1413, 1399, 1384, 1369, 1352, 1335, 1318,
	1300, 1281, 1262, 1242, 1221, 1201, 1179, 1158, 1135, 1113, 1090, 1067,
	1043, 1019, 995, 971, 946, 921, 896, 871, 846, 821, 796, 771,
	745, 720, 695, 670, 645, 620, 595, 571, 546, 522, 498, 475,
	451, 428, 406, 383, 361, 340, 318, 298, 277, 257, 238, 218,
	200, 182, 164, 147, 130, 114, 98, 83, 69, 55, 41, 28,
	16, 4, -8, -18, -29, -38, -48, -56, -65, -72, -79, -86,
	-92, -98, -103, -108, -112, -116, -120, -123, -125, -127, -129, -130,
	-132, -132, -133, -133, -132, -132, -131, -130, -129, -127, -125, -123,
	-121, -118, -116, -113, -110, -107, -104, -101, -98, -94, -91, -87,
	-84, -80, -77, -73, -70, -66, -63, -59, -56, -53, -49, -46,
	-43, -40, -37, -34, -31, -28, -26, -23, -21, -19, -17, -15,
	-13, -11, -9, -8, -7, -5, -4, -3, -3, -2, -1, -1,
	0, 0, 0, 0, 0, 0, -1, -1, -1, -2, -3, -3,
	-4, -5, -6, -7, -8, -9, -10, -11, -12, -13, -14, -15,
	-17, -18, -19, -20, -21, -22, -24, -25, -26, -27, -28, -29,
	-30, -30, -31, -32, -33, -33, -34, -34, -35, -35, -35, -36,
	-36, -36, -36, -36, -36, -36, -35, -35, -35, -34, -34, -33,
	-33, -32, -31, -30, -29, -28, -28, -27, -25, -24, -23, -22,
	-21, -20, -18, -17, -16, -15, -13, -12, -11, -9, -8, -7,
	-5, -4, -3, -1, 0,
};
static const short t33_msca_tab16[257] = {
	1535, 1534, 1532, 1528, 1523, 1516, 1507, 1497, 1486, 1473, 1459, 1444,
	1427, 1409, 1389, 1369, 1347, 1324, 1300, 1274, 1248, 1221, 1194, 1165,
	1135, 1105, 1074, 1043, 1011, 979, 946, 913, 880, 847, 813, 779,
	745, 711, 678, 645, 612, 579, 546, 514, 483, 452, 421, 391,
	361, 332, 304, 277, 251, 225, 200, 175, 152, 130, 109, 89,
	69, 50, 32, 16, 0, -15, -29, -41, -53, -64, -75, -84,
	-92, -99, -106, -112, -117, -121, -125, -128, -130, -131, -132, -132,
	-132, -131, -130, -128, -126, -124, -121, -118, -114, -110, -106, -102,
	-98, -94, -89, -84, -79, -75, -70, -65, -60, -55, -51, -47,
	-43, -39, -35, -31, -27, -24, -21, -18, -15, -12, -10, -8,
	-7, -5, -4, -3, -2, -1, 0, 0, 0, 0, 0, 0,
	-1, -2, -3, -4, -5, -7, -8, -9, -10, -12, -14, -16,
	-17, -19, -20, -22, -23, -25, -26, -27, -28, -30, -31, -32,
	-33, -34, -34, -35, -35, -36, -36, -36, -36, -36, -36, -36,
	-35, -34, -33, -33, -32, -31, -29, -28, -27, -26, -24, -23,
	-21, -20, -18, -16, -14, -13, -11, -9, -7, -5, -3, -1,
	0, 2, 3, 5, 6, 8, 9, 11, 12, 13, 14, 15,
	16, 17, 18, 19, 19, 20, 20, 21, 21, 21, 21, 21,
	21, 21, 21, 21, 20, 20, 19, 19, 18, 18, 17, 16,
	15, 15, 14, 13, 12, 12, 11, 10, 9, 8, 7, 6,
	6, 6, 5, 4, 3, 2, 2, 2, 2, 2, 1, 0,
	0, 0, 0, 0, 0,
};
static const short t33_msca_tab24[257] = {
	1535, 1533, 1528, 1520, 1507, 1492, 1473, 1452, 1427, 1399, 1369, 1336,
	1300, 1261, 1221, 1180, 1135, 1089, 1043, 995, 946, 897, 847, 796,
	745, 695, 645, 595, 546, 499, 452, 406, 361, 318, 277, 238,
	200, 164, 130, 99, 69, 41, 16, -8, -29, -47, -64, -80,
	-92, -103, -112, -119, -125, -129, -131, -132, -132, -131, -128, -125,
	-121, -116, -110, -104, -98, -91, -84, -77, -70, -62, -55, -49,
	-43, -37, -31, -25, -21, -16, -12, -9, -7, -5, -3, -1,
	0, 0, 0, 0, -1, -2, -4, -6, -8, -9, -12, -15,
	-17, -19, -22, -24, -26, -27, -30, -32, -33, -34, -35, -35,
	-36, -36, -36, -36, -35, -33, -33, -31, -29, -28, -26, -23,
	-21, -19, -16, -13, -11, -8, -5, -2, 0, 2, 4, 7,
	9, 11, 13, 14, 16, 18, 19, 19, 20, 21, 21, 21,
	21, 21, 21, 20, 19, 18, 18, 17, 15, 14, 13, 12,
	11, 10, 8, 7, 6, 5, 4, 3, 2, 2, 2, 1,
	0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 3,
	4, 5, 5, 6, 7, 7, 7, 8, 9, 10, 10, 10,
	11, 11, 11, 11, 11, 11, 11, 11, 10, 10, 10, 9,
	8, 7, 7, 6, 5, 4, 3, 2, 1, 0, 0, -1,
	-2, -3, -3, -4, -5, -6, -7, -7, -7, -8, -8, -8,
	-8, -8, -8, -8, -8, -8, -8, -7, -7, -6, -6, -6,
	-5, -4, -4, -4, -3, -3, -3, -2, -1, -1, -1, -1,
	0, 0, 0, 0, 0,
};

#define T33_MSCA_RATIO_MAX	0x6aaaU
#define T33_MSCA_CURVE_MAX	25

static inline unsigned int t33_msca_ratio(unsigned int out, unsigned int in)
{
	unsigned int r = in ? (out << 14) / in : T33_MSCA_RATIO_MAX;

	return r > T33_MSCA_RATIO_MAX ? T33_MSCA_RATIO_MAX : r;
}

/* tisp_sin: n + 1 samples (n = 8, 12, 16, 24) */
static inline void t33_msca_sin(unsigned int ratio, unsigned int n, int *out)
{
	const short *tab = n == 8 ? t33_msca_tab8 : n == 12 ? t33_msca_tab12 : n == 16 ? t33_msca_tab16 :
			   t33_msca_tab24;
	unsigned int step = (ratio << 3) / n, i;

	for (i = 0; i <= n; i++) {
		unsigned int x = i * step, seg = x >> 10, f = x & 1023;
		int a = seg < 257 ? tab[seg] : 0, b = seg + 1 < 257 ? tab[seg + 1] : 0;
		int v = b * (int)f + a * (int)(1024 - f);

		out[i] = (short)((v >> 10) + ((v >> 9) & 1));
	}
}

/* tisp_msca_t33_{two,thr,four,six}_point_normalized: c[0..n] in place, 8 phases */
static inline void t33_msca_norm(int *c, unsigned int n, unsigned int taps, const signed char *pos,
				 const unsigned char *add, const unsigned char *sub)
{
	int in[T33_MSCA_CURVE_MAX], tap[6], k[6], p, t, sum, err;

	for (t = 0; t <= (int)n; t++)
		in[t] = c[t];
	for (t = 0; t <= (int)n; t++)
		c[t] = 0;
	for (p = 0; p < 8; p++) {
		sum = 0;
		for (t = 0; t < (int)taps; t++) {
			k[t] = pos[t] - p < 0 ? p - pos[t] : pos[t] - p;
			sum += in[k[t]];
		}
		if (!sum)
			continue;
		err = 0;
		for (t = 0; t < (int)taps; t++) {
			int v = in[k[t]] * 0x800 + (in[k[t]] < 0 ? -(sum / 2) : sum / 2);

			tap[t] = v / sum;
			err += tap[t];
		}
		err -= 0x800;
		for (t = 0; err < 0 && t < -err && t < (int)taps; t++)
			tap[add[t]]++;
		for (t = 0; err > 0 && t < err && t < (int)taps; t++)
			tap[sub[t]]--;
		for (t = 0; t < (int)taps; t++)
			c[k[t]] = tap[t];
	}
}

/* n + 1 coefficients of a taps-tap filter at ratio into the list from slot s; returns the next slot */
static inline unsigned int t33_msca_set(u32 *l, unsigned int s, unsigned int ratio, unsigned int taps)
{
	static const signed char p2[2] = { 0, 8 }, p3[3] = { -4, 4, 12 }, p4[4] = { -8, 0, 8, 16 },
				 p6[6] = { -16, -8, 0, 8, 16, 24 };
	static const unsigned char a2[2] = { 0, 1 }, s2[2] = { 1, 0 }, a3[3] = { 1, 0, 2 }, s3[3] = { 2, 0, 1 },
				   a4[4] = { 1, 2, 0, 3 }, s4[4] = { 3, 0, 2, 1 },
				   a6[6] = { 2, 3, 1, 4, 0, 5 }, s6[6] = { 5, 0, 4, 1, 3, 2 };
	int c[T33_MSCA_CURVE_MAX + 1];
	unsigned int n = taps * 4, i;

	t33_msca_sin(ratio, n, c);
	if (taps == 2)
		t33_msca_norm(c, n, 2, p2, a2, s2);
	else if (taps == 3)
		t33_msca_norm(c, n, 3, p3, a3, s3);
	else if (taps == 4)
		t33_msca_norm(c, n, 4, p4, a4, s4);
	else
		t33_msca_norm(c, n, 6, p6, a6, s6);
	c[n + 1] = 0;
	for (i = 0; i <= n; i += 2)
		l[1 + s++] = ((u32)c[i + 1] & 0x1fff) << 16 | ((u32)c[i] & 0x1fff);
	return s;
}

/* the curve slots of channel ch: rh / rv the horizontal / vertical ratios */
static inline void t33_msca_curves(u32 *l, int ch, unsigned int rh, unsigned int rv)
{
	unsigned int s = ch == 0 ? 0xcc : ch == 1 ? 0xe9 : 0x10c;

	s = t33_msca_set(l, s, rv, 4);
	s = t33_msca_set(l, s, rh, ch ? 6 : 4);
	s = t33_msca_set(l, s, rv, 2);
	t33_msca_set(l, s, rh, ch ? 3 : 2);
}

#endif /* TX_ISP_T33_MSCA_CURVE_H */
