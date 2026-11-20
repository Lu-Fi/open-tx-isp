/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) sharpness: tisp_set_sharpness (vendor tx-isp-t33.ko 0x108cf8; callers tx_isp_core_ops_s_ctrl,
 * tisp_api_param_array_set), openimp #707.
 *
 * - tisp_dmsc_sharpness_set (0x66fd8, t33_dmsc_sharpness_set in tx_isp_t33_i9N.h);
 * - the YSP strength tables: 32 tables of 11 gain points at section bytes 0x454 + 11 n of the working section
 *   (yspInfo word 543) are rebuilt from the bin copy ysp_paramsP: level <= 128 scales a point by level / 128,
 *   above 128 it moves the point towards 255 by (level - 128) / 128; ysp_ratio keeps the level;
 * - then tisp_ysp_bin_data_refresh, the gain interpolation at the stored gain (word 544, signed 16.16 split),
 *   tisp_ysp_reg_data_refresh and, with list 14 not busy, all 91 YSP
 *   register stores of the vendor (register address with yspCfgMode 0, list slot otherwise), the MDNS decode
 *   stores, the 0x11010 trigger and refresh flag 1 (busy: flag 2).
 * Before: t32_tx_os_tisp_set_sharpness was a stub on T33 (the level was only stored).
 */
#ifndef TX_ISP_T33_SHARP_H
#define TX_ISP_T33_SHARP_H

#include "tx_isp_t33_i9M.h"

#ifndef T33_SHARP_GLUE
extern u8 *ysp_paramsP[4];
extern u32 ysp_ratio;
extern void tisp_dmsc_sharpness_set(u32 ch, u32 level);
#endif

#define T33_SHARP_TABS		32
#define T33_SHARP_POINTS	11
#define T33_SHARP_BASE		0x454

#define Y(o)	(v[(o) / 4])

/* tisp_set_sharpness */
static inline void t33_set_sharpness(u32 ch, u32 level)
{
	u32 l = level & 0xff, c, *v, gain, i, f;
	int d = (int)l - 128;
	const u8 *o, *sec;
	u8 *w;
	unsigned int k, n;

	tisp_dmsc_sharpness_set(ch, l);
	w = (u8 *)(unsigned long)yspInfo[ch][2172 / 4];
	ysp_ratio = l;
	for (k = 0; k < T33_SHARP_POINTS; k++) {
		o = ysp_paramsP[ch] + k;
		for (n = 0; n < T33_SHARP_TABS; n++) {
			u32 p = T33_SHARP_BASE + n * T33_SHARP_POINTS;

			w[p + k] = l <= 128 ? (u8)((o[p] * l) >> 7) : (u8)(o[p] + (u8)(((int)(255 - o[p]) * d) >> 7));
		}
	}
	c = ch & 0xff;
	tisp_ysp_bin_data_refresh(c);
	v = yspInfo[c];
	sec = (const u8 *)(unsigned long)v[2172 / 4];
	gain = v[2176 / 4];
	i = (u32)((int)gain >> 16);
	f = gain & 0xffff;
	for (k = 0; k < ARRAY_SIZE(t33_ysp_intp); k++)
		v[t33_ysp_intp[k].word] = t33_ysp_intp[k].bits == 8 ?
			tisp_simple_intp_uint8_t(i, f, sec + t33_ysp_intp[k].off) :
			tisp_simple_intp_uint16_t(i, f, sec + t33_ysp_intp[k].off);
	tisp_ysp_reg_data_refresh(c);
	if (tisp_rcfg_get_dma_state(14)) {
		tispYspRefresh = 2;
		return;
	}
	v = yspInfo[c];
	t33_ysp_wr(c, 6, 4172, (Y(1080) & 1) << 16 | (Y(1084) & 0x1ff) << 20 | (Y(1076) & 0xff));
	t33_ysp_wr(c, 9, 4188, (Y(1104) & 0xff) << 8 | (Y(1096) & 0xff));
	t33_ysp_wr(c, 10, 4192, (Y(1108) & 0x7f) << 8 | (Y(1112) & 3) << 16 | (Y(1100) & 0x7f));
	t33_ysp_wr(c, 11, 4196, (Y(1120) & 0xff) << 16 | (Y(1124) & 31) << 24 | (Y(1116) & 0x1ff));
	t33_ysp_wr(c, 13, 4204, (Y(1140) & 0xff) << 16 | (Y(1144) & 31) << 24 | (Y(1136) & 0x1ff));
	t33_ysp_wr(c, 15, 4212, (Y(1160) & 0xff) << 16 | (Y(1164) & 31) << 24 | (Y(1156) & 0x1ff));
	t33_ysp_wr(c, 18, 4224, (Y(1200) & 0xff) << 8 | (Y(1204) & 3) << 24 | (Y(1196) & 0xff));
	t33_ysp_wr(c, 21, 4236, (Y(1244) & 0x7f) << 8 | (Y(1248) & 0x7f) << 16 | (Y(1240) & 0x7f) |
		   (Y(1252) & 0x7f) << 24);
	t33_ysp_wr(c, 22, 4240, (Y(1296) & 0x3fff) << 16 | (Y(1256) & 0xff));
	t33_ysp_wr(c, 23, 4244, (Y(1328) & 0x3fff) << 16 | (Y(1288) & 0xff));
	t33_ysp_wr(c, 24, 4248, (Y(1292) & 0x7f));
	t33_ysp_wr(c, 45, 4252, (Y(1336) & 0x3f) << 8 | (Y(1340) & 0x3f) << 16 | (Y(1332) & 0x3f) |
		   (Y(1344) & 0x3f) << 24);
	t33_ysp_wr(c, 46, 4252, (Y(1352) & 0x3f) << 8 | (Y(1356) & 0x3f) << 16 | (Y(1348) & 0x3f) |
		   (Y(1360) & 0x3f) << 24);
	t33_ysp_wr(c, 47, 4252, (Y(1368) & 0x3f) << 8 | (Y(1372) & 0x3f) << 16 | (Y(1364) & 0x3f) |
		   (Y(1376) & 0x3f) << 24);
	t33_ysp_wr(c, 48, 4252, (Y(1388) & 0x3f) << 8 | (Y(1384) & 0x3f) << 16 | (Y(1392) & 0x3f) |
		   (Y(1380) & 0x3f) << 24);
	t33_ysp_wr(c, 49, 4252, (Y(1404) & 0x3f) << 8 | (Y(1400) & 0x3f) << 16 | (Y(1408) & 0x3f) |
		   (Y(1396) & 0x3f) << 24);
	t33_ysp_wr(c, 50, 4252, (Y(1420) & 0x3f) << 8 | (Y(1416) & 0x3f) << 16 | (Y(1424) & 0x3f) |
		   (Y(1412) & 0x3f) << 24);
	t33_ysp_wr(c, 51, 4252, (Y(1436) & 0x3f) << 8 | (Y(1432) & 0x3f) << 16 | (Y(1440) & 0x3f) |
		   (Y(1428) & 0x3f) << 24);
	t33_ysp_wr(c, 52, 4252, (Y(1452) & 0x3f) << 8 | (Y(1448) & 0x3f) << 16 | (Y(1456) & 0x3f) |
		   (Y(1444) & 0x3f) << 24);
	t33_ysp_wr(c, 54, 4316, (Y(1464) & 0x3f) << 8 | (Y(1468) & 0x3f) << 16 | (Y(1460) & 0x3f) |
		   (Y(1472) & 0x3f) << 24);
	t33_ysp_wr(c, 55, 4316, (Y(1480) & 0x3f) << 8 | (Y(1484) & 0x3f) << 16 | (Y(1476) & 0x3f) |
		   (Y(1488) & 0x3f) << 24);
	t33_ysp_wr(c, 56, 4316, (Y(1496) & 0x3f) << 8 | (Y(1500) & 0x3f) << 16 | (Y(1492) & 0x3f) |
		   (Y(1504) & 0x3f) << 24);
	t33_ysp_wr(c, 57, 4316, (Y(1516) & 0x3f) << 8 | (Y(1512) & 0x3f) << 16 | (Y(1520) & 0x3f) |
		   (Y(1508) & 0x3f) << 24);
	t33_ysp_wr(c, 58, 4316, (Y(1532) & 0x3f) << 8 | (Y(1528) & 0x3f) << 16 | (Y(1536) & 0x3f) |
		   (Y(1524) & 0x3f) << 24);
	t33_ysp_wr(c, 59, 4316, (Y(1548) & 0x3f) << 8 | (Y(1544) & 0x3f) << 16 | (Y(1552) & 0x3f) |
		   (Y(1540) & 0x3f) << 24);
	t33_ysp_wr(c, 60, 4316, (Y(1564) & 0x3f) << 8 | (Y(1560) & 0x3f) << 16 | (Y(1568) & 0x3f) |
		   (Y(1556) & 0x3f) << 24);
	t33_ysp_wr(c, 61, 4316, (Y(1580) & 0x3f) << 8 | (Y(1576) & 0x3f) << 16 | (Y(1584) & 0x3f) |
		   (Y(1572) & 0x3f) << 24);
	t33_ysp_wr(c, 63, 4380, (Y(1588) & 0xff) | Y(1600) << 24 | (Y(1592) & 0xff) << 8 | (Y(1596) & 0xff) << 16);
	t33_ysp_wr(c, 64, 4380, (Y(1604) & 0xff) | Y(1616) << 24 | (Y(1608) & 0xff) << 8 | (Y(1612) & 0xff) << 16);
	t33_ysp_wr(c, 65, 4380, (Y(1620) & 0xff) | Y(1632) << 24 | (Y(1624) & 0xff) << 8 | (Y(1628) & 0xff) << 16);
	t33_ysp_wr(c, 66, 4380, (Y(1636) & 0xff) | Y(1648) << 24 | (Y(1640) & 0xff) << 8 | (Y(1644) & 0xff) << 16);
	t33_ysp_wr(c, 68, 4412, (Y(1652) & 0xff) | Y(1664) << 24 | (Y(1656) & 0xff) << 8 | (Y(1660) & 0xff) << 16);
	t33_ysp_wr(c, 69, 4412, (Y(1668) & 0xff) | Y(1680) << 24 | (Y(1672) & 0xff) << 8 | (Y(1676) & 0xff) << 16);
	t33_ysp_wr(c, 70, 4412, (Y(1684) & 0xff) | Y(1696) << 24 | (Y(1688) & 0xff) << 8 | (Y(1692) & 0xff) << 16);
	t33_ysp_wr(c, 71, 4412, (Y(1700) & 0xff) | Y(1712) << 24 | (Y(1704) & 0xff) << 8 | (Y(1708) & 0xff) << 16);
	t33_ysp_wr(c, 73, 4444, (Y(1720) & 0x3f) << 8 | (Y(1724) & 0x3f) << 16 | (Y(1716) & 0x3f) |
		   (Y(1728) & 0x3f) << 24);
	t33_ysp_wr(c, 74, 4444, (Y(1744) & 0x3f) << 8 | (Y(1732) & 0x3f) << 16 | (Y(1736) & 0x3f) |
		   (Y(1740) & 0x3f) << 24);
	t33_ysp_wr(c, 75, 4444, (Y(1760) & 0x3f) << 8 | (Y(1748) & 0x3f) << 16 | (Y(1752) & 0x3f) |
		   (Y(1756) & 0x3f) << 24);
	t33_ysp_wr(c, 76, 4444, (Y(1776) & 0x3f) << 8 | (Y(1764) & 0x3f) << 16 | (Y(1768) & 0x3f) |
		   (Y(1772) & 0x3f) << 24);
	t33_ysp_wr(c, 26, 4476, (Y(1788) & 0xff) << 16 | (Y(1784) & 0xff));
	t33_ysp_wr(c, 27, 4480, (Y(1796) & 0x1ff) << 8 | (Y(1792) & 7));
	t33_ysp_wr(c, 78, 4484, (Y(1804) & 0x1ff) << 16 | (Y(1800) & 0x1ff));
	t33_ysp_wr(c, 79, 4484, (Y(1812) & 0x1ff) << 16 | (Y(1808) & 0x1ff));
	t33_ysp_wr(c, 80, 4484, (Y(1820) & 0x1ff) << 16 | (Y(1816) & 0x1ff));
	t33_ysp_wr(c, 81, 4484, (Y(1828) & 0x1ff) << 16 | (Y(1824) & 0x1ff));
	t33_ysp_wr(c, 29, 4500, (Y(1832) & 0xff));
	t33_ysp_wr(c, 83, 4504, (Y(1260) & 0xff) | Y(1272) << 24 | (Y(1264) & 0xff) << 8 | (Y(1268) & 0xff) << 16);
	t33_ysp_wr(c, 84, 4504, (Y(1280) & 0xff) << 8 | (Y(1284) & 0xff) << 16 | (Y(1276) & 0xff));
	t33_ysp_wr(c, 86, 4520, (Y(1304) & 0x3fff) << 16 | (Y(1300) & 0x3fff));
	t33_ysp_wr(c, 87, 4520, (Y(1312) & 0x3fff) << 16 | (Y(1308) & 0x3fff));
	t33_ysp_wr(c, 88, 4520, (Y(1320) & 0x3fff) << 16 | (Y(1316) & 0x3fff));
	t33_ysp_wr(c, 31, 4532, (Y(1324) & 0x3fff));
	t33_ysp_wr(c, 33, 4544, (Y(1840) & 0xff) << 8 | (Y(1836) & 0xff));
	t33_ysp_wr(c, 35, 4552, (Y(1856) & 0xff) << 16 | (Y(1852) & 0xff));
	t33_ysp_wr(c, 36, 4556, (Y(1864) & 0x7f) << 16 | (Y(1860) & 0x7f));
	t33_ysp_wr(c, 37, 4560, (Y(1872) & 0xff) << 8 | (Y(1876) & 0xff) << 16 | (Y(1868) & 0xff));
	t33_ysp_wr(c, 90, 4564, (Y(1880) & 0xff) | Y(1892) << 24 | (Y(1884) & 0xff) << 8 | (Y(1888) & 0xff) << 16);
	t33_ysp_wr(c, 91, 4564, (Y(1896) & 0xff) | Y(1908) << 24 | (Y(1900) & 0xff) << 8 | (Y(1904) & 0xff) << 16);
	t33_ysp_wr(c, 93, 4572, (Y(1912) & 0xff) | Y(1924) << 24 | (Y(1916) & 0xff) << 8 | (Y(1920) & 0xff) << 16);
	t33_ysp_wr(c, 94, 4572, (Y(1928) & 0xff) | Y(1940) << 24 | (Y(1932) & 0xff) << 8 | (Y(1936) & 0xff) << 16);
	t33_ysp_wr(c, 96, 4580, (Y(1944) & 0xff) | Y(1956) << 24 | (Y(1948) & 0xff) << 8 | (Y(1952) & 0xff) << 16);
	t33_ysp_wr(c, 97, 4580, (Y(1964) & 0xff) << 8 | (Y(1968) & 0xff) << 16 | (Y(1960) & 0xff));
	t33_ysp_wr(c, 99, 4588, (Y(1976) & 0xff) << 8 | (Y(1980) & 0xff) << 16 | (Y(1972) & 0xff));
	t33_ysp_wr(c, 100, 4588, (Y(1988) & 0xff) << 8 | (Y(1992) & 0xff) << 16 | (Y(1984) & 0xff));
	t33_ysp_wr(c, 101, 4588, (Y(2000) & 0xff) << 8 | (Y(2004) & 0xff) << 16 | (Y(1996) & 0xff));
	t33_ysp_wr(c, 102, 4588, (Y(2012) & 0xff) << 8 | (Y(2016) & 0xff) << 16 | (Y(2008) & 0xff));
	t33_ysp_wr(c, 103, 4588, (Y(2024) & 0xff) << 8 | (Y(2028) & 0xff) << 16 | (Y(2020) & 0xff));
	t33_ysp_wr(c, 105, 4608, (Y(2032) & 0xff) | Y(2044) << 24 | (Y(2036) & 0xff) << 8 | (Y(2040) & 0xff) << 16);
	t33_ysp_wr(c, 106, 4608, (Y(2052) & 0xff) << 8 | (Y(2056) & 0xff) << 16 | (Y(2048) & 0xff));
	t33_ysp_wr(c, 108, 4616, (Y(2060) & 0xff) | Y(2072) << 24 | (Y(2064) & 0xff) << 8 | (Y(2068) & 0xff) << 16);
	t33_ysp_wr(c, 109, 4616, (Y(2080) & 0xff) << 8 | (Y(2084) & 0xff) << 16 | (Y(2076) & 0xff));
	t33_ysp_wr(c, 111, 4624, (Y(2088) & 0xff) | Y(2100) << 24 | (Y(2092) & 0xff) << 8 | (Y(2096) & 0xff) << 16);
	t33_ysp_wr(c, 112, 4624, (Y(2108) & 0xff) << 8 | (Y(2112) & 0xff) << 16 | (Y(2104) & 0xff));
	t33_ysp_wr(c, 39, 4632, (Y(2120) & 1) << 1 | (Y(2124) & 1) << 2 | (Y(2116) & 1) |
		   (Y(2128) & 1) << 3 | (Y(2132) & 1) << 4);
	t33_ysp_wr(c, 42, 4644, (Y(2156) & 0xff) << 4 | (Y(2152) & 7));
	t33_ysp_wr(c, 43, 4648, (Y(2164) & 1) << 4 | (Y(2168) & 15) << 8 | (Y(2160) & 7));
	t33_ysp_wr(c, 3, 4160, (Y(1780) & 31));
	t33_ysp_wr(c, 5, 4168, (Y(1068) & 3) << 16 | (Y(1072) & 3) << 20);
	t33_ysp_wr(c, 6, 4172, (Y(1080) & 1) << 16 | (Y(1084) & 0x1ff) << 20 | (Y(1076) & 0xff));
	t33_ysp_wr(c, 8, 4184, (Y(1088) & 15) << 4 | (Y(1092) & 0xff) << 8);
	t33_ysp_wr(c, 10, 4192, (Y(1108) & 0x7f) << 8 | (Y(1112) & 3) << 16 | (Y(1100) & 0x7f));
	t33_ysp_wr(c, 12, 4200, (Y(1132) & 0xff) << 16 | (Y(1128) & 0xff));
	t33_ysp_wr(c, 14, 4208, (Y(1152) & 0xff) << 16 | (Y(1148) & 0xff));
	t33_ysp_wr(c, 16, 4216, (Y(1172) & 0xff) << 8 | (Y(1176) & 31) << 16 | (Y(1168) & 0xff));
	t33_ysp_wr(c, 17, 4220, (Y(1184) & 3) << 4 | (Y(1188) & 3) << 8 | (Y(1180) & 1) | (Y(1192) & 3) << 12);
	t33_ysp_wr(c, 18, 4224, (Y(1200) & 0xff) << 8 | (Y(1204) & 3) << 24 | (Y(1196) & 0xff));
	t33_ysp_wr(c, 19, 4228, (Y(1212) & 7) << 8 | (Y(1216) & 7) << 12 | (Y(1208) & 31));
	t33_ysp_wr(c, 20, 4232, (Y(1224) & 7) << 4 | (Y(1228) & 7) << 8 | (Y(1220) & 7) |
		   (Y(1232) & 7) << 12 | (Y(1236) & 15) << 16);
	t33_ysp_wr(c, 34, 4548, (Y(1848) & 1) << 16 | (Y(1844) & 0xff));
	t33_ysp_wr(c, 41, 4640, (Y(2140) & 1) << 4 | (Y(2144) & 1) << 8 | (Y(2136) & 1) | (Y(2148) & 1) << 12);
	t33_ysp_wr(c, 43, 4648, (Y(2164) & 1) << 4 | (Y(2168) & 15) << 8 | (Y(2160) & 7));
	tisp_mdns_decode_reg_write(c, yspInfo[c] + 1068 / 4);
	t33_ysp_wr(c, 1, 4112, 1);
	tispYspRefresh = 1;
}

#undef Y

#endif /* TX_ISP_T33_SHARP_H */
