/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 ysp block, frame-done handler (vendor tisp_ysp_t33_frame_done_interrupt).
 *
 * Refresh state 1: trigger the module list; state 2: when the module DMA is idle
 * reprogram the registers from the per-channel yspInfo struct (two passes, the
 * second rewrites the single-field registers), run the mdns decode write and arm
 * state 1.  Hardware access goes through the ops table (register write, rcfg
 * trigger / DMA state, mdns decode).
 */
#ifndef TX_ISP_T33_FD_YSP_H
#define TX_ISP_T33_FD_YSP_H

#include "tx_isp_t33_blk.h"

#define T33_YSP_MODULE	14

struct t33_ysp_ops {
	void (*reg_write)(u32 ch, u32 module, u32 reg, u32 val);
	void (*mdns_decode)(u32 ch, const u8 *par);
	void (*cfg_trig)(u32 ch, u32 module);
	u32 (*dma_state)(u32 module);
};

struct t33_ysp_ctx {
	const struct t33_ysp_ops *ops;
	const u8 *info;
	u32 ch;
	u32 mode;	/* yspCfgMode: 0 = absolute register, else slot index */
};

static inline u32 t33_ysp_w(const u8 *i, u32 o)
{
	return t33_le32(i + o);
}

#define YW(o)	t33_ysp_w(i, (o))
#define YB(o)	((u32)i[(o)])

static inline void t33_ysp_wr(const struct t33_ysp_ctx *c, u32 idx, u32 off, u32 val)
{
	u32 reg = c->mode ? idx : 0x10000 + off;

	c->ops->reg_write(c->ch, T33_YSP_MODULE, reg, val);
}

/* four 6-bit fields: low, <<8, <<16, <<24 */
static inline u32 t33_ysp_p6(const u8 *i, u32 a, u32 b, u32 c, u32 d)
{
	return (YW(a) & 0x3f) | (YW(b) & 0x3f) << 8 | (YW(c) & 0x3f) << 16 | (YW(d) & 0x3f) << 24;
}

/* byte, 16-bit (<<8), byte <<16, word <<24 */
static inline u32 t33_ysp_q4(const u8 *i, u32 a, u32 b, u32 c, u32 d)
{
	return YB(a) | ((YW(b) << 8) & 0xffff) | YB(c) << 16 | YW(d) << 24;
}

static inline u32 t33_ysp_r3(const u8 *i, u32 a, u32 b, u32 c)
{
	return YB(a) | ((YW(b) << 8) & 0xffff) | YB(c) << 16;
}

static inline u32 t33_ysp_pair(const u8 *i, u32 a, u32 b, u32 mask)
{
	return (YW(a) & mask) | (YW(b) & mask) << 16;
}

static inline u32 t33_ysp_v_a6(const u8 *i)
{
	return YB(1076) | (YW(1080) & 1) << 16 | (YW(1084) & 0x1ff) << 20;
}

static inline u32 t33_ysp_v_a10(const u8 *i)
{
	return (YW(1100) & 0x7f) | (YW(1108) & 0x7f) << 8 | (YW(1112) & 3) << 16;
}

static inline u32 t33_ysp_v_a18(const u8 *i)
{
	return YB(1196) | ((YW(1200) << 8) & 0xffff) | (YW(1204) & 3) << 24;
}

static inline u32 t33_ysp_v_a43(const u8 *i)
{
	return (YW(2160) & 7) | (YW(2164) & 1) << 4 | (YW(2168) & 0xf) << 8;
}

static inline void t33_ysp_pass1(const struct t33_ysp_ctx *c)
{
	const u8 *i = c->info;

	t33_ysp_wr(c, 6, 4172, t33_ysp_v_a6(i));
	t33_ysp_wr(c, 9, 4188, YB(1096) | ((YW(1104) << 8) & 0xffff));
	t33_ysp_wr(c, 10, 4192, t33_ysp_v_a10(i));
	t33_ysp_wr(c, 11, 4196, (YW(1116) & 0x1ff) | YB(1120) << 16 | (YW(1124) & 0x1f) << 24);
	t33_ysp_wr(c, 13, 4204, (YW(1136) & 0x1ff) | YB(1140) << 16 | (YW(1144) & 0x1f) << 24);
	t33_ysp_wr(c, 15, 4212, (YW(1156) & 0x1ff) | YB(1160) << 16 | (YW(1164) & 0x1f) << 24);
	t33_ysp_wr(c, 18, 4224, t33_ysp_v_a18(i));
	t33_ysp_wr(c, 21, 4236, (YW(1240) & 0x7f) | (YW(1244) & 0x7f) << 8 | (YW(1248) & 0x7f) << 16 |
				(YW(1252) & 0x7f) << 24);
	t33_ysp_wr(c, 22, 4240, YB(1256) | (YW(1296) & 0x3fff) << 16);
	t33_ysp_wr(c, 23, 4244, YB(1288) | (YW(1328) & 0x3fff) << 16);
	t33_ysp_wr(c, 24, 4248, YW(1292) & 0x7f);
	t33_ysp_wr(c, 45, 4252, t33_ysp_p6(i, 1332, 1336, 1340, 1344));
	t33_ysp_wr(c, 46, 4252, t33_ysp_p6(i, 1348, 1352, 1356, 1360));
	t33_ysp_wr(c, 47, 4252, t33_ysp_p6(i, 1364, 1368, 1372, 1376));
	t33_ysp_wr(c, 48, 4252, t33_ysp_p6(i, 1392, 1388, 1384, 1380));
	t33_ysp_wr(c, 49, 4252, t33_ysp_p6(i, 1408, 1404, 1400, 1396));
	t33_ysp_wr(c, 50, 4252, t33_ysp_p6(i, 1424, 1420, 1416, 1412));
	t33_ysp_wr(c, 51, 4252, t33_ysp_p6(i, 1440, 1436, 1432, 1428));
	t33_ysp_wr(c, 52, 4252, t33_ysp_p6(i, 1456, 1452, 1448, 1444));
	t33_ysp_wr(c, 54, 4316, t33_ysp_p6(i, 1460, 1464, 1468, 1472));
	t33_ysp_wr(c, 55, 4316, t33_ysp_p6(i, 1476, 1480, 1484, 1488));
	t33_ysp_wr(c, 56, 4316, t33_ysp_p6(i, 1492, 1496, 1500, 1504));
	t33_ysp_wr(c, 57, 4316, t33_ysp_p6(i, 1520, 1516, 1512, 1508));
	t33_ysp_wr(c, 58, 4316, t33_ysp_p6(i, 1536, 1532, 1528, 1524));
	t33_ysp_wr(c, 59, 4316, t33_ysp_p6(i, 1552, 1548, 1544, 1540));
	t33_ysp_wr(c, 60, 4316, t33_ysp_p6(i, 1568, 1564, 1560, 1556));
	t33_ysp_wr(c, 61, 4316, t33_ysp_p6(i, 1584, 1580, 1576, 1572));
	t33_ysp_wr(c, 63, 4380, t33_ysp_q4(i, 1588, 1592, 1596, 1600));
	t33_ysp_wr(c, 64, 4380, t33_ysp_q4(i, 1604, 1608, 1612, 1616));
	t33_ysp_wr(c, 65, 4380, t33_ysp_q4(i, 1620, 1624, 1628, 1632));
	t33_ysp_wr(c, 66, 4380, t33_ysp_q4(i, 1636, 1640, 1644, 1648));
	t33_ysp_wr(c, 68, 4412, t33_ysp_q4(i, 1652, 1656, 1660, 1664));
	t33_ysp_wr(c, 69, 4412, t33_ysp_q4(i, 1668, 1672, 1676, 1680));
	t33_ysp_wr(c, 70, 4412, t33_ysp_q4(i, 1684, 1688, 1692, 1696));
	t33_ysp_wr(c, 71, 4412, t33_ysp_q4(i, 1700, 1704, 1708, 1712));
	t33_ysp_wr(c, 73, 4444, t33_ysp_p6(i, 1716, 1720, 1724, 1728));
	t33_ysp_wr(c, 74, 4444, t33_ysp_p6(i, 1736, 1744, 1732, 1740));
	t33_ysp_wr(c, 75, 4444, t33_ysp_p6(i, 1752, 1760, 1748, 1756));
	t33_ysp_wr(c, 76, 4444, t33_ysp_p6(i, 1768, 1776, 1764, 1772));
	t33_ysp_wr(c, 26, 4476, YB(1784) | YB(1788) << 16);
	t33_ysp_wr(c, 27, 4480, (YW(1792) & 7) | (YW(1796) & 0x1ff) << 8);
	t33_ysp_wr(c, 78, 4484, t33_ysp_pair(i, 1800, 1804, 0x1ff));
	t33_ysp_wr(c, 79, 4484, t33_ysp_pair(i, 1808, 1812, 0x1ff));
	t33_ysp_wr(c, 80, 4484, t33_ysp_pair(i, 1816, 1820, 0x1ff));
	t33_ysp_wr(c, 81, 4484, t33_ysp_pair(i, 1824, 1828, 0x1ff));
	t33_ysp_wr(c, 29, 4500, YB(1832));
	t33_ysp_wr(c, 83, 4504, t33_ysp_q4(i, 1260, 1264, 1268, 1272));
	t33_ysp_wr(c, 84, 4504, t33_ysp_r3(i, 1276, 1280, 1284));
	t33_ysp_wr(c, 86, 4520, t33_ysp_pair(i, 1300, 1304, 0x3fff));
	t33_ysp_wr(c, 87, 4520, t33_ysp_pair(i, 1308, 1312, 0x3fff));
	t33_ysp_wr(c, 88, 4520, t33_ysp_pair(i, 1316, 1320, 0x3fff));
	t33_ysp_wr(c, 31, 4532, YW(1324) & 0x3fff);
	t33_ysp_wr(c, 33, 4544, YB(1836) | ((YW(1840) << 8) & 0xffff));
	t33_ysp_wr(c, 35, 4552, YB(1852) | YB(1856) << 16);
	t33_ysp_wr(c, 36, 4556, t33_ysp_pair(i, 1860, 1864, 0x7f));
	t33_ysp_wr(c, 37, 4560, t33_ysp_r3(i, 1868, 1872, 1876));
	t33_ysp_wr(c, 90, 4564, t33_ysp_q4(i, 1880, 1884, 1888, 1892));
	t33_ysp_wr(c, 91, 4564, t33_ysp_q4(i, 1896, 1900, 1904, 1908));
	t33_ysp_wr(c, 93, 4572, t33_ysp_q4(i, 1912, 1916, 1920, 1924));
	t33_ysp_wr(c, 94, 4572, t33_ysp_q4(i, 1928, 1932, 1936, 1940));
	t33_ysp_wr(c, 96, 4580, t33_ysp_q4(i, 1944, 1948, 1952, 1956));
	t33_ysp_wr(c, 97, 4580, t33_ysp_r3(i, 1960, 1964, 1968));
	t33_ysp_wr(c, 99, 4588, t33_ysp_r3(i, 1972, 1976, 1980));
	t33_ysp_wr(c, 100, 4588, t33_ysp_r3(i, 1984, 1988, 1992));
	t33_ysp_wr(c, 101, 4588, t33_ysp_r3(i, 1996, 2000, 2004));
	t33_ysp_wr(c, 102, 4588, t33_ysp_r3(i, 2008, 2012, 2016));
	t33_ysp_wr(c, 103, 4588, t33_ysp_r3(i, 2020, 2024, 2028));
	t33_ysp_wr(c, 105, 4608, t33_ysp_q4(i, 2032, 2036, 2040, 2044));
	t33_ysp_wr(c, 106, 4608, t33_ysp_r3(i, 2048, 2052, 2056));
	t33_ysp_wr(c, 108, 4616, t33_ysp_q4(i, 2060, 2064, 2068, 2072));
	t33_ysp_wr(c, 109, 4616, t33_ysp_r3(i, 2076, 2080, 2084));
	t33_ysp_wr(c, 111, 4624, t33_ysp_q4(i, 2088, 2092, 2096, 2100));
	t33_ysp_wr(c, 112, 4624, t33_ysp_r3(i, 2104, 2108, 2112));
	t33_ysp_wr(c, 39, 4632, (YW(2116) & 1) | (YW(2120) & 1) << 1 | (YW(2124) & 1) << 2 |
				(YW(2128) & 1) << 3 | (YW(2132) & 1) << 4);
	t33_ysp_wr(c, 42, 4644, YB(2156) << 4 | (YW(2152) & 7));
	t33_ysp_wr(c, 43, 4648, t33_ysp_v_a43(i));
}

static inline void t33_ysp_pass2(const struct t33_ysp_ctx *c)
{
	const u8 *i = c->info;

	t33_ysp_wr(c, 3, 4160, YW(1780) & 0x1f);
	t33_ysp_wr(c, 5, 4168, (YW(1068) & 3) << 16 | (YW(1072) & 3) << 20);
	t33_ysp_wr(c, 6, 4172, t33_ysp_v_a6(i));
	t33_ysp_wr(c, 8, 4184, ((YW(1088) << 4) & 0xff) | ((YW(1092) << 8) & 0xffff));
	t33_ysp_wr(c, 10, 4192, t33_ysp_v_a10(i));
	t33_ysp_wr(c, 12, 4200, YB(1128) | YB(1132) << 16);
	t33_ysp_wr(c, 14, 4208, YB(1148) | YB(1152) << 16);
	t33_ysp_wr(c, 16, 4216, YB(1168) | ((YW(1172) << 8) & 0xffff) | (YW(1176) & 0x1f) << 16);
	t33_ysp_wr(c, 17, 4220, (YW(1180) & 1) | (YW(1184) & 3) << 4 | (YW(1188) & 3) << 8 |
				(YW(1192) & 3) << 12);
	t33_ysp_wr(c, 18, 4224, t33_ysp_v_a18(i));
	t33_ysp_wr(c, 19, 4228, (YW(1208) & 0x1f) | (YW(1212) & 7) << 8 | (YW(1216) & 7) << 12);
	t33_ysp_wr(c, 20, 4232, (YW(1220) & 7) | (YW(1224) & 7) << 4 | (YW(1228) & 7) << 8 |
				(YW(1232) & 7) << 12 | (YW(1236) & 0xf) << 16);
	t33_ysp_wr(c, 34, 4548, YB(1844) | (YW(1848) & 1) << 16);
	t33_ysp_wr(c, 41, 4640, (YW(2136) & 1) | (YW(2140) & 1) << 4 | (YW(2144) & 1) << 8 |
				(YW(2148) & 1) << 12);
	t33_ysp_wr(c, 43, 4648, t33_ysp_v_a43(i));
}

/* tisp_ysp_t33_frame_done_interrupt: info[] = yspInfo (per-channel struct pointers) */
static inline u32 t33_ysp_frame_done(const struct t33_ysp_ops *ops, u32 ch_arg, u32 init_flag, u8 *refresh,
				     u8 cfg_mode, u8 *const *info)
{
	struct t33_ysp_ctx c;
	u32 ch = ch_arg & 0xff;

	if (init_flag != 1)
		return 1;
	if (*refresh == 1) {
		ops->cfg_trig(ch, T33_YSP_MODULE);
		*refresh = 0;
		return 1;
	}
	if (*refresh != 2)
		return 1;
	if (ops->dma_state(T33_YSP_MODULE))
		return 1;
	c.ops = ops;
	c.info = info[ch];
	c.ch = ch;
	c.mode = cfg_mode;
	t33_ysp_pass1(&c);
	t33_ysp_pass2(&c);
	ops->mdns_decode(ch, c.info + 1068);
	t33_ysp_wr(&c, 1, 4112, 1);
	*refresh = 1;
	return 1;
}

#undef YW
#undef YB

#endif /* TX_ISP_T33_FD_YSP_H */
