/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 DMSC frame-done handler (vendor tisp_dmsc_t33_frame_done_interrupt).
 *
 * State 1: trigger the module list refresh.  State 2: when the DMA of the
 * module is idle, write the DMSC parameter words (from the per-channel info
 * struct) to the register list: the vendor's long pass, then a short second
 * pass that repeats a subset, then the enable word.  In list mode (cfg mode
 * non-zero) the register argument is the list slot index, else the address.
 */
#ifndef TX_ISP_T33_FD_DMSC_H
#define TX_ISP_T33_FD_DMSC_H

#include "tx_isp_t33_blk.h"

#define T33_DMSC_MOD 9

extern int tisp_reg_write_t33(u32 ch, u32 mod, u32 reg, u32 val);
extern int tisp_rcfg_get_dma_state(u32 mod);
extern int tisp_rcfg_module_cfg_trig(u32 ch, u32 mod);

static inline void t33_dmsc_wr(u32 ch, u32 mode, u32 idx, u32 addr, u32 val)
{
	tisp_reg_write_t33(ch, T33_DMSC_MOD, mode ? idx : addr, val);
}

#define DF(o)		t33_le32(i + (o))
#define DK(o, m, s)	((DF(o) & (m)) << (s))
#define DB(o, s)	((u32)i[o] << (s))
#define DS(o, s)	((DF(o) << (s)) & 0xffff)
#define D5(b)		(DK((b) + 4, 0x1f, 6) | DK((b) + 8, 0x1f, 12) | DK(b, 0x1f, 0) | \
			 DK((b) + 12, 0x1f, 18) | DK((b) + 16, 0x1f, 24))
#define DW(a, v)	t33_dmsc_wr(ch, m, id, a, v)

/* one register word of the info struct i, selected by its list slot id */
static inline void t33_dmsc_put(u32 ch, u32 m, const u8 *i, u32 id)
{
	switch (id) {
	case 1: DW(0x8010, 1); break;
	case 3: DW(0x8040, DK(1320, 0x1f, 0)); break;
	case 4: DW(0x8044, DB(1340, 0) | DK(1344, 1, 31) | DK(1328, 3, 8) | DS(1324, 10)); break;
	case 5: DW(0x8048, DB(1332, 16) | DK(1336, 0xfff, 0)); break;
	case 6: DW(0x804c, DB(1348, 16) | DB(1352, 0)); break;
	case 7: DW(0x8050, DK(1360, 0x3f, 16) | DK(1356, 0x3f, 0)); break;
	case 8: DW(0x8054, DB(1368, 16) | DB(1364, 0)); break;
	case 9: DW(0x8058, DB(1376, 16) | DK(1372, 0x7f, 24) | DK(1380, 0x3ff, 0)); break;
	case 10: DW(0x805c, DB(1388, 16) | DB(1384, 0)); break;
	case 11: DW(0x8060, DB(1396, 16) | DK(1392, 0x7f, 24) | DK(1400, 0x3ff, 0)); break;
	case 12: DW(0x8064, DB(1408, 16) | DB(1404, 0)); break;
	case 13: DW(0x8068, DB(1416, 16) | DK(1412, 0x7f, 24) | DK(1420, 0x3ff, 0)); break;
	case 14: DW(0x806c, DK(1444, 0xf, 6) | DB(1428, 10) | DK(1436, 0x3f, 0) | DK(1424, 0x3ff, 18)); break;
	case 15: DW(0x8070, DK(1440, 0x1fff, 16) | DK(1432, 0x1fff, 0)); break;
	case 16: DW(0x8074, DK(1448, 0x1ff, 16) | DK(1452, 0x1ff, 0)); break;
	case 17: DW(0x8078, DS(1460, 8) | DK(1456, 0x3f, 16) | DB(1464, 0)); break;
	case 18: DW(0x807c, DK(1484, 3, 2) | DK(1480, 0x1f, 7) | DK(1468, 0xf, 17)); break;
	case 19: DW(0x8080, DK(1488, 0x3ff, 16) | DK(1492, 0x3ff, 0)); break;
	case 21: DW(0x8094, DK(1572, 3, 2) | ((DF(1544) << 4) & 0xff) | DK(1548, 0xf, 8) | DS(1552, 12) | DK(1516, 0xf, 16)); break;
	case 22: DW(0x8098, DK(1576, 0xfff, 16) | DK(1580, 0xfff, 0)); break;
	case 24: DW(0x80ac, DK(1620, 0x1ff, 16) | DK(1624, 0xf, 0)); break;
	case 25: DW(0x80b0, DK(1640, 0x1ff, 16) | DK(1632, 0xfff, 0)); break;
	case 26: DW(0x80b4, DK(1636, 0x3f, 5) | DK(1628, 7, 16) | DK(1644, 0x1f, 0)); break;
	case 27: DW(0x80b8, DK(1680, 0x7f, 7) | DK(1660, 0x7f, 14) | DK(1700, 0x7f, 0) | DK(1648, 0x7f, 21)); break;
	case 28: DW(0x80bc, DK(1652, 0x1ff, 16) | DK(1664, 0xfff, 0)); break;
	case 29: DW(0x80c0, DK(1672, 0xfff, 16) | DK(1676, 0xfff, 0)); break;
	case 30: DW(0x80c4, DK(1696, 0x7f, 12) | DK(1684, 0xfff, 19) | DK(1692, 0xfff, 0)); break;
	case 31: DW(0x80c8, DK(1656, 0x3f, 6) | DK(1668, 0x3f, 16) | DK(1688, 0x3f, 0)); break;
	case 32: DW(0x80cc, DK(1708, 0xfff, 16) | DK(1704, 0xfff, 0)); break;
	case 33: DW(0x80d0, DK(1472, 0xfff, 16) | DK(1476, 0x3f, 0)); break;
	case 34: DW(0x80d4, DK(1508, 1, 16) | DK(1512, 0x3ff, 0)); break;
	case 35: DW(0x80d8, DK(1564, 7, 8) | DK(1560, 0x1f, 15) | DK(1568, 7, 0) | DK(1556, 1, 27)); break;
	case 36: DW(0x80dc, DK(1536, 7, 4) | DK(1532, 7, 8) | DK(1540, 0xf, 0) | DK(1528, 7, 16) | DK(1524, 7, 24) | DK(1520, 1, 27)); break;
	case 37: DW(0x80e0, DK(1592, 0x3f, 8) | DK(1584, 0xfff, 16) | DK(1588, 0x3f, 0)); break;
	case 38: DW(0x80e4, DK(1612, 0xfff, 8) | DK(1608, 1, 27) | DK(1616, 3, 0)); break;
	case 40: DW(0x817c, DK(1712, 0xfff, 16) | DK(1716, 0x7f, 0)); break;
	case 42: DW(0x8188, DK(2008, 1, 24)); break;
	case 43: DW(0x818c, DB(1596, 16) | DB(1496, 0)); break;
	case 44: DW(0x8190, DK(1600, 0x3ff, 16) | DK(1500, 0x1ff, 0)); break;
	case 45: DW(0x8194, DK(1604, 0xfff, 16) | DK(1504, 0x3ff, 0)); break;
	case 46: DW(0x8198, DK(2012, 0xfff, 0)); break;
	case 48: DW(0x81c4, DK(2020, 3, 4) | DK(2024, 1, 8) | DK(2016, 1, 0) | DK(2028, 7, 12)); break;
	case 49: DW(0x81c8, DK(2036, 0xfff, 16) | DB(2032, 0)); break;
	case 50: DW(0x81cc, DK(2044, 1, 8) | DK(2040, 0xf, 0)); break;
	case 52: DW(0x81dc, DK(2052, 0x3f, 4) | DK(2048, 3, 0)); break;
	case 54: DW(0x81e4, DK(2056, 0x3f, 0)); break;
	case 55: DW(0x81e8, DK(2064, 0x3f, 16) | DK(2068, 7, 24) | DK(2060, 0xfff, 0)); break;
	case 57: DW(0x81fc, DK(2072, 3, 0)); break;
	case 58: DW(0x8200, DK(2080, 0xfff, 16) | DK(2076, 0xfff, 0)); break;
	case 60: DW(0x8208, DB(2088, 16) | DK(2084, 0x1ff, 0)); break;
	case 61: DW(0x820c, DK(2096, 0xfff, 16) | DK(2092, 0xfff, 0)); break;
	case 62: DW(0x8210, DK(2104, 0xfff, 16) | DK(2100, 0xfff, 0)); break;
	case 64: DW(0x8218, DB(2112, 16) | DK(2108, 0x1ff, 0)); break;
	case 65: DW(0x821c, DK(2120, 1, 8) | DK(2124, 0x3f, 12) | DK(2116, 0x3f, 0) | DK(2128, 1, 20)); break;
	case 67: DW(0x8224, DK(2136, 0xfff, 16) | DK(2132, 0xfff, 0)); break;
	case 68: DW(0x8228, DK(2144, 0x1ff, 16) | DK(2140, 0x1ff, 0)); break;
	case 70: DW(0x8254, DK(2184, 0xfff, 8) | DK(2192, 0x3f, 0)); break;
	case 71: DW(0x8258, DK(2180, 0x3f, 8) | DK(2188, 0x3f, 0)); break;
	case 73: DW(0x827c, DK(2196, 1, 8) | DK(2200, 1, 16) | DK(2204, 3, 24)); break;
	case 75: DW(0x8284, DK(2212, 0xfff, 12) | DK(2216, 0x7f, 24) | DK(2208, 0xfff, 0)); break;
	case 76: DW(0x8288, DK(2220, 0x3f, 0) | (DF(2224) << 20) | DK(2228, 0xfff, 8)); break;
	case 77: DW(0x828c, DK(2236, 0x3f, 8) | DK(2240, 1, 24) | DK(2232, 0x7f, 0)); break;
	case 78: DW(0x8290, DK(2244, 0x7f, 12) | DK(2248, 0x7f, 24)); break;
	case 79: DW(0x8294, DK(2256, 0x7f, 12) | DK(2260, 0x7f, 24) | DK(2252, 0x7f, 0)); break;
	case 80: DW(0x8298, DK(2268, 0xf, 8) | DK(2272, 0xf, 16) | DK(2264, 0x7f, 0) | DK(2276, 0xf, 24)); break;
	case 81: DW(0x829c, DK(2284, 3, 24) | DK(2280, 0xfff, 0)); break;
	case 82: DW(0x82a0, DS(2288, 8) | DB(2292, 16) | (DF(2296) << 24)); break;
	case 83: DW(0x82a4, DS(2304, 8) | DB(2308, 16) | DB(2300, 0)); break;
	case 84: DW(0x82a8, DK(2316, 0x3ff, 10) | DK(2320, 0x3ff, 20) | DK(2312, 0x3ff, 0)); break;
	case 85: DW(0x82ac, DK(2328, 0x3ff, 10) | DK(2332, 0x3ff, 20) | DK(2324, 0x3ff, 0)); break;
	case 86: DW(0x82b0, DK(2340, 0xfff, 12) | DK(2336, 0xfff, 0)); break;
	case 87: DW(0x82b4, DK(2348, 0xfff, 12) | DK(2344, 0xfff, 0)); break;
	case 88: DW(0x82b8, DK(2356, 0xfff, 12) | DK(2352, 0xfff, 0)); break;
	case 89: DW(0x82bc, DS(2364, 10) | DK(2368, 0x3f, 20) | DK(2360, 0x3f, 0)); break;
	case 90: DW(0x82c0, DS(2376, 10) | DK(2380, 0x3f, 20) | DK(2372, 0x3f, 0)); break;
	case 92: DW(0x82d4, DK(2384, 0xf, 16)); break;
	case 93: DW(0x82d8, DK(2452, 0x7f, 8) | DK(2456, 0x7f, 16) | DK(2448, 0x7f, 0) | DK(2460, 7, 24) | DK(2464, 7, 28)); break;
	case 95: DW(0x82e0, DK(2404, 0x3f, 24)); break;
	case 96: DW(0x82e4, DK(2392, 0x1f, 8) | DK(2396, 0x7f, 16) | DK(2388, 0x3f, 0) | DK(2400, 0xf, 24)); break;
	case 97: DW(0x82e8, DK(2412, 0x1fff, 16) | DK(2408, 0xfff, 0)); break;
	case 98: DW(0x82ec, DK(2420, 0xf, 16) | DK(2424, 1, 20) | DK(2416, 0x1fff, 0) | DK(2436, 0x1f, 24)); break;
	case 99: DW(0x82f0, DK(2444, 0xfff, 16) | DK(2440, 0xfff, 0)); break;
	case 100: DW(0x82f4, DK(2432, 0xfff, 16) | DK(2428, 0xfff, 0)); break;
	case 102: DW(0x8304, DK(2468, 0x7f, 0)); break;
	case 104: DW(0x8324, DK(2500, 0xfff, 8) | DK(2492, 1, 20) | DK(2496, 0x1f, 0)); break;
	case 105: DW(0x8328, DK(2512, 0x7f, 8) | DK(2508, 0x7f, 0)); break;
	case 106: DW(0x832c, DK(2524, 0x1ff, 12) | DK(2516, 0xfff, 0)); break;
	case 107: DW(0x8330, DK(2532, 0x1ff, 12) | DK(2528, 0x1ff, 0)); break;
	case 108: DW(0x8334, DK(2540, 0x7f, 8) | DK(2544, 0x7f, 16) | DK(2536, 0x7f, 0) | DK(2548, 0x7f, 24)); break;
	case 109: DW(0x8338, DK(2556, 1, 8) | DK(2560, 3, 12) | DK(2552, 0x7f, 0) | DK(2564, 0xf, 16) | DK(2568, 0xf, 20)); break;
	case 110: DW(0x833c, DS(2576, 4) | DK(2580, 0x7f, 16) | DK(2572, 1, 0)); break;
	case 111: DW(0x8340, DK(2588, 0xfff, 12) | DK(2584, 0xfff, 0)); break;
	case 112: DW(0x8344, DB(2592, 0) | (DF(2604) << 24) | DS(2596, 8) | DB(2600, 16)); break;
	case 113: DW(0x8348, DK(2608, 0xf, 0) | (DF(2620) << 20) | ((DF(2612) << 4) & 0xff) | DK(2616, 0x1ff, 8)); break;
	case 114: DW(0x834c, DS(2628, 12) | DK(2632, 0x1ff, 16) | DK(2624, 0x1ff, 0)); break;
	case 115: DW(0x8350, DS(2644, 12) | DK(2648, 0x1ff, 16) | DK(2640, 0x1ff, 0)); break;
	case 116: DW(0x8354, DK(2652, 0xfff, 12) | DK(2636, 0xfff, 0)); break;
	case 117: DW(0x8358, DS(2660, 12) | DK(2664, 0x1ff, 16) | DK(2656, 0x1ff, 0)); break;
	case 118: DW(0x835c, DS(2676, 12) | DK(2672, 0x1ff, 16) | DK(2668, 0xfff, 0)); break;
	case 119: DW(0x8360, DK(2484, 1, 4) | DK(2480, 1, 8) | DK(2488, 1, 0) | DK(2476, 3, 12) | DK(2472, 7, 16)); break;
	case 121: DW(0x80e8, DK(1724, 0x1f, 16) | DK(1720, 0x1f, 0)); break;
	case 122: DW(0x80e8, DK(1732, 0x1f, 16) | DK(1728, 0x1f, 0)); break;
	case 123: DW(0x80e8, DK(1740, 0x1f, 16) | DK(1736, 0x1f, 0)); break;
	case 124: DW(0x80e8, DK(1748, 0x1f, 16) | DK(1744, 0x1f, 0)); break;
	case 126: DW(0x8108, D5(1752)); break;
	case 127: DW(0x8108, D5(1772)); break;
	case 128: DW(0x8108, D5(1792)); break;
	case 129: DW(0x8108, D5(1812)); break;
	case 130: DW(0x8108, D5(1832)); break;
	case 131: DW(0x8108, D5(1852)); break;
	case 132: DW(0x8108, DK(1876, 0x1f, 6) | DK(1872, 0x1f, 0)); break;
	case 134: DW(0x8124, D5(1880)); break;
	case 135: DW(0x8124, D5(1900)); break;
	case 136: DW(0x8124, D5(1920)); break;
	case 137: DW(0x8124, D5(1940)); break;
	case 138: DW(0x8124, D5(1960)); break;
	case 139: DW(0x8124, D5(1980)); break;
	case 140: DW(0x8124, DK(2004, 0x1f, 6) | DK(2000, 0x1f, 0)); break;
	case 142: DW(0x822c, DK(2152, 0x1f, 8) | DK(2156, 0x1f, 16) | DK(2148, 0x1f, 0) | DK(2160, 0x1f, 24)); break;
	case 143: DW(0x822c, DK(2168, 0x1f, 8) | DK(2164, 0x1f, 16) | DK(2172, 0x1f, 0) | DK(2176, 0x1f, 24)); break;
	default: break;
	}
}

#undef DF
#undef DK
#undef DB
#undef DS
#undef D5
#undef DW

/* tisp_dmsc_t33_frame_done_interrupt */
static inline int t33_dmsc_frame_done(u32 ch, u32 initpar904, u8 *const *infos, const u8 *cfg_mode,
				      u8 *refresh)
{
	static const u8 pass1[] = {
		4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 24, 25, 26, 27, 28,
		29, 30, 32, 33, 34, 37, 38, 121, 122, 123, 124, 126, 127, 128, 129, 130, 131, 132,
		134, 135, 136, 137, 138, 139, 140, 40, 43, 44, 45, 46, 49, 52, 54, 55, 58, 60, 61,
		62, 64, 67, 68, 142, 143, 70, 71, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86,
		87, 88, 89, 90, 92, 93, 95, 96, 97, 98, 99, 100, 102, 104, 105, 106, 107, 108, 109,
		110, 111, 112, 113, 114, 115, 116, 117, 118, 119,
	};
	static const u8 pass2[] = {
		3, 4, 9, 11, 13, 14, 17, 18, 21, 24, 26, 31, 33, 34, 35, 36, 37, 38, 42, 48, 50, 52,
		55, 57, 65, 70, 71, 73, 76, 77, 81, 98, 104, 105, 107, 109, 110, 114, 115, 117, 118,
		119, 1,
	};
	const u8 *info;
	u32 c = ch & 0xff, m;
	unsigned int k;

	if (initpar904 != 1)
		return 1;
	if (*refresh == 1) {
		tisp_rcfg_module_cfg_trig(c, T33_DMSC_MOD);
		*refresh = 0;
	} else if (*refresh == 2) {
		if (tisp_rcfg_get_dma_state(T33_DMSC_MOD)) {
			*refresh = 2;
			return 1;
		}
		info = infos[c];
		m = *cfg_mode;
		for (k = 0; k < sizeof(pass1); k++)
			t33_dmsc_put(c, m, info, pass1[k]);
		for (k = 0; k < sizeof(pass2); k++)
			t33_dmsc_put(c, m, infos[c], pass2[k]);
		*refresh = 1;
	}
	return 1;
}

#endif /* TX_ISP_T33_FD_DMSC_H */
