/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T40 SDNS (spatial denoise, TOP bit 18) in plain C.
 *
 * Rebuilt from the flow of the OEM tisp_sdns_* functions (sc830ai
 * tx-isp-t40.ko): the tuning bank holds a 2900-byte SDNS section at +0x17b00;
 * 125 fields (most of them 11-knot gain tables) are selected from its linear
 * or WDR half, 123 of them are interpolated at the denoise EV (16.16 log2
 * gain), the static fields go to the "noref" registers and the interpolated
 * ones to the "ref" registers of the unit at 0x14000, 0x14054 latches.
 *
 * This replaces the literal mips2c chain (tx_isp_t40_sdns_lit.inc up to
 * open-tx-isp next 6d63667b); tests/t40/c_chains_host compares both.  Only
 * the main channel exists here.  The driver glue keeps the lit behaviour:
 * the init (also after a bank switch or SetModule_Ratio) restarts the gain
 * follow and repacks everything at EV 1.0; the frame-done DNS step calls the
 * gain refresh with a 1/256 threshold.
 */
#ifndef TX_ISP_T40_SDNS_H
#define TX_ISP_T40_SDNS_H

#include "tx_isp_t40_intp.h"

#define T40_SDNS_PAR_OFFSET 0x17b00U
#define T40_SDNS_PAR_BYTES  2900U
#define T40_SDNS_FIELDS     125U
#define T40_SDNS_CUR_BYTES  126U
#define T40_SDNS_BASE       0x14000U

/* OEM tisp_sdns_wdr_en: field offsets in the section, linear and WDR half */
static const uint16_t t40_sdns_field_lin[T40_SDNS_FIELDS] = {
	3, 14, 48, 70, 81, 93, 104, 115, 126, 137, 148, 159,
	170, 182, 245, 256, 267, 278, 289, 300, 311, 322, 333, 344,
	355, 366, 377, 388, 399, 410, 421, 432, 443, 454, 465, 476,
	487, 498, 509, 520, 531, 542, 553, 564, 575, 586, 597, 608,
	619, 630, 641, 652, 663, 674, 685, 696, 707, 718, 731, 742,
	753, 764, 775, 786, 797, 808, 819, 830, 841, 852, 863, 874,
	885, 896, 907, 918, 929, 937, 979, 990, 1001, 1012, 1023, 1034,
	1045, 1056, 1067, 1068, 1070, 1081, 1092, 1103, 1114, 1125, 1136, 1152,
	1163, 1174, 1185, 1196, 1207, 1218, 1229, 1240, 1251, 1262, 1273, 1284,
	1295, 1306, 1317, 1328, 1339, 1350, 1361, 1372, 1383, 1394, 1405, 1416,
	1427, 1438, 1449, 1460, 1471,
};
static const uint16_t t40_sdns_field_wdr[T40_SDNS_FIELDS] = {
	1503, 1514, 1526, 1548, 1559, 1570, 1581, 1592, 1603, 1614, 1625, 1636,
	1647, 1658, 1680, 1691, 1702, 1713, 1724, 1735, 1746, 1757, 1768, 1779,
	1790, 1801, 1812, 1823, 1834, 1845, 1856, 1867, 1878, 1889, 1900, 1911,
	1922, 1933, 1944, 1955, 1966, 1977, 1988, 1999, 2010, 2021, 2032, 2043,
	2054, 2065, 2076, 2087, 2098, 2109, 2120, 2131, 2142, 2153, 2164, 2175,
	2186, 2197, 2208, 2219, 2230, 2241, 2252, 2263, 2274, 2285, 2296, 2307,
	2318, 2329, 2340, 2351, 2362, 2370, 2381, 2392, 2403, 2414, 2425, 2436,
	2447, 2458, 2469, 2470, 2471, 2482, 2493, 2504, 2515, 2526, 2537, 2548,
	2559, 2570, 2581, 2592, 2603, 2614, 2625, 2636, 2647, 2658, 2669, 2680,
	2691, 2702, 2713, 2724, 2735, 2746, 2757, 2768, 2779, 2790, 2801, 2812,
	2823, 2834, 2845, 2856, 2867,
};

/* OEM tisp_sdns_intp: field -> value byte in cur[].  Runs of 8-bit tables,
 * two 16-bit tables (fields 2 and 13) and two 8-bit tables the OEM reads at
 * fixed linear offsets (25, 36) in both modes.  Fields 76, 86, 87 and 124 are
 * not gain dependent and are used as they are. */
struct t40_sdns_run { uint8_t field, dst, count; };
static const struct t40_sdns_run t40_sdns_u8_runs[] = {
	{ 0, 0, 2 }, { 3, 6, 10 }, { 14, 18, 62 }, { 77, 80, 9 }, { 88, 89, 36 },
};

static bool regtrace_enable_sdns;
module_param_named(enable_sdns, regtrace_enable_sdns, bool, 0644);
static int regtrace_sdns_dbg_init_ret = -1;

/* The three OEM buffers (section copy, field table, interpolated values) are
 * the OEM globals main_sdns / main_sdns_comb / main_sdns_intp, allocated as
 * the literal glue did: the recovered tisp_sdns_* helpers (SetModule_Ratio
 * scaling through the field table, WDR switch, tuning get/set) keep working
 * on them.  The C code keeps its own field pointers. */
static struct t40_sdns_state {
	uint8_t *par;		/* main_sdns, T40_SDNS_PAR_BYTES */
	uint8_t *cur;		/* main_sdns_intp, T40_SDNS_CUR_BYTES */
	const uint8_t *f[T40_SDNS_FIELDS];
	uint32_t wdr;		/* OEM sdns_wdr_en[0]; never set on this driver */
	uint32_t ev_old;	/* 0xffffffff: nothing packed yet */
	bool ready;
} t40_sdns T40C2;	/* outside .bss/.data (see T40-LAYOUT in tx_isp_t40_recovered.c) */

/* the OEM buffers are still the ones this state was set up with */
static inline bool t40_sdns_live(const struct t40_sdns_state *s)
{
	return s->ready && s->par && s->par == (uint8_t *)main_sdns && s->cur == (uint8_t *)main_sdns_intp;
}

static inline uint32_t t40_sdns_le16(const uint8_t *p)
{
	return p[0] | ((uint32_t)p[1] << 8);
}

static inline uint32_t t40_sdns_le32(const uint8_t *p)
{
	return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* tisp_sdns_wdr_en */
static void t40_sdns_select(struct t40_sdns_state *s, uint32_t wdr)
{
	const uint16_t *off = wdr ? t40_sdns_field_wdr : t40_sdns_field_lin;
	unsigned int i;

	s->wdr = wdr;
	for (i = 0; i < T40_SDNS_FIELDS; i++) {
		s->f[i] = s->par + off[i];
		((uint32_t *)main_sdns_comb)[i] = (uint32_t)(uintptr_t)s->f[i];
	}
}

/* tisp_sdns_intp */
static void t40_sdns_interpolate(struct t40_sdns_state *s, uint32_t ev)
{
	unsigned int r, i;
	uint32_t v;

	for (r = 0; r < sizeof(t40_sdns_u8_runs) / sizeof(t40_sdns_u8_runs[0]); r++)
		for (i = 0; i < t40_sdns_u8_runs[r].count; i++)
			s->cur[t40_sdns_u8_runs[r].dst + i] =
				t40_intp_u8_at(ev, s->f[t40_sdns_u8_runs[r].field + i]);
	s->cur[2] = t40_intp_u8_at(ev, s->par + 25);
	s->cur[3] = t40_intp_u8_at(ev, s->par + 36);
	v = t40_intp_u16_at(ev, s->f[2]);
	s->cur[4] = v;
	s->cur[5] = v >> 8;
	v = t40_intp_u16_at(ev, s->f[13]);
	s->cur[16] = v;
	s->cur[17] = v >> 8;
}

#define SDNS_W(off, val) system_reg_write(T40_SDNS_BASE + (off), (val))

/* two 8-bit thresholds at bits 2 and 18 */
static inline uint32_t t40_sdns_pair(uint32_t lo, uint32_t hi)
{
	return (lo << 2) | (hi << 18);
}

/* tisp_sdns_noref_reg_cfg: fields that do not follow the gain */
static void t40_sdns_write_static(const struct t40_sdns_state *s)
{
	const uint8_t *p = s->par;
	unsigned int i;

	SDNS_W(0x000, p[0] | ((uint32_t)p[1] << 16));
	SDNS_W(0x020, p[0x3b4] | ((uint32_t)p[0x5c] << 8));
	/* two blocks of eight threshold pairs, the eighth one single */
	for (i = 0; i < 8; i++) {
		SDNS_W(0x100 + 4 * i, i < 7 ? t40_sdns_pair(p[0xcc + 2 * i], p[0xcd + 2 * i]) : p[0xda] << 2);
	}
	for (i = 0; i < 8; i++) {
		SDNS_W(0x120 + 4 * i, i < 7 ? t40_sdns_pair(p[0xdb + 2 * i], p[0xdc + 2 * i]) : p[0xe9] << 2);
	}
	SDNS_W(0x160, t40_sdns_le32(p + 0xea));
	SDNS_W(0x164, t40_sdns_le32(p + 0xee));
	SDNS_W(0x168, p[0xf2] | ((uint32_t)p[0xf3] << 8) | ((uint32_t)p[0xf4] << 16));
	SDNS_W(0x1ac, t40_sdns_le32(s->f[76]));
	SDNS_W(0x1b0, t40_sdns_le32(s->f[76] + 4));
	SDNS_W(0x184, t40_sdns_pair(p[0x3c4], p[0x3c5]));
	SDNS_W(0x188, t40_sdns_pair(p[0x3c6], p[0x3c7]));
	SDNS_W(0x18c, t40_sdns_pair(p[0x3c8], p[0x3c9]));
	SDNS_W(0x190, p[0x3ca] << 2);
	SDNS_W(0x194, t40_sdns_le32(p + 0x3cb));
	SDNS_W(0x198, t40_sdns_le32(p + 0x3cf));
	SDNS_W(0x08c, t40_sdns_le32(p + 0x3bc));
	SDNS_W(0x090, t40_sdns_le32(p + 0x3c0));
	SDNS_W(0x034, p[0x3b5] | ((uint32_t)p[0x3b6] << 16));
	SDNS_W(0x098, t40_sdns_le16(p + 0x47e));
	for (i = 0; i < 8; i++)
		SDNS_W(0x1d4 + 4 * i, t40_sdns_le32(s->f[124] + 4 * i));
}

/* tisp_sdns_ref_reg_cfg: gain-dependent fields from cur[] (some OEM pairs
 * repeat one value) */
static void t40_sdns_write_gain(const struct t40_sdns_state *s)
{
	const uint8_t *c = s->cur, *p = s->par;
	unsigned int i;

	SDNS_W(0x004, (c[0] << 2) | ((uint32_t)c[1] << 20));
	SDNS_W(0x00c, (c[2] << 1) | ((uint32_t)c[3] << 18));
	SDNS_W(0x010, t40_sdns_le32(c + 4));
	SDNS_W(0x19c, t40_sdns_le32(c + 8));
	SDNS_W(0x1a0, t40_sdns_le32(c + 12));
	SDNS_W(0x014, t40_sdns_le16(c + 16));
	for (i = 0; i < 8; i++)
		SDNS_W(0x140 + 4 * i, t40_sdns_le32(c + 18 + 4 * i));
	SDNS_W(0x16c, t40_sdns_le32(c + 50));
	SDNS_W(0x170, t40_sdns_le32(c + 54));
	SDNS_W(0x174, t40_sdns_le32(c + 58));
	SDNS_W(0x01c, c[0x3e] | ((uint32_t)c[0x40] << 8));
	SDNS_W(0x018, c[0x4f] | ((uint32_t)c[0x3f] << 8) | ((uint32_t)p[0x2da] << 16));
	SDNS_W(0x178, t40_sdns_pair(c[0x41], c[0x42]));
	SDNS_W(0x17c, t40_sdns_pair(c[0x43], c[0x44]));
	SDNS_W(0x180, t40_sdns_pair(c[0x45], c[0x46]));
	SDNS_W(0x1a4, t40_sdns_le32(c + 0x47));
	SDNS_W(0x1a8, t40_sdns_le32(c + 0x4b));
	SDNS_W(0x094, c[0x50] | ((uint32_t)p[0x47b] << 8) | ((uint32_t)p[0x47c] << 16) |
		      ((uint32_t)p[0x47d] << 24) | ((uint32_t)c[0x60] << 28));
	SDNS_W(0x030, ((uint32_t)p[0x3b9] << 4) | ((uint32_t)c[0x51] << 18));
	SDNS_W(0x038, t40_sdns_pair(c[0x52], c[0x53]));
	SDNS_W(0x024, p[0x3b7] | ((uint32_t)c[0x54] << 18));
	SDNS_W(0x028, t40_sdns_pair(c[0x55], c[0x56]));
	SDNS_W(0x02c, p[0x3b8] | ((uint32_t)c[0x57] << 8) | ((uint32_t)c[0x58] << 18));
	SDNS_W(0x03c, s->f[87][0] | ((uint32_t)c[0x59] << 8) | ((uint32_t)c[0x5a] << 16));
	SDNS_W(0x040, t40_sdns_le32(c + 0x5b));
	SDNS_W(0x088, p[0x42d] | ((uint32_t)c[0x5f] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[0x2d9] << 24));
	SDNS_W(0x09c, t40_sdns_le32(c + 0x61));
	SDNS_W(0x1b4, t40_sdns_pair(c[0x65], c[0x66]));
	SDNS_W(0x1b8, t40_sdns_pair(c[0x67], c[0x68]));
	SDNS_W(0x1bc, t40_sdns_pair(c[0x69], c[0x69]));
	SDNS_W(0x1c0, t40_sdns_pair(c[0x6a], c[0x6a]));
	SDNS_W(0x1c4, t40_sdns_pair(c[0x6b], c[0x6c]));
	SDNS_W(0x1c8, t40_sdns_pair(c[0x6d], c[0x6e]));
	SDNS_W(0x1cc, t40_sdns_pair(c[0x6f], c[0x6f]));
	SDNS_W(0x1d0, t40_sdns_pair(c[0x70], c[0x70]));
	SDNS_W(0x1f4, t40_sdns_le32(c + 0x71));
	SDNS_W(0x1f8, c[0x75] | ((uint32_t)c[0x75] << 8) | ((uint32_t)c[0x76] << 16) | ((uint32_t)c[0x76] << 24));
	SDNS_W(0x1fc, t40_sdns_le32(c + 0x77));
	SDNS_W(0x200, c[0x7b] | ((uint32_t)c[0x7b] << 8) | ((uint32_t)c[0x7c] << 16) | ((uint32_t)c[0x7c] << 24));
}
#undef SDNS_W

/* tisp_sdns_all_reg_refresh / tisp_sdns_intp_reg_refresh */
static void t40_sdns_pack(struct t40_sdns_state *s, uint32_t ev, bool with_static)
{
	t40_sdns_interpolate(s, ev);
	if (with_static)
		t40_sdns_write_static(s);
	t40_sdns_write_gain(s);
	system_reg_write(T40_SDNS_BASE + 0x54, 1);
}

/* tisp_sdns_par_refresh: repack when the EV moved by at least thres */
static void t40_sdns_refresh(uint32_t ev, uint32_t thres)
{
	struct t40_sdns_state *s = &t40_sdns;
	uint32_t old;

	if (!t40_sdns_live(s))
		return;
	old = s->ev_old;
	if (old == 0xffffffffU) {
		s->ev_old = ev;
		t40_sdns_pack(s, ev, true);
		return;
	}
	if ((ev < old ? old - ev : ev - old) < thres)
		return;
	s->ev_old = ev;
	t40_sdns_pack(s, ev, false);
}

/* OEM tisp_sdns_main_init (as the lit glue: also on a bank switch and after
 * SetModule_Ratio): copy the section, select the half, pack at EV 1.0 */
static int t40_sdns_init(void)
{
	struct t40_sdns_state *s = &t40_sdns;
	const uint8_t *bank = (const uint8_t *)(uintptr_t)tparamsN[0];

	if (!main_sdns || !main_sdns_comb || !main_sdns_intp) {
		/* page-granular vmalloc like the OEM-era glue (tracked, freed at module exit):
		 * the recovered WDR/tuning helpers write the field table with their own
		 * (scaled) strides and must stay inside the page */
		uintptr_t (*valloc)(uintptr_t) = (uintptr_t (*)(uintptr_t))(uintptr_t)private_vmalloc;
		void *par = (void *)valloc(T40_SDNS_PAR_BYTES);
		void *comb = (void *)valloc(4 * T40_SDNS_FIELDS);
		void *cur = (void *)valloc(T40_SDNS_CUR_BYTES);

		if (!par || !comb || !cur) {
			s->ready = false;
			return -ENOMEM;
		}
		memset(par, 0, T40_SDNS_PAR_BYTES);
		memset(comb, 0, 4 * T40_SDNS_FIELDS);
		memset(cur, 0, T40_SDNS_CUR_BYTES);
		main_sdns = (uintptr_t)par;
		main_sdns_comb = (uintptr_t)comb;
		main_sdns_intp = (uintptr_t)cur;
	}
	s->par = (uint8_t *)main_sdns;
	s->cur = (uint8_t *)main_sdns_intp;
	s->ev_old = 0xffffffffU;
	s->ready = false;
	if (!bank)
		return -ENOENT;
	memcpy(s->par, bank + T40_SDNS_PAR_OFFSET, T40_SDNS_PAR_BYTES);
	t40_sdns_select(s, s->wdr);
	t40_sdns_pack(s, 0x10000, true);
	s->ready = true;
	return 0;
}

#endif
