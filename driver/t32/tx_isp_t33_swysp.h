/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 ysp day/night parameter switch: port of the vendor's tisp_ysp_switch_para.
 *
 * The ysp section of the IQ bin (tparamsP[ch] plus T33_YSP_SEC) is copied, interpolated by the
 * gain word of the block state into the ysp info words and written to the ISP register
 * list (slot number with the list config mode, register offset without).
 */
#ifndef TX_ISP_T33_SWYSP_H
#define TX_ISP_T33_SWYSP_H

#include "tx_isp_t33_blk.h"

#define T33_YSP_SEC	0x907c
#define T33_YSP_SEC_LEN	2724

struct t33_ysp_ops {
	void (*copy)(void *dst, const void *src, u32 n);
	void (*bin_refresh)(u32 ch);
	void (*reg_refresh)(u32 ch);
	int (*dma_state)(u32 id);
	void (*reg_write)(u32 ch, u32 id, u32 slot, u32 val);
	void (*mdns_decode)(u32 ch, u32 *info);
};

struct t33_ysp_ent {
	u16 tp;		/* byte offset in the section */
	u16 info;	/* byte offset in the info block */
	u8 wide;	/* 16 bit table */
};

static inline void t33_ysp_put(u32 *v, const u8 *s, u32 i, u32 f, u32 tp, u32 info, int wide)
{
	v[info / 4] = wide ? t32_tp_intp_u16(i, f, s + tp) : t32_tp_intp_u8(i, f, s + tp);
}

/* the interpolation part of tisp_ysp_switch_para */
static inline void t33_ysp_interp(u32 *v, const u8 *s, u32 gain)
{
	static const struct t33_ysp_ent ent[] = {
		{ 6, 8, 0 }, { 18, 16, 0 }, { 31, 28, 0 }, { 42, 32, 0 }, { 53, 36, 0 }, { 64, 40, 0 },
		{ 76, 48, 1 }, { 98, 52, 0 }, { 109, 56, 0 }, { 122, 68, 1 }, { 144, 72, 0 }, { 155, 76, 0 },
		{ 168, 88, 1 }, { 190, 92, 0 }, { 201, 96, 0 }, { 219, 128, 0 }, { 230, 132, 0 },
		{ 1637, 680, 0 }, { 1648, 684, 0 }, { 1659, 688, 0 },
		{ 1868, 728, 0 }, { 1879, 732, 0 }, { 1890, 736, 0 },
		{ 2688, 1048, 0 }, { 2699, 1052, 0 }, { 2710, 1056, 0 },
	};
	u32 i = (u32)((int)gain >> 16), f = gain & 0xffff;
	unsigned int k;

	for (k = 0; k < ARRAY_SIZE(ent); k++)
		t33_ysp_put(v, s, i, f, ent[k].tp, ent[k].info, ent[k].wide);
	for (k = 0; k <= 125; k++)
		t33_ysp_put(v, s, i, f, 250 + 11 * k, 172 + 4 * k, 0);
	for (k = 0; k < 9; k++)
		t33_ysp_put(v, s, i, f, 1670 + 22 * k, 692 + 4 * k, 1);
	for (k = 0; k <= 70; k++)
		t33_ysp_put(v, s, i, f, 1903 + 11 * k, 748 + 4 * k, 0);
}

static inline u32 t33_ysp_p6(u32 lo, u32 b8, u32 b16, u32 b24)
{
	return (lo & 0x3f) | ((b8 & 0x3f) << 8) | ((b16 & 0x3f) << 16) | ((b24 & 0x3f) << 24);
}

static inline u32 t33_ysp_q4(u32 b0, u32 w8, u32 b16, u32 w24)
{
	return (b0 & 0xff) | ((w8 << 8) & 0xffff) | ((b16 & 0xff) << 16) | (w24 << 24);
}

#define R(o)	(v[(o) >> 2])
#define W(slot, reg, val) \
	o->reg_write(ch, 14, mode ? (slot) : 0x10000 + (reg), (val))
#define P6(a, b, c, d)	t33_ysp_p6(R(a), R(b), R(c), R(d))
#define Q4(a, b, c, d)	t33_ysp_q4(R(a), R(b), R(c), R(d))
#define R3(a, b, c)	t33_ysp_q4(R(a), R(b), R(c), 0)
#define V6	(((R(1080) & 1) << 16) | ((R(1084) & 0x1ff) << 20) | (R(1076) & 0xff))
#define V10	(((R(1108) & 0x7f) << 8) | ((R(1112) & 3) << 16) | (R(1100) & 0x7f))
#define V18	((R(1196) & 0xff) | ((R(1200) << 8) & 0xffff) | ((R(1204) & 3) << 24))
#define V43	(((R(2164) & 1) << 4) | ((R(2168) & 0xf) << 8) | (R(2160) & 7))

/*
 * tisp_ysp_switch_para: v = yspInfo[ch] (word array), tp = tparamsP[ch], params = ysp_paramsP[ch],
 * mode = yspCfgMode, refresh = tispYspRefresh.  Returns 0.
 */
static inline int t33_ysp_switch_para(const struct t33_ysp_ops *o, u32 ch, u8 *tp, u8 *params, u32 *v,
				      u32 mode, u8 *refresh)
{
	const u8 *sec = tp + T33_YSP_SEC;

	v[2172 / 4] = (u32)(unsigned long)sec;
	o->copy(params, sec, T33_YSP_SEC_LEN);
	o->bin_refresh(ch);
	t33_ysp_interp(v, sec, v[2176 / 4]);
	o->reg_refresh(ch);
	if (o->dma_state(14)) {
		*refresh = 2;
		return 0;
	}
	W(6, 4172, V6);
	W(9, 4188, ((R(1104) << 8) & 0xffff) | (R(1096) & 0xff));
	W(10, 4192, V10);
	W(11, 4196, ((R(1120) & 0xff) << 16) | ((R(1124) & 0x1f) << 24) | (R(1116) & 0x1ff));
	W(13, 4204, ((R(1140) & 0xff) << 16) | ((R(1144) & 0x1f) << 24) | (R(1136) & 0x1ff));
	W(15, 4212, ((R(1160) & 0xff) << 16) | ((R(1164) & 0x1f) << 24) | (R(1156) & 0x1ff));
	W(18, 4224, V18);
	W(21, 4236, ((R(1244) & 0x7f) << 8) | ((R(1248) & 0x7f) << 16) | (R(1240) & 0x7f) |
		    ((R(1252) & 0x7f) << 24));
	W(22, 4240, (R(1256) & 0xff) | ((R(1296) & 0x3fff) << 16));
	W(23, 4244, (R(1288) & 0xff) | ((R(1328) & 0x3fff) << 16));
	W(24, 4248, R(1292) & 0x7f);
	W(45, 4252, P6(1332, 1336, 1340, 1344));
	W(46, 4252, P6(1348, 1352, 1356, 1360));
	W(47, 4252, P6(1364, 1368, 1372, 1376));
	W(48, 4252, P6(1392, 1388, 1384, 1380));
	W(49, 4252, P6(1408, 1404, 1400, 1396));
	W(50, 4252, P6(1424, 1420, 1416, 1412));
	W(51, 4252, P6(1440, 1436, 1432, 1428));
	W(52, 4252, P6(1456, 1452, 1448, 1444));
	W(54, 4316, P6(1460, 1464, 1468, 1472));
	W(55, 4316, P6(1476, 1480, 1484, 1488));
	W(56, 4316, P6(1492, 1496, 1500, 1504));
	W(57, 4316, P6(1520, 1516, 1512, 1508));
	W(58, 4316, P6(1536, 1532, 1528, 1524));
	W(59, 4316, P6(1552, 1548, 1544, 1540));
	W(60, 4316, P6(1568, 1564, 1560, 1556));
	W(61, 4316, P6(1584, 1580, 1576, 1572));
	W(63, 4380, Q4(1588, 1592, 1596, 1600));
	W(64, 4380, Q4(1604, 1608, 1612, 1616));
	W(65, 4380, Q4(1620, 1624, 1628, 1632));
	W(66, 4380, Q4(1636, 1640, 1644, 1648));
	W(68, 4412, Q4(1652, 1656, 1660, 1664));
	W(69, 4412, Q4(1668, 1672, 1676, 1680));
	W(70, 4412, Q4(1684, 1688, 1692, 1696));
	W(71, 4412, Q4(1700, 1704, 1708, 1712));
	W(73, 4444, P6(1716, 1720, 1724, 1728));
	W(74, 4444, P6(1736, 1744, 1732, 1740));
	W(75, 4444, P6(1752, 1760, 1748, 1756));
	W(76, 4444, P6(1768, 1776, 1764, 1772));
	W(26, 4476, (R(1784) & 0xff) | ((R(1788) & 0xff) << 16));
	W(27, 4480, (R(1792) & 7) | ((R(1796) & 0x1ff) << 8));
	W(78, 4484, (R(1800) & 0x1ff) | ((R(1804) & 0x1ff) << 16));
	W(79, 4484, (R(1808) & 0x1ff) | ((R(1812) & 0x1ff) << 16));
	W(80, 4484, (R(1816) & 0x1ff) | ((R(1820) & 0x1ff) << 16));
	W(81, 4484, (R(1824) & 0x1ff) | ((R(1828) & 0x1ff) << 16));
	W(29, 4500, R(1832) & 0xff);
	W(83, 4504, Q4(1260, 1264, 1268, 1272));
	W(84, 4504, R3(1276, 1280, 1284));
	W(86, 4520, (R(1300) & 0x3fff) | ((R(1304) & 0x3fff) << 16));
	W(87, 4520, (R(1308) & 0x3fff) | ((R(1312) & 0x3fff) << 16));
	W(88, 4520, (R(1316) & 0x3fff) | ((R(1320) & 0x3fff) << 16));
	W(31, 4532, R(1324) & 0x3fff);
	W(33, 4544, (R(1836) & 0xff) | ((R(1840) << 8) & 0xffff));
	W(35, 4552, (R(1852) & 0xff) | ((R(1856) & 0xff) << 16));
	W(36, 4556, (R(1860) & 0x7f) | ((R(1864) & 0x7f) << 16));
	W(37, 4560, R3(1868, 1872, 1876));
	W(90, 4564, Q4(1880, 1884, 1888, 1892));
	W(91, 4564, Q4(1896, 1900, 1904, 1908));
	W(93, 4572, Q4(1912, 1916, 1920, 1924));
	W(94, 4572, Q4(1928, 1932, 1936, 1940));
	W(96, 4580, Q4(1944, 1948, 1952, 1956));
	W(97, 4580, R3(1960, 1964, 1968));
	W(99, 4588, R3(1972, 1976, 1980));
	W(100, 4588, R3(1984, 1988, 1992));
	W(101, 4588, R3(1996, 2000, 2004));
	W(102, 4588, R3(2008, 2012, 2016));
	W(103, 4588, R3(2020, 2024, 2028));
	W(105, 4608, Q4(2032, 2036, 2040, 2044));
	W(106, 4608, R3(2048, 2052, 2056));
	W(108, 4616, Q4(2060, 2064, 2068, 2072));
	W(109, 4616, R3(2076, 2080, 2084));
	W(111, 4624, Q4(2088, 2092, 2096, 2100));
	W(112, 4624, R3(2104, 2108, 2112));
	W(39, 4632, ((R(2120) & 1) << 1) | ((R(2124) & 1) << 2) | (R(2116) & 1) | ((R(2128) & 1) << 3) |
		    ((R(2132) & 1) << 4));
	W(42, 4644, ((R(2156) & 0xff) << 4) | (R(2152) & 7));
	W(43, 4648, V43);
	/* second pass */
	W(3, 4160, R(1780) & 0x1f);
	W(5, 4168, ((R(1068) & 3) << 16) | ((R(1072) & 3) << 20));
	W(6, 4172, V6);
	W(8, 4184, ((R(1088) << 4) & 0xff) | ((R(1092) << 8) & 0xffff));
	W(10, 4192, V10);
	W(12, 4200, (R(1128) & 0xff) | ((R(1132) & 0xff) << 16));
	W(14, 4208, (R(1148) & 0xff) | ((R(1152) & 0xff) << 16));
	W(16, 4216, (R(1168) & 0xff) | ((R(1172) << 8) & 0xffff) | ((R(1176) & 0x1f) << 16));
	W(17, 4220, (R(1180) & 1) | ((R(1184) & 3) << 4) | ((R(1188) & 3) << 8) | ((R(1192) & 3) << 12));
	W(18, 4224, V18);
	W(19, 4228, (R(1208) & 0x1f) | ((R(1212) & 7) << 8) | ((R(1216) & 7) << 12));
	W(20, 4232, (R(1220) & 7) | ((R(1224) & 7) << 4) | ((R(1228) & 7) << 8) | ((R(1232) & 7) << 12) |
		    ((R(1236) & 0xf) << 16));
	W(34, 4548, (R(1844) & 0xff) | ((R(1848) & 1) << 16));
	W(41, 4640, (R(2136) & 1) | ((R(2140) & 1) << 4) | ((R(2144) & 1) << 8) | ((R(2148) & 1) << 12));
	W(43, 4648, V43);
	o->mdns_decode(ch, v + 1068 / 4);
	W(1, 4112, 1);
	*refresh = 1;
	return 0;
}

#undef R
#undef W
#undef P6
#undef Q4
#undef R3
#undef V6
#undef V10
#undef V18
#undef V43

#endif /* TX_ISP_T33_SWYSP_H */
