/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) YSP per-frame refresh: tisp_ysp_par_refresh.  When the gain moved by at least a
 * threshold, the block state (the vendor's yspInfo[ch], a word array) is refreshed: the 16.16 gain is
 * interpolated over the section tables (section pointer in state word 543), tisp_ysp_reg_data_refresh
 * derives the register values, and they are written (list 14) either as register addresses
 * (yspCfgMode 0) or as list slots (yspCfgMode != 0).
 */
#ifndef TX_ISP_T33_I9M_H
#define TX_ISP_T33_I9M_H

#include "tx_isp_t33_ysp.h"

#ifndef T33_YSPPR_GLUE
extern u32 *yspInfo[4];
extern u8 yspCfgMode;
extern u8 tispYspRefresh;
extern void tisp_ysp_bin_data_refresh(u32 ch);
extern void tisp_ysp_reg_data_refresh(u32 ch);
extern int tisp_rcfg_get_dma_state(u32 id);
extern void tisp_reg_write_t33(u32 ch, u32 id, u32 where, u32 val);
extern void tisp_mdns_decode_reg_write(u32 ch, u32 *p);
extern u32 tisp_simple_intp_uint8_t(u32 i, u32 f, const u8 *s);
extern u32 tisp_simple_intp_uint16_t(u32 i, u32 f, const u8 *s);
#endif

/* one store: the register address (mode 0) or the list slot */
static inline void t33_ysp_wr(u32 ch, u32 slot, u32 imm, u32 val)
{
	tisp_reg_write_t33(ch, 14, yspCfgMode ? slot : 0x10000 + imm, val);
}

#define Y(o)	(v[(o) / 4])

/* four 6 bit fields (byte offsets of the state words, lowest field first) */
static inline u32 t33_ysp_p4(const u32 *v, u32 o0, u32 o1, u32 o2, u32 o3)
{
	return (Y(o0) & 0x3f) | ((Y(o1) & 0x3f) << 8) | ((Y(o2) & 0x3f) << 16) | ((Y(o3) & 0x3f) << 24);
}

/* byte, 8 bit field at +8, byte, word at +24 */
static inline u32 t33_ysp_q4(const u32 *v, u32 o0, u32 o1, u32 o2, u32 o3)
{
	return (Y(o0) & 0xff) | ((Y(o1) << 8) & 0xffff) | ((Y(o2) & 0xff) << 16) | (Y(o3) << 24);
}

static inline u32 t33_ysp_r3(const u32 *v, u32 o0, u32 o1, u32 o2)
{
	return (Y(o0) & 0xff) | ((Y(o1) << 8) & 0xffff) | ((Y(o2) & 0xff) << 16);
}

static inline void t33_ysp_pr_intp(u32 *v, u32 gain)
{
	const u8 *sec = (const u8 *)(unsigned long)v[2172 / 4];
	u32 i = gain >> 16, f = gain & 0xffff;
	unsigned int k;

	for (k = 0; k < ARRAY_SIZE(t33_ysp_intp); k++)
		v[t33_ysp_intp[k].word] = t33_ysp_intp[k].bits == 8 ?
			tisp_simple_intp_uint8_t(i, f, sec + t33_ysp_intp[k].off) :
			tisp_simple_intp_uint16_t(i, f, sec + t33_ysp_intp[k].off);
}

/* tisp_ysp_par_refresh */
static inline void t33_ysp_par_refresh(u32 ch, u32 gain, u32 thr)
{
	u32 *v = yspInfo[ch];
	u32 old = v[2176 / 4], diff;
	u32 c = ch & 0xff;

	if (old != 0xffffffffU) {
		diff = gain > old ? gain - old : old - gain;
		if (diff < thr)
			return;
	}
	v[2176 / 4] = gain;
	tisp_ysp_bin_data_refresh(c);
	v = yspInfo[c];
	t33_ysp_pr_intp(v, v[2176 / 4]);
	tisp_ysp_reg_data_refresh(c);
	if (tisp_rcfg_get_dma_state(14)) {
		tispYspRefresh = 2;
		return;
	}
	v = yspInfo[c];

	t33_ysp_wr(c, 6, 4172, (Y(1076) & 0xff) | ((Y(1080) & 1) << 16) | ((Y(1084) & 0x1ff) << 20));
	t33_ysp_wr(c, 9, 4188, (Y(1096) & 0xff) | ((Y(1104) << 8) & 0xffff));
	t33_ysp_wr(c, 10, 4192, (Y(1100) & 0x7f) | ((Y(1108) & 0x7f) << 8) | ((Y(1112) & 3) << 16));
	t33_ysp_wr(c, 11, 4196, (Y(1116) & 0x1ff) | ((Y(1120) & 0xff) << 16) | ((Y(1124) & 0x1f) << 24));
	t33_ysp_wr(c, 13, 4204, (Y(1136) & 0x1ff) | ((Y(1140) & 0xff) << 16) | ((Y(1144) & 0x1f) << 24));
	t33_ysp_wr(c, 15, 4212, (Y(1156) & 0x1ff) | ((Y(1160) & 0xff) << 16) | ((Y(1164) & 0x1f) << 24));
	t33_ysp_wr(c, 18, 4224, (Y(1196) & 0xff) | ((Y(1200) << 8) & 0xffff) | ((Y(1204) & 3) << 24));
	t33_ysp_wr(c, 21, 4236, (Y(1240) & 0x7f) | ((Y(1244) & 0x7f) << 8) | ((Y(1248) & 0x7f) << 16) |
		   ((Y(1252) & 0x7f) << 24));
	t33_ysp_wr(c, 22, 4240, (Y(1256) & 0xff) | ((Y(1296) & 0x3fff) << 16));
	t33_ysp_wr(c, 23, 4244, (Y(1288) & 0xff) | ((Y(1328) & 0x3fff) << 16));
	t33_ysp_wr(c, 24, 4248, Y(1292) & 0x7f);
	t33_ysp_wr(c, 45, 4252, t33_ysp_p4(v, 1332, 1336, 1340, 1344));
	t33_ysp_wr(c, 46, 4252, t33_ysp_p4(v, 1348, 1352, 1356, 1360));
	t33_ysp_wr(c, 47, 4252, t33_ysp_p4(v, 1364, 1368, 1372, 1376));
	t33_ysp_wr(c, 48, 4252, t33_ysp_p4(v, 1392, 1388, 1384, 1380));
	t33_ysp_wr(c, 49, 4252, t33_ysp_p4(v, 1408, 1404, 1400, 1396));
	t33_ysp_wr(c, 50, 4252, t33_ysp_p4(v, 1424, 1420, 1416, 1412));
	t33_ysp_wr(c, 51, 4252, t33_ysp_p4(v, 1440, 1436, 1432, 1428));
	t33_ysp_wr(c, 52, 4252, t33_ysp_p4(v, 1456, 1452, 1448, 1444));
	t33_ysp_wr(c, 54, 4316, t33_ysp_p4(v, 1460, 1464, 1468, 1472));
	t33_ysp_wr(c, 55, 4316, t33_ysp_p4(v, 1476, 1480, 1484, 1488));
	t33_ysp_wr(c, 56, 4316, t33_ysp_p4(v, 1492, 1496, 1500, 1504));
	t33_ysp_wr(c, 57, 4316, t33_ysp_p4(v, 1520, 1516, 1512, 1508));
	t33_ysp_wr(c, 58, 4316, t33_ysp_p4(v, 1536, 1532, 1528, 1524));
	t33_ysp_wr(c, 59, 4316, t33_ysp_p4(v, 1552, 1548, 1544, 1540));
	t33_ysp_wr(c, 60, 4316, t33_ysp_p4(v, 1568, 1564, 1560, 1556));
	t33_ysp_wr(c, 61, 4316, t33_ysp_p4(v, 1584, 1580, 1576, 1572));
	t33_ysp_wr(c, 63, 4380, t33_ysp_q4(v, 1588, 1592, 1596, 1600));
	t33_ysp_wr(c, 64, 4380, t33_ysp_q4(v, 1604, 1608, 1612, 1616));
	t33_ysp_wr(c, 65, 4380, t33_ysp_q4(v, 1620, 1624, 1628, 1632));
	t33_ysp_wr(c, 66, 4380, t33_ysp_q4(v, 1636, 1640, 1644, 1648));
	t33_ysp_wr(c, 68, 4412, t33_ysp_q4(v, 1652, 1656, 1660, 1664));
	t33_ysp_wr(c, 69, 4412, t33_ysp_q4(v, 1668, 1672, 1676, 1680));
	t33_ysp_wr(c, 70, 4412, t33_ysp_q4(v, 1684, 1688, 1692, 1696));
	t33_ysp_wr(c, 71, 4412, t33_ysp_q4(v, 1700, 1704, 1708, 1712));
	t33_ysp_wr(c, 73, 4444, t33_ysp_p4(v, 1716, 1720, 1724, 1728));
	t33_ysp_wr(c, 74, 4444, t33_ysp_p4(v, 1736, 1744, 1732, 1740));
	t33_ysp_wr(c, 75, 4444, t33_ysp_p4(v, 1752, 1760, 1748, 1756));
	t33_ysp_wr(c, 76, 4444, t33_ysp_p4(v, 1768, 1776, 1764, 1772));
	t33_ysp_wr(c, 26, 4476, (Y(1784) & 0xff) | ((Y(1788) & 0xff) << 16));
	t33_ysp_wr(c, 27, 4480, (Y(1792) & 7) | ((Y(1796) & 0x1ff) << 8));
	t33_ysp_wr(c, 78, 4484, (Y(1800) & 0x1ff) | ((Y(1804) & 0x1ff) << 16));
	t33_ysp_wr(c, 79, 4484, (Y(1808) & 0x1ff) | ((Y(1812) & 0x1ff) << 16));
	t33_ysp_wr(c, 80, 4484, (Y(1816) & 0x1ff) | ((Y(1820) & 0x1ff) << 16));
	t33_ysp_wr(c, 81, 4484, (Y(1824) & 0x1ff) | ((Y(1828) & 0x1ff) << 16));
	t33_ysp_wr(c, 29, 4500, Y(1832) & 0xff);
	t33_ysp_wr(c, 83, 4504, t33_ysp_q4(v, 1260, 1264, 1268, 1272));
	t33_ysp_wr(c, 84, 4504, t33_ysp_r3(v, 1276, 1280, 1284));
	t33_ysp_wr(c, 86, 4520, (Y(1300) & 0x3fff) | ((Y(1304) & 0x3fff) << 16));
	t33_ysp_wr(c, 87, 4520, (Y(1308) & 0x3fff) | ((Y(1312) & 0x3fff) << 16));
	t33_ysp_wr(c, 88, 4520, (Y(1316) & 0x3fff) | ((Y(1320) & 0x3fff) << 16));
	t33_ysp_wr(c, 31, 4532, Y(1324) & 0x3fff);
	t33_ysp_wr(c, 33, 4544, (Y(1836) & 0xff) | ((Y(1840) << 8) & 0xffff));
	t33_ysp_wr(c, 35, 4552, (Y(1852) & 0xff) | ((Y(1856) & 0xff) << 16));
	t33_ysp_wr(c, 36, 4556, (Y(1860) & 0x7f) | ((Y(1864) & 0x7f) << 16));
	t33_ysp_wr(c, 37, 4560, t33_ysp_r3(v, 1868, 1872, 1876));
	t33_ysp_wr(c, 90, 4564, t33_ysp_q4(v, 1880, 1884, 1888, 1892));
	t33_ysp_wr(c, 91, 4564, t33_ysp_q4(v, 1896, 1900, 1904, 1908));
	t33_ysp_wr(c, 93, 4572, t33_ysp_q4(v, 1912, 1916, 1920, 1924));
	t33_ysp_wr(c, 94, 4572, t33_ysp_q4(v, 1928, 1932, 1936, 1940));
	t33_ysp_wr(c, 96, 4580, t33_ysp_q4(v, 1944, 1948, 1952, 1956));
	t33_ysp_wr(c, 97, 4580, t33_ysp_r3(v, 1960, 1964, 1968));
	t33_ysp_wr(c, 99, 4588, t33_ysp_r3(v, 1972, 1976, 1980));
	t33_ysp_wr(c, 100, 4588, t33_ysp_r3(v, 1984, 1988, 1992));
	t33_ysp_wr(c, 101, 4588, t33_ysp_r3(v, 1996, 2000, 2004));
	t33_ysp_wr(c, 102, 4588, t33_ysp_r3(v, 2008, 2012, 2016));
	t33_ysp_wr(c, 103, 4588, t33_ysp_r3(v, 2020, 2024, 2028));
	t33_ysp_wr(c, 105, 4608, t33_ysp_q4(v, 2032, 2036, 2040, 2044));
	t33_ysp_wr(c, 106, 4608, t33_ysp_r3(v, 2048, 2052, 2056));
	t33_ysp_wr(c, 108, 4616, t33_ysp_q4(v, 2060, 2064, 2068, 2072));
	t33_ysp_wr(c, 109, 4616, t33_ysp_r3(v, 2076, 2080, 2084));
	t33_ysp_wr(c, 111, 4624, t33_ysp_q4(v, 2088, 2092, 2096, 2100));
	t33_ysp_wr(c, 112, 4624, t33_ysp_r3(v, 2104, 2108, 2112));
	t33_ysp_wr(c, 39, 4632, (Y(2116) & 1) | ((Y(2120) & 1) << 1) | ((Y(2124) & 1) << 2) |
		   ((Y(2128) & 1) << 3) | ((Y(2132) & 1) << 4));
	t33_ysp_wr(c, 42, 4644, ((Y(2156) & 0xff) << 4) | (Y(2152) & 7));
	t33_ysp_wr(c, 43, 4648, (Y(2160) & 7) | ((Y(2164) & 1) << 4) | ((Y(2168) & 0xf) << 8));

	v = yspInfo[c];
	t33_ysp_wr(c, 3, 4160, Y(1780) & 0x1f);
	t33_ysp_wr(c, 5, 4168, ((Y(1068) & 3) << 16) | ((Y(1072) & 3) << 20));
	t33_ysp_wr(c, 6, 4172, (Y(1076) & 0xff) | ((Y(1080) & 1) << 16) | ((Y(1084) & 0x1ff) << 20));
	t33_ysp_wr(c, 8, 4184, ((Y(1088) << 4) & 0xff) | ((Y(1092) << 8) & 0xffff));
	t33_ysp_wr(c, 10, 4192, (Y(1100) & 0x7f) | ((Y(1108) & 0x7f) << 8) | ((Y(1112) & 3) << 16));
	t33_ysp_wr(c, 12, 4200, (Y(1128) & 0xff) | ((Y(1132) & 0xff) << 16));
	t33_ysp_wr(c, 14, 4208, (Y(1148) & 0xff) | ((Y(1152) & 0xff) << 16));
	t33_ysp_wr(c, 16, 4216, (Y(1168) & 0xff) | ((Y(1172) << 8) & 0xffff) | ((Y(1176) & 0x1f) << 16));
	t33_ysp_wr(c, 17, 4220, (Y(1180) & 1) | ((Y(1184) & 3) << 4) | ((Y(1188) & 3) << 8) |
		   ((Y(1192) & 3) << 12));
	t33_ysp_wr(c, 18, 4224, (Y(1196) & 0xff) | ((Y(1200) << 8) & 0xffff) | ((Y(1204) & 3) << 24));
	t33_ysp_wr(c, 19, 4228, (Y(1208) & 0x1f) | ((Y(1212) & 7) << 8) | ((Y(1216) & 7) << 12));
	t33_ysp_wr(c, 20, 4232, (Y(1220) & 7) | ((Y(1224) & 7) << 4) | ((Y(1228) & 7) << 8) |
		   ((Y(1232) & 7) << 12) | ((Y(1236) & 0xf) << 16));
	t33_ysp_wr(c, 34, 4548, (Y(1844) & 0xff) | ((Y(1848) & 1) << 16));
	t33_ysp_wr(c, 41, 4640, (Y(2136) & 1) | ((Y(2140) & 1) << 4) | ((Y(2144) & 1) << 8) |
		   ((Y(2148) & 1) << 12));
	t33_ysp_wr(c, 43, 4648, (Y(2160) & 7) | ((Y(2164) & 1) << 4) | ((Y(2168) & 0xf) << 8));
	tisp_mdns_decode_reg_write(c, yspInfo[c] + 1068 / 4);
	t33_ysp_wr(c, 1, 4112, 1);
	tispYspRefresh = 1;
}

#undef Y

#endif /* TX_ISP_T33_I9M_H */
