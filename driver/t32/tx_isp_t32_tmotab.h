/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 TMO: the vendor's initialised tables of tisp_tmo_adr_fpga (and of map_curve1, map_y_filter_sp, tmo_adr_algorithm), openimp #280.
 * The values are the module's .rodata / .data words of Matt Davis' recovered T32 tree (opensensor/open-tx-isp PR #15 @ 2e2e06bc,
 * driver/t32/data.c, symbol offsets from data-aliases.lds; .rodata is at vendor address 0xbd420).  The data is little-endian, so an s16
 * table is the low half of each word first.  tests/t32_oracle/tmotab_check.py compares every value with data.c, and
 * t32_tmo14_oracle runs the port once more with these tables on both sides.
 *
 * Only d88d4 is written at run time (block 4, the y filters): the driver keeps it as module state and starts it from
 * t32_tmo_tab_d88d4.  The other env members (e31b0, e3160, e3518, e3500, e3244, e34e8, e3248, exp2) are .bss state of the TMO module
 * (zero at load, filled by tisp_tmo_init / param_array_set / adr_gauss_local / adr_faceae_refresh) and are not part of these tables.
 */
#ifndef TX_ISP_T32_TMOTAB_H
#define TX_ISP_T32_TMOTAB_H

/* include after tx_isp_t32_tmo.h (struct t32_tmo_fpga_env; its fpga block is outside that header's include guard) */

/* t32_data_c0444 (.rodata +0x3024, data.c:3380..3396): block 4, map_y_filter_sp: the 33 positions 0, 128 .. 3968, 4095 */
static const s16 t32_tmo_tab_c0444[33] = {
	0, 128, 256, 384, 512, 640, 768, 896, 1024, 1152, 1280, 1408, 1536, 1664, 1792, 1920,
	2048, 2176, 2304, 2432, 2560, 2688, 2816, 2944, 3072, 3200, 3328, 3456, 3584, 3712, 3840, 3968,
	4095,
};

/* t32_data_c0488 (.rodata +0x3068, data.c:3397..3403): block 3: the 13 point axis (copied, then interp1_short) */
static const s16 t32_tmo_tab_c0488[13] = {
	0, 32, 64, 128, 256, 512, 1024, 1536, 2048, 2560, 3072, 3584, 4095,
};

/* t32_data_c04a4 (.rodata +0x3084, data.c:3404..3408): block 3: the 9 point axis of ispint_16 (8 .. 2048) */
static const s16 t32_tmo_tab_c04a4[9] = {
	8, 16, 32, 64, 128, 256, 512, 1024, 2048,
};

/* t32_data_d8120 (.data +0x9f00, data.c:27125..27252): block 1: the 128 weights of the cumulative histogram */
static const int t32_tmo_tab_d8120[128] = {
	255, 255, 255, 255, 254, 254, 253, 253, 252, 251, 250, 249,
	248, 247, 245, 244, 242, 241, 239, 237, 235, 233, 231, 229,
	227, 225, 223, 220, 218, 216, 213, 210, 208, 205, 202, 200,
	197, 194, 191, 188, 185, 182, 179, 176, 173, 170, 167, 164,
	161, 158, 155, 152, 148, 145, 142, 139, 136, 133, 130, 127,
	124, 121, 118, 115, 112, 110, 107, 104, 101, 98, 96, 93,
	90, 88, 85, 83, 80, 78, 76, 73, 71, 69, 66, 64,
	62, 60, 58, 56, 54, 52, 50, 49, 47, 45, 44, 42,
	40, 39, 37, 36, 35, 33, 32, 31, 29, 28, 27, 26,
	25, 24, 23, 22, 21, 20, 19, 18, 17, 17, 16, 15,
	14, 14, 13, 12, 12, 11, 11, 10,
};

/* t32_data_d8320 (.data +0xa100, data.c:27253..27253): block 1: the divisor */
#define T32_TMO_TAB_D8320 15934

/* t32_data_d8324 (.data +0xa104, data.c:27254..27509): blocks 3 / 4, map_curve1: the 256 curve slopes */
static const int t32_tmo_tab_d8324[256] = {
	2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24,
	26, 28, 30, 32, 34, 36, 38, 40, 42, 44, 46, 48,
	50, 52, 54, 56, 58, 60, 62, 64, 66, 68, 70, 72,
	74, 76, 78, 80, 82, 84, 86, 88, 90, 92, 94, 96,
	98, 100, 102, 104, 106, 108, 111, 113, 115, 117, 119, 121,
	123, 125, 127, 129, 131, 133, 135, 137, 139, 142, 144, 146,
	148, 150, 152, 155, 157, 159, 161, 163, 166, 168, 170, 173,
	175, 178, 180, 183, 185, 188, 191, 194, 197, 200, 203, 206,
	210, 214, 218, 222, 226, 231, 237, 243, 250, 258, 267, 277,
	289, 304, 321, 343, 370, 404, 448, 507, 588, 702, 867, 1119,
	1523, 2212, 3496, 6184, 12868, 34878, 160568, 4328775, -4328775, -160568, -34878, -12868,
	-6184, -3496, -2212, -1523, -1119, -867, -702, -588, -507, -448, -404, -370,
	-343, -321, -304, -289, -277, -267, -258, -250, -243, -237, -231, -226,
	-222, -218, -214, -210, -206, -203, -200, -197, -194, -191, -188, -185,
	-183, -180, -178, -175, -173, -170, -168, -166, -163, -161, -159, -157,
	-155, -152, -150, -148, -146, -144, -142, -139, -137, -135, -133, -131,
	-129, -127, -125, -123, -121, -119, -117, -115, -113, -111, -108, -106,
	-104, -102, -100, -98, -96, -94, -92, -90, -88, -86, -84, -82,
	-80, -78, -76, -74, -72, -70, -68, -66, -64, -62, -60, -58,
	-56, -54, -52, -50, -48, -46, -44, -42, -40, -38, -36, -34,
	-32, -30, -28, -26, -24, -22, -20, -18, -16, -14, -12, -10,
	-8, -6, -4, -2,
};

/* t32_data_d87a0 (.data +0xa580, data.c:27541..27605): blocks 3 / 4 (interp1_short, subsection_up), also tmo_adr_algorithm: the 129 point axis 0, 32 .. 4064, 4095 */
static const s16 t32_tmo_tab_d87a0[129] = {
	0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 480,
	512, 544, 576, 608, 640, 672, 704, 736, 768, 800, 832, 864, 896, 928, 960, 992,
	1024, 1056, 1088, 1120, 1152, 1184, 1216, 1248, 1280, 1312, 1344, 1376, 1408, 1440, 1472, 1504,
	1536, 1568, 1600, 1632, 1664, 1696, 1728, 1760, 1792, 1824, 1856, 1888, 1920, 1952, 1984, 2016,
	2048, 2080, 2112, 2144, 2176, 2208, 2240, 2272, 2304, 2336, 2368, 2400, 2432, 2464, 2496, 2528,
	2560, 2592, 2624, 2656, 2688, 2720, 2752, 2784, 2816, 2848, 2880, 2912, 2944, 2976, 3008, 3040,
	3072, 3104, 3136, 3168, 3200, 3232, 3264, 3296, 3328, 3360, 3392, 3424, 3456, 3488, 3520, 3552,
	3584, 3616, 3648, 3680, 3712, 3744, 3776, 3808, 3840, 3872, 3904, 3936, 3968, 4000, 4032, 4064,
	4095,
};

/* t32_data_d88a4 (.data +0xa684, data.c:27606..27617): block 3: 23 curve values + 1 pad (d88a4, d88d4, d88f4 are adjacent in .data) */
static const s16 t32_tmo_tab_d88a4[24] = {
	0, 8, 16, 32, 64, 96, 128, 256, 384, 512, 640, 768, 896, 1024, 1280, 1536,
	1792, 2048, 2304, 2560, 3072, 3584, 4096, 0,
};

/* t32_data_d88d4 (.data +0xa6b4, data.c:27618..27625): blocks 3 .. 5: the 16 y filter positions, initial value only (block 4 writes this .data array) */
static const s16 t32_tmo_tab_d88d4[16] = {
	0, 8, 16, 32, 64, 128, 192, 256, 384, 512, 768, 1024, 1536, 2048, 3072, 4096,
};

/* t32_data_d88f4 (.data +0xa6d4, data.c:27626..27633): block 4: the 14 per-position thresholds + 2 pad */
static const s16 t32_tmo_tab_d88f4[16] = {
	8, 8, 16, 32, 64, 64, 64, 128, 128, 256, 256, 512, 512, 1024, 1024, 0,
};

/* t32_data_d8914 (.data +0xa6f4, data.c:27634..27642): block 2: the 17 point axis of ispint_16 */
static const s16 t32_tmo_tab_d8914[17] = {
	16, 64, 128, 192, 256, 384, 512, 640, 768, 1024, 1280, 1536, 2048, 2560, 3072, 3584,
	4095,
};

/* the vendor tables into the fpga env; d88d4 = the caller's writable copy (start it with t32_tmo_fpga_d88d4_init) */
static inline void t32_tmo_fpga_env_tables(struct t32_tmo_fpga_env *e, s16 *d88d4)
{
	e->c0488 = t32_tmo_tab_c0488;
	e->c04a4 = t32_tmo_tab_c04a4;
	e->d8120 = t32_tmo_tab_d8120;
	e->d8320 = T32_TMO_TAB_D8320;
	e->d8914 = t32_tmo_tab_d8914;
	e->d88d4 = d88d4;
	e->d88a4 = t32_tmo_tab_d88a4;
	e->d88f4 = t32_tmo_tab_d88f4;
	e->d87a0 = t32_tmo_tab_d87a0;
	e->d8324 = t32_tmo_tab_d8324;
	e->c0444 = t32_tmo_tab_c0444;
}

/* d88d4 at module load (the .data initialiser) */
static inline void t32_tmo_fpga_d88d4_init(s16 *d88d4)
{
	memcpy(d88d4, t32_tmo_tab_d88d4, sizeof(t32_tmo_tab_d88d4));
}

#endif /* TX_ISP_T32_TMOTAB_H */
