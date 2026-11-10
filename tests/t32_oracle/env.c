// SPDX-License-Identifier: GPL-2.0
/*
 * Fake kernel / ISP environment for the reference functions in ref/ (Matt
 * Davis' recovered T32 C, opensensor/open-tx-isp PR #15 @ 2e2e06bc).
 *
 * The recovered code calls through u32 "callee" values with up to 64 u32
 * arguments and returns u64 = v1:v0, so every stub below has the o32 shape
 * u64 f(u32 a0, u32 a1, u32 a2, u32 a3); a 64-bit value is passed as
 * (lo, hi) in two registers.  All memory the reference code sees (these
 * arrays, the pool, the stack in t32o_call) is static and the binary is
 * linked non-PIE, so its addresses fit in u32.
 *
 * Data objects keep the OEM names (t32_data_<OEM .data/.bss address>, see
 * driver/t32/data-aliases.lds@2e2e06bc); initial values of .data objects are
 * copied from driver/t32/data.c@2e2e06bc (line numbers below).
 */
#include <stdint.h>
#include <string.h>
#include <ucontext.h>
#include "t32_oracle_env.h"

/* ---- .bss objects (zero at module load) ---- */
u32 t32_data_e28f0[4] __attribute__((aligned(16)));	/* per-channel CCM buffers (tisp_ccm_init) */
u32 t32_data_dd0a4[4] __attribute__((aligned(16)));	/* per-channel tuning-parameter base */
u32 t32_data_dc274[4] __attribute__((aligned(16)));	/* per-channel LUT info (stLutInfo), +24 = CCM knots */
u32 t32_data_da844[0x100] __attribute__((aligned(16)));	/* register shadow (tisp_reg_write mode 1) */

/* top block (tisp_top_init / tisp_top_reg_cfg) */
u8 t32_data_e1460[8] __attribute__((aligned(4)));	/* per-channel "configured" flags */
u32 t32_data_e1464[4];		/* per-channel top enable word */
u32 t32_data_e1474[4];		/* per-pipeline-parity DE-IR flag */
u32 t32_data_e1484[4];		/* per-channel copy of the tuning base */
u8 t32_data_dc284[1808] __attribute__((aligned(4)));	/* 2 x 904 B channel state, +900 = pipeline count */

/* IQ bin parser (tisp_bin_data_parse), per-channel state, all .bss */
u8 t32_data_da850[704] __attribute__((aligned(4)));	/* 336 B per channel, +224 channel count, +232 14 B copy */
u8 t32_data_dd114[224] __attribute__((aligned(4)));	/* 112 B per channel: copy of the bin's section table */
u8 t32_data_dc1a4[112] __attribute__((aligned(4)));	/* 28 B per channel: largest payload sizes seen */
u32 t32_data_e1820[4];					/* gamma payload size per channel */

/* CSC / CSCCR working state (.bss) */
u32 t32_data_e3110[4], t32_data_e3120[4], t32_data_e3130[4];

/* GIB working state (.bss) */
u32 t32_data_e17c0[4];			/* per-channel context { section ptr, params, work block, flag, 24 B } */
u8 t32_data_e1710[16];
u32 t32_data_e1720[6], t32_data_e1738[1], t32_data_e173c[33];

/* DPC context pointers per channel (.bss) */
u32 t32_data_e35f0[4];
u32 t32_data_e2410[4];	/* SDNS context per channel */
u32 t32_data_e1810[4];	/* DMSC context per channel */
u32 t32_data_e1824[4], t32_data_e1834[4];	/* gamma context per channel */
u8 t32_data_e1848[8];
u32 t32_data_e28d0[4];	/* CLM context per channel */
u32 t32_data_e2420[4], t32_data_e2424[4];	/* YSP context per channel */
u32 t32_data_d6c70;	/* YSP sharpness byte stored by tisp_ysp_sharpness_set */
u32 t32_data_e2440[4], t32_data_e2450[4];	/* BCSH context per channel */
u8 t32_data_e2460[112], t32_data_e24d0[300];
u32 t32_data_e23c0[4];
u32 t32_data_e23d0[4];	/* CDNS context per channel */
u8 t32_data_e23e0[64];	/* CDNS value block */	/* YDNS context per channel */

/* MDNS state (.bss): the vendor's neighbouring objects are kept as one block each, the other symbols are offsets into it */
u8 t32_data_e2150[0x180] __attribute__((aligned(16)));
u8 t32_data_e22c8[0x200] __attribute__((aligned(16)));
u8 t32_data_e4100[0x10000] __attribute__((aligned(16)));
u8 t32_data_f0900[0x10000] __attribute__((aligned(16)));
u32 t32_data_dc074[8];
char t32_data_bfb4c[4] = "f", t32_data_c9c80[4] = "f", t32_data_c9cb4[4] = "f", t32_data_c9cf4[4] = "f";
__asm__(".globl t32_data_e2154\n.set t32_data_e2154, t32_data_e2150 + 0x4");
__asm__(".globl t32_data_e2158\n.set t32_data_e2158, t32_data_e2150 + 0x8");
__asm__(".globl t32_data_e215c\n.set t32_data_e215c, t32_data_e2150 + 0xc");
__asm__(".globl t32_data_e2160\n.set t32_data_e2160, t32_data_e2150 + 0x10");
__asm__(".globl t32_data_e2164\n.set t32_data_e2164, t32_data_e2150 + 0x14");
__asm__(".globl t32_data_e2168\n.set t32_data_e2168, t32_data_e2150 + 0x18");
__asm__(".globl t32_data_e216c\n.set t32_data_e216c, t32_data_e2150 + 0x1c");
__asm__(".globl t32_data_e2170\n.set t32_data_e2170, t32_data_e2150 + 0x20");
__asm__(".globl t32_data_e2174\n.set t32_data_e2174, t32_data_e2150 + 0x24");
__asm__(".globl t32_data_e2178\n.set t32_data_e2178, t32_data_e2150 + 0x28");
__asm__(".globl t32_data_e217c\n.set t32_data_e217c, t32_data_e2150 + 0x2c");
__asm__(".globl t32_data_e2180\n.set t32_data_e2180, t32_data_e2150 + 0x30");
__asm__(".globl t32_data_e2184\n.set t32_data_e2184, t32_data_e2150 + 0x34");
__asm__(".globl t32_data_e2188\n.set t32_data_e2188, t32_data_e2150 + 0x38");
__asm__(".globl t32_data_e218c\n.set t32_data_e218c, t32_data_e2150 + 0x3c");
__asm__(".globl t32_data_e2190\n.set t32_data_e2190, t32_data_e2150 + 0x40");
__asm__(".globl t32_data_e2194\n.set t32_data_e2194, t32_data_e2150 + 0x44");
__asm__(".globl t32_data_e22d8\n.set t32_data_e22d8, t32_data_e22c8 + 0x10");
__asm__(".globl t32_data_e2304\n.set t32_data_e2304, t32_data_e22c8 + 0x3c");
__asm__(".globl t32_data_e2330\n.set t32_data_e2330, t32_data_e22c8 + 0x68");
__asm__(".globl t32_data_e235c\n.set t32_data_e235c, t32_data_e22c8 + 0x94");
__asm__(".globl t32_data_e2388\n.set t32_data_e2388, t32_data_e22c8 + 0xc0");

/* data.c:2867 (bfc40, 514 B): tisp_sin table, 257 s16 */
short t32_data_bfc40[258] = { 1535, 1533, 1528, 1520, 1507, 1492, 1473, 1452, 1427, 1399, 1369, 1336, 1300, 1261, 1221, 1180, 1135, 1089, 1043, 995, 946, 897, 847, 796, 745, 695, 645, 595, 546, 499, 452, 406, 361, 318, 277, 238, 200, 164, 130, 99, 69, 41, 16, -8, -29, -47, -64, -80, -92, -103, -112, -119, -125, -129, -131, -132, -132, -131, -128, -125, -121, -116, -110, -104, -98, -91, -84, -77, -70, -62, -55, -49, -43, -37, -31, -25, -21, -16, -12, -9, -7, -5, -3, -1, 0, 0, 0, 0, -1, -2, -4, -6, -8, -9, -12, -15, -17, -19, -22, -24, -26, -27, -30, -32, -33, -34, -35, -35, -36, -36, -36, -36, -35, -33, -33, -31, -29, -28, -26, -23, -21, -19, -16, -13, -11, -8, -5, -2, 0, 2, 4, 7, 9, 11, 13, 14, 16, 18, 19, 19, 20, 21, 21, 21, 21, 21, 21, 20, 19, 18, 18, 17, 15, 14, 13, 12, 11, 10, 8, 7, 6, 5, 4, 3, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 3, 4, 5, 5, 6, 7, 7, 7, 8, 9, 10, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 10, 10, 10, 9, 8, 7, 7, 6, 5, 4, 3, 2, 1, 0, 0, -1, -2, -3, -3, -4, -5, -6, -7, -7, -7, -8, -8, -8, -8, -8, -8, -8, -8, -8, -8, -7, -7, -6, -6, -6, -5, -4, -4, -4, -3, -3, -3, -2, -1, -1, -1, -1, 0, 0, 0, 0, 0 };
/* data.c:2855 (bfc10) and 2861 (bfc28): tap orders of tisp_msca_normalized */
u32 t32_data_bfc10[6] = { 2, 3, 1, 4, 0, 5 };
u32 t32_data_bfc28[6] = { 5, 0, 4, 1, 3, 2 };
char t32_data_c9eac[4] = "f";
/* MSCA curve state (.bss) */
u32 t32_data_e2768[4], t32_data_e28ac[4];
u8 t32_data_e2898[16];

/* AWB state (.bss): neighbouring objects kept as one block, the other symbols are offsets into it */
u8 t32_data_e1694[0x100] __attribute__((aligned(16)));
__asm__(".globl t32_data_e16a4\n.set t32_data_e16a4, t32_data_e1694 + 0x10");
__asm__(".globl t32_data_e16b4\n.set t32_data_e16b4, t32_data_e1694 + 0x20");
__asm__(".globl t32_data_e16c4\n.set t32_data_e16c4, t32_data_e1694 + 0x30");
u8 t32_data_dd6c0[0x400] __attribute__((aligned(16)));
char t32_data_c93f0[4] = "f", t32_data_c9408[4] = "f";
/* data.c:25265 (d6410, 1028 B): default AWB table copied by tisp_awb_init */
u32 t32_data_d6410[257] = { 0x01000100, 0x01000100, 0x01000100, 0x01000100, 0x01000100, 0x01000100, 0x01000100, 0x01000100, 0x01000100, 0x01000100, 0x01000100, 0x01000100, 0x01000100, 0x01000100, 0x00fe0100, 0x00fa00fc, 0x00f600f8, 0x00f200f4, 0x00ee00f0, 0x00ea00ec, 0x00e700e9, 0x00e300e5, 0x00e000e1, 0x00dc00de, 0x00d900da, 0x00d500d7, 0x00d200d4, 0x00cf00d0, 0x00cb00cd, 0x00c800ca, 0x00c500c7, 0x00c200c3, 0x00bf00c0, 0x00bc00bd, 0x00b900ba, 0x00b600b7, 0x00b300b5, 0x00b000b2, 0x00ae00af, 0x00ab00ac, 0x00a800aa, 0x00a600a7, 0x00a300a4, 0x00a000a2, 0x009e009f, 0x009b009d, 0x0099009a, 0x00970098, 0x00940095, 0x00920093, 0x00900091, 0x008d008e, 0x008b008c, 0x0089008a, 0x00870088, 0x00850086, 0x00830084, 0x00810082, 0x007f0080, 0x007d007e, 0x007b007c, 0x0079007a, 0x00770078, 0x00750076, 0x00730074, 0x00710072, 0x00700070, 0x006e006f, 0x006c006d, 0x006a006b, 0x0069006a, 0x00670068, 0x00650066, 0x00640065, 0x00620063, 0x00610062, 0x005f0060, 0x005e005e, 0x005c005d, 0x005b005c, 0x0059005a, 0x00580059, 0x00570057, 0x00550056, 0x00540055, 0x00530053, 0x00510052, 0x00500051, 0x004f004f, 0x004e004e, 0x004c004d, 0x004b004c, 0x004a004b, 0x00490049, 0x00480048, 0x00470047, 0x00450046, 0x00440045, 0x00430044, 0x00420043, 0x00410042, 0x00400041, 0x003f0040, 0x003e003f, 0x003d003e, 0x003c003d, 0x003b003c, 0x003a003b, 0x0039003a, 0x00390039, 0x00380038, 0x00370037, 0x00360036, 0x00350035, 0x00340035, 0x00330034, 0x00330033, 0x00320032, 0x00310031, 0x00300031, 0x00300030, 0x002f002f, 0x002e002e, 0x002d002e, 0x002d002d, 0x002c002c, 0x002b002c, 0x002b002b, 0x002a002a, 0x0029002a, 0x00290029, 0x00280028, 0x00270028, 0x00270027, 0x00260026, 0x00250026, 0x00250025, 0x00240025, 0x00240024, 0x00230023, 0x00230023, 0x00220022, 0x00220022, 0x00210021, 0x00210021, 0x00200020, 0x00200020, 0x001f001f, 0x001f001f, 0x001e001e, 0x001e001e, 0x001d001d, 0x001d001d, 0x001c001c, 0x001c001c, 0x001b001c, 0x001b001b, 0x001a001b, 0x001a001a, 0x001a001a, 0x00190019, 0x00190019, 0x00180019, 0x00180018, 0x00180018, 0x00170018, 0x00170017, 0x00170017, 0x00160016, 0x00160016, 0x00160016, 0x00150015, 0x00150015, 0x00150015, 0x00140014, 0x00140014, 0x00140014, 0x00130013, 0x00130013, 0x00130013, 0x00120013, 0x00120012, 0x00120012, 0x00120012, 0x00110011, 0x00110011, 0x00110011, 0x00100011, 0x00100010, 0x00100010, 0x00100010, 0x000f0010, 0x000f000f, 0x000f000f, 0x000f000f, 0x000f000f, 0x000e000e, 0x000e000e, 0x000e000e, 0x000e000e, 0x000d000e, 0x000d000d, 0x000d000d, 0x000d000d, 0x000d000d, 0x000c000c, 0x000c000c, 0x000c000c, 0x000c000c, 0x000c000c, 0x000b000c, 0x000b000b, 0x000b000b, 0x000b000b, 0x000b000b, 0x000b000b, 0x000a000b, 0x000a000a, 0x000a000a, 0x000a000a, 0x000a000a, 0x000a000a, 0x0009000a, 0x00090009, 0x00090009, 0x00090009, 0x00090009, 0x00090009, 0x00090009, 0x00080009, 0x00080008, 0x00080008, 0x00080008, 0x00080008, 0x00080008, 0x00080008, 0x00080008, 0x00070008, 0x00070007, 0x00070007, 0x00070007, 0x00070007, 0x00070007, 0x00070007, 0x00070007, 0x00070007, 0x00060007, 0x00060006, 0x00060006, 0x00060006, 0x00060006, 0x00060006, 0x00060006, 0x00060006, 0x00060006, 0x00060006, 0x00060006 };

/* weak: t32_awbi_oracle links the reference copy instead */
__attribute__((weak)) u64 tisp_awb_interrupt_static(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a1; (void)a2; (void)a3;
	return 0;
}

__attribute__((weak)) u64 tisp_awb_process(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a1; (void)a2; (void)a3;
	return 0;
}

/* AE state (.bss), neighbouring objects kept as one block with offsets */
u8 t32_data_e1514[0x300] __attribute__((aligned(16)));
__asm__(".globl t32_data_e1588\n.set t32_data_e1588, t32_data_e1514 + 0x74");
__asm__(".globl t32_data_e1598\n.set t32_data_e1598, t32_data_e1514 + 0x84");
__asm__(".globl t32_data_e15a8\n.set t32_data_e15a8, t32_data_e1514 + 0x94");
__asm__(".globl t32_data_e15c8\n.set t32_data_e15c8, t32_data_e1514 + 0xb4");
__asm__(".globl t32_data_e15d8\n.set t32_data_e15d8, t32_data_e1514 + 0xc4");
u8 t32_data_dd2d4[0x400] __attribute__((aligned(16)));
/* data.c:1996 (beea4, 512 B window) */
u32 t32_data_beea4[128] = { 0x40000000, 0x4166c34c, 0x42d561b4, 0x444c0740, 0x45cae0f2, 0x47521cc6, 0x48e1e9ba, 0x4a7a77d4, 0x4c1bf829, 0x4dc69cdd, 0x4f7a9930, 0x51382182, 0x52ff6b55, 0x54d0ad5a, 0x56ac1f75, 0x5891fac1, 0x5a82799a, 0x5c7dd7a4, 0x5e8451d0, 0x60962665, 0x62b39509, 0x64dcdec3, 0x6712460b, 0x69540ec9, 0x6ba27e65, 0x6dfddbcc, 0x70666f76, 0x72dc8374, 0x75606374, 0x77f25cce, 0x7a92be8b, 0x7d41d96e, 0x80000000, 0x00000000, 0x00000000, 0x0001f1ac, 0x0001f1e8, 0x0001f874, 0x0001f8c4, 0x0001f928, 0x0001f938, 0x0001fa54, 0x0001f9a0, 0x0001f9ec, 0x0001f270, 0x0001f570, 0x0001f284, 0x0001f2ac, 0x0001f298, 0x0001f2c0, 0x0001f2d4, 0x0001f570, 0x0001f2e8, 0x0001f410, 0x0001f2fc, 0x0001f310, 0x0001f324, 0x0001f338, 0x0001f35c, 0x0001f370, 0x0001f384, 0x0001f3fc, 0x0001f398, 0x0001f3ac, 0x0001f3d4, 0x0001f3e8, 0x0001f424, 0x0001f3c0, 0x0001f438, 0x0001f458, 0x0001f570, 0x0001f46c, 0x0001f570, 0x0001f570, 0x0001f570, 0x0001f570, 0x0001f570, 0x0001f570, 0x0001f570, 0x0001f490, 0x0001f49c, 0x0001f598, 0x0001f4e4, 0x0001f4bc, 0x0001f4d0, 0x0001f4f8, 0x0001f51c, 0x0001f530, 0x0001f550, 0x0001f600, 0x0001f844, 0x0001f610, 0x0001f634, 0x0001f624, 0x0001f644, 0x0001f654, 0x0001f844, 0x0001f664, 0x0001f744, 0x0001f674, 0x0001f684, 0x0001f694, 0x0001f6a4, 0x0001f6c4, 0x0001f6d4, 0x0001f6e4, 0x0001f734, 0x0001f6f4, 0x0001f704, 0x0001f724, 0x0001f754, 0x0001f764, 0x0001f714, 0x0001f774, 0x0001f78c, 0x0001f844, 0x0001f79c, 0x0001f844, 0x0001f844, 0x0001f844, 0x0001f844, 0x0001f844, 0x0001f844, 0x0001f844, 0x0001f7b4, 0x0001fae4, 0x0001fae4, 0x0001f7ec };
/* data.c:2305 (bf378, 512 B window) */
u32 t32_data_bf378[128] = { 0x0002e094, 0x0002e1d4, 0x0002e2f4, 0x0002e3c0, 0x0002e4c4, 0x0002e59c, 0x0002e6a0, 0x70736974, 0x5f65615f, 0x656b6177, 0x0070755f, 0x70736974, 0x5f65615f, 0x5f697061, 0x5f746573, 0x00737066, 0x70736974, 0x5f65615f, 0x5f697061, 0x5f746573, 0x5f707865, 0x7473696c, 0x00000000, 0x70736974, 0x5f65615f, 0x74696e69, 0x00000000, 0x70736974, 0x5f65615f, 0x736e6573, 0x705f726f, 0x635f7261, 0x736c7274, 0x6470755f, 0x00657461, 0x00000000, 0x00000000, 0x00000000, 0x70736974, 0x5f66615f, 0x74696e69, 0x00000000, 0x0003f858, 0x0003f860, 0x0003f868, 0x0003f880, 0x0003f898, 0x0003f8b0, 0x0003f8c8, 0x0003f8e0, 0x0003f8f8, 0x0003f910, 0x70736974, 0x6277615f, 0x7465735f, 0x646f6d5f, 0x00000065, 0x00000000, 0x00042838, 0x000424fc, 0x00042544, 0x00042590, 0x00042838, 0x00042838, 0x00042838, 0x00042838, 0x00042838, 0x00042838, 0x00042838, 0x00042838, 0x000425d4, 0x00042624, 0x00042670, 0x000426bc, 0x00042700, 0x0004274c, 0x0004279c, 0x000427e8, 0x70736974, 0x6269675f, 0x6970615f, 0x7465735f, 0x636c625f, 0x00000000, 0x00000000, 0x00000000, 0x70736974, 0x63736c5f, 0x72696d5f, 0x5f726f72, 0x70696c66, 0x00000000, 0x70736974, 0x63736c5f, 0x7261705f, 0x615f6d61, 0x79617272, 0x7465735f, 0x00000000, 0x70736974, 0x63736c5f, 0x6c61635f, 0x61705f63, 0x725f6172, 0x65726665, 0x00006873, 0x70736974, 0x736d645f, 0x6e695f63, 0x00007469, 0x70736974, 0x6d61675f, 0x705f616d, 0x6d617261, 0x7272615f, 0x735f7961, 0x00007465, 0x00900000, 0x019c0120, 0x0271020e, 0x031c02ca, 0x03ad0367, 0x042e03ef, 0x04a20469, 0x050d04d8, 0x0570053f, 0x05ce05a0, 0x062605fa };
/* data.c:22589 (d3a40, 2400 B window) */
u32 t32_data_d3a40[600] = { 0x00000400, 0x00000000, 0x00000400, 0x00000000, 0x00000400, 0x00000000, 0x00000400, 0x00000000, 0x00000400, 0x00000000, 0x00000400, 0x00000000, 0x00000400, 0x00000000, 0x00000400, 0x00000000, 0x00000400, 0x00000000, 0x00000400, 0x00000000, 0x00000400, 0x00000000, 0x00000400, 0x00000000, 0x00000400, 0x00000000, 0x00000400, 0x00000000, 0x00000400, 0x00000000, 0x00000400, 0x00000000, 0x00000400, 0x00000000, 0x00000400, 0x00000000, 0x00000400, 0x00000000, 0x00000400, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000400, 0x00000000, 0x00000000, 0x0000000a, 0x00000400, 0x00000400, 0x00000400, 0x0000000a, 0x00000400, 0x00000400, 0x00000400, 0x00000005, 0x00000400, 0x00000400, 0x00000400, 0x00000190, 0x00001000, 0x00001000, 0x00001000, 0x00000005, 0x00000400, 0x00000400, 0x00000400, 0x00000190, 0x00001000, 0x00001000, 0x00001000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00006400, 0x00001400, 0x00006400, 0x00006400, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x0000003c, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000064, 0x00000064, 0x000000c8, 0x000000c8, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x0000000f, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000101, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 };
char t32_data_bee80[4] = "f", t32_data_bee94[4] = "f", t32_data_bf3d4[4] = "f", t32_data_bf3e4[4] = "f", t32_data_c7a10[4] = "f", t32_data_c84e8[4] = "f", t32_data_c8500[4] = "f", t32_data_c8524[4] = "f", t32_data_c8548[4] = "f", t32_data_c8594[4] = "f", t32_data_c85bc[4] = "f";

u64 t32o___ashldi3(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u64 v = (u64)a1 << 32 | a0;

	(void)a3;
	return v << (a2 & 63);
}

u64 t32o___lshrdi3(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u64 v = (u64)a1 << 32 | a0;

	(void)a3;
	return v >> (a2 & 63);
}

/* kernel __div64_32(u64 *n, u32 base): *n /= base, returns the remainder */
u64 __div64_32(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u64 *n = T32O_HOST(a0), v = *n;

	(void)a2; (void)a3;
	if (!a1)
		return 0;
	*n = v / a1;
	return (u32)(v % a1);
}

__attribute__((weak)) u64 tisp_ae_calc_process(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 0; }
__attribute__((weak)) u64 tisp_ae_interrupt_hist(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 0; }
__attribute__((weak)) u64 tisp_ae_interrupt_static(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 0; }

/* LSC state (.bss): neighbouring objects kept as one block with offsets */
u8 t32_data_e17d0[0x80] __attribute__((aligned(16)));
__asm__(".globl t32_data_e17e0\n.set t32_data_e17e0, t32_data_e17d0 + 0x10");
__asm__(".globl t32_data_e17f0\n.set t32_data_e17f0, t32_data_e17d0 + 0x20");
__asm__(".globl t32_data_e17f4\n.set t32_data_e17f4, t32_data_e17d0 + 0x24");
__asm__(".globl t32_data_e1800\n.set t32_data_e1800, t32_data_e17d0 + 0x30");
__asm__(".globl t32_data_e180c\n.set t32_data_e180c, t32_data_e17d0 + 0x3c");
char t32_data_bf504[4] = "f", t32_data_c94d8[4] = "f", t32_data_c94f8[4] = "f", t32_data_c953c[4] = "f", t32_data_c9544[4] = "f";

/*
 * The LSC shadow writers.  Matt's recovered C of tisp_single_lsc_reg_write and
 * tisp_single_lsc_reg_lut_write lost the load of the shadow base pointer (the
 * MIPS has "lui v0,%hi(.bss); lw v0,0x1844(v0)", the C only keeps the hi part), so
 * they are written here from the disassembly (0x1c800, 0x1c84c):
 *   base = *(u32 *)da844;  buf = *(u32 *)(base + (ch << 8) + 204);
 *   reg_write:     buf[idx] = val
 *   reg_lut_write: buf[idx + off] = val
 */
static u32 *lsc_shadow(u32 ch)
{
	u32 *base = T32O_HOST(t32_data_da844[0]);

	return T32O_HOST(*(u32 *)((u8 *)base + (ch << 8) + 204));
}

u64 tisp_single_lsc_reg_write(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a3;
	lsc_shadow(a0)[a1] = a2;
	return 0;
}

u64 tisp_single_lsc_reg_lut_write(u32 a0, u32 a1, u32 a2, u32 a3)
{
	lsc_shadow(a0)[a1 + a3] = a2;
	return 0;
}

/* tisp_get_system_sensor_num: "lbu v0, stIspInitPar + 900" (0x28cd8); Matt's recovered file is empty */
u8 t32o_sensor_num = 1;
u64 tisp_get_system_sensor_num(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a1; (void)a2; (void)a3;
	return t32o_sensor_num;
}

/* the AWB frame-done completion: the oracle compares how often and with which object it was signalled */
u32 t32o_complete_n, t32o_complete_arg;
u64 private_complete(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	t32o_complete_n++;
	t32o_complete_arg = a0;
	return 0;
}

/* tisp_event_push (event queue of the ISP core): records the channel and the first 8 words of the event */
u32 t32o_ev_n, t32o_ev_ch, t32o_ev_w[8];
u32 t32o_evlog[T32O_EVLOG][9];	/* every event: channel + 8 words (the first T32O_EVLOG of a run) */
u8 t32_data_e1690[4];		/* AWB process frame counter (u16) */
u64 tisp_event_push(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	if (t32o_ev_n < T32O_EVLOG) {
		t32o_evlog[t32o_ev_n][0] = a0;
		memcpy(&t32o_evlog[t32o_ev_n][1], (const void *)(unsigned long)a1, 8 * sizeof(u32));
	}
	t32o_ev_n++;
	t32o_ev_ch = a0;
	memcpy(t32o_ev_w, (const void *)(unsigned long)a1, sizeof(t32o_ev_w));
	return 0;
}
__attribute__((weak)) u64 private_init_completion(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 0; }
u64 private_mutex_lock(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 0; }
u64 private_mutex_unlock(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 0; }
/* the wait returns the remaining time: nonzero = the frame-done completion arrived */
__attribute__((weak)) u64 private_wait_for_completion_timeout(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 1; }
u64 tisp_single_lsc_rcfg_cfg_trig(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 0; }

/* ---- .data objects ---- */
/* CSC / CSCCR: mode tables (92 B x 6 for CSC, 12 B x 3 for CSCCR) and defaults */
/* data.c:26937 (92 B) */
u32 t32_data_d7e30[23] = { 0x00000005, 0x000039c2, 0x00009510, 0x00000d0a, 0xffffe09a, 0xffffaef6, 0x00007070, 0x00007070, 0xffff989b, 0xfffff6f5, 0xeb108010, 0x0000f010, 0x00012a15, 0x00000000, 0x0001adbe, 0x00012a15, 0xffffd00b, 0xffff597e, 0x00012a15, 0x0002244b, 0x00000000, 0xeb108010, 0x0000f010 };
/* data.c:26960 (92 B) */
u32 t32_data_d7e8c[23] = { 0x00000004, 0x00004340, 0x0000ad91, 0x00000f2e, 0xffffdc41, 0xffffa3bf, 0x00008000, 0x00008000, 0xffff8a4b, 0xfffff5b5, 0xff008000, 0x0000ff00, 0x00010000, 0x00000000, 0x0001797f, 0x00010000, 0xffffd5e0, 0xffff6dbc, 0x00010000, 0x0001e1a3, 0x00000000, 0xff008000, 0x0000ff00 };
/* data.c:26983 (92 B) */
u32 t32_data_d7ee8[23] = { 0x00000003, 0x00002ebe, 0x00009d3e, 0x00000fe0, 0xffffe63c, 0xffffa953, 0x00007070, 0x00007070, 0xffff99df, 0xfffff5b1, 0xeb108010, 0x0000f010, 0x00012a15, 0x00000000, 0x0001caf1, 0x00012a15, 0xffffc969, 0xffff7793, 0x00012a15, 0x00021cc6, 0x00000000, 0xeb108010, 0x0000f010 };
/* data.c:27006 (92 B) */
u32 t32_data_d7f44[23] = { 0x00000002, 0x0000366d, 0x0000b717, 0x0000127c, 0xffffe2ab, 0xffff9d55, 0x00008000, 0x00008000, 0xffff8bbd, 0xfffff443, 0xff008000, 0x0000ff00, 0x00010000, 0x00000000, 0x00019326, 0x00010000, 0xffffd00c, 0xffff8829, 0x00010000, 0x0001db09, 0x00000000, 0xff008000, 0x0000ff00 };
/* data.c:27029 (92 B) */
u32 t32_data_d7fa0[23] = { 0x00000001, 0x000041bd, 0x0000810f, 0x00001910, 0xffffda0e, 0xffffb581, 0x00007070, 0x00007070, 0xffffa1d9, 0xffffedb7, 0xeb108010, 0x0000f010, 0x00012a15, 0x00000000, 0x00019895, 0x00012a15, 0xffff9bb5, 0xffff2fe1, 0x00012a15, 0x00020469, 0x00000000, 0xeb108010, 0x0000f010 };
/* data.c:27052 (92 B) */
u32 t32_data_d7ffc[23] = { 0x00000000, 0x00004c8b, 0x00009646, 0x00001d2f, 0xffffd4ce, 0xffffab32, 0x00008000, 0x00008000, 0xffff94d1, 0xffffeb2f, 0xff008000, 0x0000ff00, 0x00010000, 0x00000000, 0x000166e9, 0x00010000, 0xffffa7e7, 0xffff492e, 0x00010000, 0x0001c5a2, 0x00000000, 0xff008000, 0x0000ff00 };
/* data.c:27075 (92 B) */
u32 t32_data_d8058[23] = { 0x00000000, 0x00004c8b, 0x00009646, 0x00001d2f, 0xffffd4ce, 0xffffab32, 0x00008000, 0x00008000, 0xffff94d1, 0xffffeb2f, 0xff008000, 0x0000ff00, 0x00010000, 0x00000000, 0x000166e9, 0x00010000, 0xffffa7e7, 0xffff492e, 0x00010000, 0x0001c5a2, 0x00000000, 0xff008000, 0x0000ff00 };
/* data.c:27098 (40 B) */
u32 t32_data_d80b4[10] = { 0x00000000, 0x00000000, 0x01320000, 0x00750259, 0x015300ad, 0x02000200, 0x005301ad, 0xff008000, 0x0000ff00, 0x00000000 };
/* data.c:27109 (12 B) */
u32 t32_data_d80e0[3] = { 0x00000002, 0xeb10ff00, 0xf010ff00 };
/* data.c:27112 (12 B) */
u32 t32_data_d80ec[3] = { 0x00000001, 0xff00eb10, 0xff00f010 };
/* data.c:27115 (12 B) */
u32 t32_data_d80f8[3] = { 0x00000000, 0xff00ff00, 0xff00ff00 };
/* data.c:27118 (16 B) */
u32 t32_data_d8104[4] = { 0x00000400, 0x000000ff, 0xff800400, 0x0000007f };
/* data.c:27122 (16 B) */
u32 t32_data_d8114[4] = { 0x00000000, 0xff00ff00, 0xff00ff00, 0x000000ff };
/* data.c:3019 (bfea0): MIPS jump table of tisp_csc_para_refresh_by_mode (code addresses, not host pointers) */
u32 t32_data_bfea0[6] = { 0x8df98, 0x8dfa4, 0x8dfb0, 0x8dfbc, 0x8dfc8, 0x8dfd4 };
/* data.c:3025 (bfeb8): mode valid flags; bfec0: pointers to the six mode tables, bfee0: CSCCR valid flags,
 * bfee4: pointers to the three CSCCR tables (the two pointer tables are filled by t32o_reset) */
u8 t32_data_bfeb8[8] = { 1, 1, 1, 1, 1, 1, 0, 0 };
u32 t32_data_bfec0[6];
u8 t32_data_bfee0[4] = { 1, 1, 1, 0 };
u32 t32_data_bfee4[3];
/* data.c:2363 (bf460): MIPS jump table of tisp_gib_init (code addresses) per Bayer pattern */
u32 t32_data_bf460[20] = { 0x42838, 0x424fc, 0x42544, 0x42590, 0x42838, 0x42838, 0x42838, 0x42838, 0x42838,
			   0x42838, 0x42838, 0x42838, 0x425d4, 0x42624, 0x42670, 0x426bc, 0x42700, 0x4274c,
			   0x4279c, 0x427e8 };
/* data.c:25525 (d6820, 20 B): initial gain state */
u32 t32_data_d6820[5] = { 0x400, 0x400, 0, 0, 0 };
char t32_data_c9474[4] = "f";
/* function-name / printf strings of the DPC (content irrelevant) */
char t32_data_bfbc0[4] = "f", t32_data_c9e34[4] = "f";
char t32_data_bfbd0[4] = "f", t32_data_c9e4c[4] = "f";
char t32_data_bf520[4] = "f", t32_data_c95d4[4] = "f";
/* gamma: default curves (data.c) */
/* data.c:25533 (264 B) */
u32 t32_data_d6840[66] = { 0x00000000, 0x00900000, 0x01aa0120, 0x0281021d, 0x032c02da, 0x03bd0377, 0x043e0400, 0x04b3047a, 0x051e04e9, 0x05810550, 0x05de05b0, 0x0636060b, 0x06890660, 0x06d906b1, 0x072506ff, 0x076e074a, 0x07b40791, 0x07f807d6, 0x083a0819, 0x0879085a, 0x08b70898, 0x08f308d5, 0x092d0910, 0x0966094a, 0x099e0982, 0x09d409b9, 0x0a0909ef, 0x0a3d0a23, 0x0a700a57, 0x0aa20a89, 0x0ad30aba, 0x0b030aeb, 0x0b320b1a, 0x0b600b49, 0x0b8e0b77, 0x0bbb0ba4, 0x0be70bd1, 0x0c120bfc, 0x0c3d0c28, 0x0c670c52, 0x0c910c7c, 0x0cb90ca5, 0x0ce20cce, 0x0d0a0cf6, 0x0d310d1e, 0x0d580d45, 0x0d7e0d6b, 0x0da40d92, 0x0dca0db7, 0x0def0ddd, 0x0e140e01, 0x0e380e26, 0x0e5c0e4a, 0x0e7f0e6e, 0x0ea20e91, 0x0ec50eb4, 0x0ee70ed6, 0x0f0a0ef9, 0x0f2b0f1a, 0x0f4d0f3c, 0x0f6e0f5d, 0x0f8f0f7e, 0x0faf0f9f, 0x0fcf0fbf, 0x0fef0fdf, 0x00000fff };
/* data.c:25599 (260 B) */
u32 t32_data_d6948[65] = { 0x0000ff00, 0x01200090, 0x021d01aa, 0x02da0281, 0x0377032c, 0x040003bd, 0x047a043e, 0x04e904b3, 0x0550051e, 0x05b00581, 0x060b05de, 0x06600636, 0x06b10689, 0x06ff06d9, 0x074a0725, 0x0791076e, 0x07d607b4, 0x081907f8, 0x085a083a, 0x08980879, 0x08d508b7, 0x091008f3, 0x094a092d, 0x09820966, 0x09b9099e, 0x09ef09d4, 0x0a230a09, 0x0a570a3d, 0x0a890a70, 0x0aba0aa2, 0x0aeb0ad3, 0x0b1a0b03, 0x0b490b32, 0x0b770b60, 0x0ba40b8e, 0x0bd10bbb, 0x0bfc0be7, 0x0c280c12, 0x0c520c3d, 0x0c7c0c67, 0x0ca50c91, 0x0cce0cb9, 0x0cf60ce2, 0x0d1e0d0a, 0x0d450d31, 0x0d6b0d58, 0x0d920d7e, 0x0db70da4, 0x0ddd0dca, 0x0e010def, 0x0e260e14, 0x0e4a0e38, 0x0e6e0e5c, 0x0e910e7f, 0x0eb40ea2, 0x0ed60ec5, 0x0ef90ee7, 0x0f1a0f0a, 0x0f3c0f2b, 0x0f5d0f4d, 0x0f7e0f6e, 0x0f9f0f8f, 0x0fbf0faf, 0x0fdf0fcf, 0x0fff0fef };
/* data.c:25664 (264 B) */
u32 t32_data_d6a4c[66] = { 0x000000ff, 0x01200090, 0x021d01aa, 0x02da0281, 0x0377032c, 0x040003bd, 0x047a043e, 0x04e904b3, 0x0550051e, 0x05b00581, 0x060b05de, 0x06600636, 0x06b10689, 0x06ff06d9, 0x074a0725, 0x0791076e, 0x07d607b4, 0x081907f8, 0x085a083a, 0x08980879, 0x08d508b7, 0x091008f3, 0x094a092d, 0x09820966, 0x09b9099e, 0x09ef09d4, 0x0a230a09, 0x0a570a3d, 0x0a890a70, 0x0aba0aa2, 0x0aeb0ad3, 0x0b1a0b03, 0x0b490b32, 0x0b770b60, 0x0ba40b8e, 0x0bd10bbb, 0x0bfc0be7, 0x0c280c12, 0x0c520c3d, 0x0c7c0c67, 0x0ca50c91, 0x0cce0cb9, 0x0cf60ce2, 0x0d1e0d0a, 0x0d450d31, 0x0d6b0d58, 0x0d920d7e, 0x0db70da4, 0x0ddd0dca, 0x0e010def, 0x0e260e14, 0x0e4a0e38, 0x0e6e0e5c, 0x0e910e7f, 0x0eb40ea2, 0x0ed60ec5, 0x0ef90ee7, 0x0f1a0f0a, 0x0f3c0f2b, 0x0f5d0f4d, 0x0f7e0f6e, 0x0f9f0f8f, 0x0fbf0faf, 0x0fdf0fcf, 0x0fff0fef, 0x18100800 };
char t32_data_c95ec[4] = "f";
char t32_data_ca188[4] = "f";
char t32_data_bfbe0[4] = "f", t32_data_c9e64[4] = "f";
/* data.c:2847 (20 B) */
u32 t32_data_bfbf0[5] = { 0x00000000, 0x000061f8, 0x0000b505, 0x0000ec83, 0x00010000 };
/* data.c:25841 (24 B) */
u32 t32_data_d6d10[6] = { 0x00000000, 0x00000000, 0x00001388, 0x000000ff, 0x00000000, 0x00000000 };
/* data.c:25805 (144 B) */
u32 t32_data_d6c80[36] = { 0x80808080, 0x00000100, 0x00010000, 0x00000000, 0x00000000, 0x00000000, 0x00010000, 0x00000000, 0x00000000, 0x00000000, 0x00010000, 0x00000000, 0x00004c8b, 0x00009646, 0x00001d2f, 0xffffd4ce, 0xffffab32, 0x00008000, 0x00008000, 0xffff94d1, 0xffffeb2f, 0xff008000, 0x0000ff00, 0x00010000, 0x00000000, 0x000166e9, 0x00010000, 0xffffa7e7, 0xffff492e, 0x00010000, 0x0001c5a2, 0x00000000, 0xff008000, 0x0000ff00, 0x00000000, 0x00000000 };
char t32_data_c9e7c[4] = "f";
char t32_data_bfbb0[4] = "f", t32_data_c9e1c[4] = "f";
char t32_data_c0a80[4] = "f", t32_data_c0a90[4] = "f", t32_data_ca63c[4] = "f", t32_data_ca624[4] = "f";
char t32_data_ca2b4[4] = "f", t32_data_ca2e0[4] = "f", t32_data_ca314[4] = "f";

/* data.c:19093 (+0x2180) = 1: tisp_reg_write queues into the shadow buffer.
 * The oracle sets mode 0 (direct system_reg_write) so it sees each write;
 * the mode is a transport choice and not part of the block's arithmetic. */
u8 t32_data_d03a0[4] __attribute__((aligned(4))) = { 0 };
/* data.c (bf2d0): first 8 words of the table the parser's checksum uses (the standard CRC-32 table start) */
u32 t32_data_bf2d0[8] = { 0, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3 };
/* data.c (c8064 "1.00", c806c "%s", c8070 "PRJ007"): version tag, format, project name checked in the bin header */
char t32_data_c8064[8] = "1.00", t32_data_c806c[4] = "%s", t32_data_c8070[8] = "PRJ007";
/* printf format strings of the parser: content irrelevant */
char t32_data_bf23c[4] = "f", t32_data_c7ff8[4] = "f", t32_data_c8030[4] = "f", t32_data_c8078[4] = "f",
     t32_data_c80b8[4] = "f", t32_data_c80f4[4] = "f", t32_data_c8134[4] = "f", t32_data_c8174[4] = "f",
     t32_data_c81a4[4] = "f";
/* data.c:19437.. (+0x26e0, 128 B): colour-order map of tisp_top_init */
u32 t32_data_d0900[32] = { 0, 1, 2, 3, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23 };
/* data.c:25849 (+0x8b10, 40 B): API matrix default, unity, flag byte 0 */
u32 t32_data_d6d30[10] = { 0, 0x400, 0, 0, 0, 0x400, 0, 0, 0, 0x400 };
/* data.c:25859 (+0x8b38, 36 B): matrix default, unity */
u32 t32_data_d6d58[9] = { 0x400, 0, 0, 0, 0x400, 0, 0, 0, 0x400 };
/* data.c:25868 (+0x8b5c, 104 B): working parameter block default */
u32 t32_data_d6d7c[26] = { 0x400, 0, 0, 0, 0x400, 0, 0, 0, 0x400, 0, 0, 0, 0, 0, 0,
			   0x00800010, 0x06000400, 0x00800040, 0, 0,
			   0x04000400, 0x04000400, 0x04000400, 0x04000400, 0x04000400, 0x04000400 };
/* data.c:25895 (+0x8bc8, 16 B): state { ev lo, ev hi, ct 5000, 255 } */
u32 t32_data_d6de8[4] = { 0, 0, 0x1388, 0xff };
/* printf format strings: content irrelevant */
char t32_data_bfe60[4] = "f", t32_data_ca1b4[4] = "f", t32_data_ca1f8[4] = "f";

u32 t32o_reg88;

/* ---- log ---- */
struct t32o_log t32o_log;


/* ---- LCE init chain (tisp_lce_init) ---- */
/* rodata 0x2500..0x2570: size tables (bf920 4 B, bf924 16 B, bf934 40 B, bf95c 16 B, bf96c 4 B) and strings (data.c:2667) */
u32 t32_data_bf920[28] = { 0x3c3c0000, 0x00000e10, 0x00000e10, 0x00000e10, 0x00000e10, 0x00001234, 0x00001234, 0x00001234, 0x00001234, 0x00001234, 0x00001234, 0x00001234, 0x00001234, 0x00001234, 0x00000006, 0x00002469, 0x00002469, 0x00002469, 0x00002469, 0x38383838, 0x70736974, 0x65636c5f, 0x696e695f, 0x00000074, 0x70736974, 0x65636c5f, 0x6c616d5f, 0x5f636f6c };
__asm__(".globl t32_data_bf924, t32_data_bf934, t32_data_bf95c, t32_data_bf96c, t32_data_bf970, t32_data_bf980\n"
	".set t32_data_bf924, t32_data_bf920+4\n.set t32_data_bf934, t32_data_bf920+20\n.set t32_data_bf95c, t32_data_bf920+60\n"
	".set t32_data_bf96c, t32_data_bf920+76\n.set t32_data_bf970, t32_data_bf920+80\n.set t32_data_bf980, t32_data_bf920+96");
/* rodata.str 0x5f84.. (data.c:12746): printk formats, only handed to isp_printf */
u32 t32_data_c9694[24] = { 0x5053495b, 0x73255b5d, 0x6425202c, 0x6b203a5d, 0x6c6c616d, 0x6520636f, 0x726f7272, 0x0a212121, 0x00000000, 0x5053495b, 0x73255b5d, 0x6425202c, 0x76203a5d, 0x6c6c616d, 0x6520636f, 0x726f7272, 0x0a212121, 0x00000000, 0x2d2d2d0a, 0x2d2d2d2d, 0x2d2d2d2d, 0x2d2d2d2d, 0x2d2d2d2d, 0x2d2d2d2d };
__asm__(".globl t32_data_c96b8, t32_data_c96dc\n.set t32_data_c96b8, t32_data_c9694+36\n.set t32_data_c96dc, t32_data_c9694+72");
/* data 0x8a44 (data.c:25798): size_para_calc tables; d6c65 / d6c67 are bytes inside it */
u32 t32_data_d6c64[4] = { 0x14140c0b, 0x00000000, 0x00000000, 0x00000080 };
__asm__(".globl t32_data_d6c65, t32_data_d6c67\n.set t32_data_d6c65, t32_data_d6c64+1\n.set t32_data_d6c67, t32_data_d6c64+3");
u8 t32_data_e188c[64] __attribute__((aligned(4)));	/* 4 x 3 u16 band table */
u8 t32_data_e1d74[8];					/* per channel "curve written" flag */
u8 t32_data_e1d80[64] __attribute__((aligned(4)));	/* 40 B copy of the bf934 table */
u32 t32_data_e1da8[4];					/* per channel LCE state (0x2850 B) */
u8 t32_data_e1db8[4 * 36] __attribute__((aligned(4)));	/* per channel buffer set (36 B) */
void tisp_lce_interrupt_static(void) {}
void tisp_lsf_interrupt_static(void) {}
void tisp_lce_par(void) {}
u64 private_dma_cache_sync(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 0; }

/* ---- heap pool for private_vmalloc and the oracle's own buffers ---- */
static u8 pool[8u << 20] __attribute__((aligned(64)));
static u32 pool_used;

void *t32o_alloc(u32 size)
{
	void *p;

	size = (size + 63u) & ~63u;
	if (pool_used + size > sizeof(pool))
		return 0;
	p = pool + pool_used;
	pool_used += size;
	memset(p, 0, size);
	return p;
}

void t32o_reset(void)
{
	pool_used = 0;
	memset(&t32o_log, 0, sizeof(t32o_log));
	memset(t32_data_e28f0, 0, sizeof(t32_data_e28f0));
	memset(t32_data_dd0a4, 0, sizeof(t32_data_dd0a4));
	memset(t32o_ctbss, 0, sizeof(t32o_ctbss));
	memset(t32_data_dc274, 0, sizeof(t32_data_dc274));
	memset(t32_data_da844, 0, sizeof(t32_data_da844));
	memset(t32_data_e1460, 0, sizeof(t32_data_e1460));
	memset(t32_data_e1464, 0, sizeof(t32_data_e1464));
	memset(t32_data_e1474, 0, sizeof(t32_data_e1474));
	memset(t32_data_e1484, 0, sizeof(t32_data_e1484));
	memset(t32_data_dc284, 0, sizeof(t32_data_dc284));
	memset(t32_data_da850, 0, sizeof(t32_data_da850));
	memset(t32_data_dd114, 0, sizeof(t32_data_dd114));
	memset(t32_data_dc1a4, 0, sizeof(t32_data_dc1a4));
	memset(t32_data_e1820, 0, sizeof(t32_data_e1820));
	memset(t32_data_e188c, 0, sizeof(t32_data_e188c));
	memset(t32_data_e1d74, 0, sizeof(t32_data_e1d74));
	memset(t32_data_e1d80, 0, sizeof(t32_data_e1d80));
	memset(t32_data_e1da8, 0, sizeof(t32_data_e1da8));
	memset(t32_data_e1db8, 0, sizeof(t32_data_e1db8));
	memset(t32_data_e3110, 0, sizeof(t32_data_e3110));
	memset(t32_data_e3120, 0, sizeof(t32_data_e3120));
	memset(t32_data_e3130, 0, sizeof(t32_data_e3130));
	memset(t32_data_e23d0, 0, sizeof(t32_data_e23d0));
	memset(t32_data_e23e0, 0, sizeof(t32_data_e23e0));
	memset(t32_data_e2410, 0, sizeof(t32_data_e2410));
	memset(t32_data_e1810, 0, sizeof(t32_data_e1810));
	memset(t32_data_e1824, 0, sizeof(t32_data_e1824));
	memset(t32_data_e1834, 0, sizeof(t32_data_e1834));
	memset(t32_data_e1848, 0, sizeof(t32_data_e1848));
	memset(t32_data_e28d0, 0, sizeof(t32_data_e28d0));
	memset(t32_data_e2420, 0, sizeof(t32_data_e2420));
	memset(t32_data_e2424, 0, sizeof(t32_data_e2424));
	memset(t32_data_e2440, 0, sizeof(t32_data_e2440));
	memset(t32_data_e2450, 0, sizeof(t32_data_e2450));
	memset(t32_data_e2460, 0, sizeof(t32_data_e2460));
	memset(t32_data_e24d0, 0, sizeof(t32_data_e24d0));
	memset(t32_data_e2150, 0, sizeof(t32_data_e2150));
	memset(t32_data_e22c8, 0, sizeof(t32_data_e22c8));
	memset(t32_data_e4100, 0, sizeof(t32_data_e4100));
	memset(t32_data_f0900, 0, sizeof(t32_data_f0900));
	memset(t32_data_dc074, 0, sizeof(t32_data_dc074));
	memset(t32_data_e2768, 0, sizeof(t32_data_e2768));
	memset(t32_data_e28ac, 0, sizeof(t32_data_e28ac));
	memset(t32_data_e2898, 0, sizeof(t32_data_e2898));
	memset(t32_data_e1694, 0, sizeof(t32_data_e1694));
	memset(t32_data_dd6c0, 0, sizeof(t32_data_dd6c0));
	memset(t32_data_e1514, 0, sizeof(t32_data_e1514));
	memset(t32_data_dd2d4, 0, sizeof(t32_data_dd2d4));
	memset(t32_data_e17d0, 0, sizeof(t32_data_e17d0));
	memset(t32_data_e23c0, 0, sizeof(t32_data_e23c0));
	memset(t32_data_e35f0, 0, sizeof(t32_data_e35f0));
	memset(t32_data_e17c0, 0, sizeof(t32_data_e17c0));
	memset(t32_data_e1710, 0, sizeof(t32_data_e1710));
	memset(t32_data_e1720, 0, sizeof(t32_data_e1720));
	memset(t32_data_e1738, 0, sizeof(t32_data_e1738));
	memset(t32_data_e173c, 0, sizeof(t32_data_e173c));
	t32_data_bfec0[0] = T32O_PTR(t32_data_d7ffc);
	t32_data_bfec0[1] = T32O_PTR(t32_data_d7fa0);
	t32_data_bfec0[2] = T32O_PTR(t32_data_d7f44);
	t32_data_bfec0[3] = T32O_PTR(t32_data_d7ee8);
	t32_data_bfec0[4] = T32O_PTR(t32_data_d7e8c);
	t32_data_bfec0[5] = T32O_PTR(t32_data_d7e30);
	t32_data_bfee4[0] = T32O_PTR(t32_data_d80f8);
	t32_data_bfee4[1] = T32O_PTR(t32_data_d80ec);
	t32_data_bfee4[2] = T32O_PTR(t32_data_d80e0);
}

/* ---- kernel / driver stubs (o32 shape) ---- */
__attribute__((weak)) u64 private_vmalloc(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	return a0 ? (u32)(uintptr_t)t32o_alloc(a0) : 0;
}

/* kernel allocators: vmalloc(0) and kmalloc(0) return NULL as in the kernel's callers expect an error */
u64 __attribute__((weak)) private_kmalloc(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	return a0 ? (u32)(uintptr_t)t32o_alloc(a0) : 0;
}

u64 __attribute__((weak)) private_kfree(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a1; (void)a2; (void)a3;
	return 0;
}

__attribute__((weak)) u64 private_vfree(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a1; (void)a2; (void)a3;
	return 0;
}

u64 __attribute__((weak)) private_spin_lock_init(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a1; (void)a2; (void)a3;
	return 0;
}

u64 __attribute__((weak)) __private_spin_lock_irqsave(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a1; (void)a2; (void)a3;
	return 0;	/* saved flags */
}

u64 __attribute__((weak)) private_spin_unlock_irqrestore(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a1; (void)a2; (void)a3;
	return 0;
}

u8 t32_small_bss[64] __attribute__((aligned(16)));

u64 printk(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a1; (void)a2; (void)a3;
	t32o_log.printfs++;
	return 0;
}

u64 private_raw_mutex_init(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a1; (void)a2; (void)a3;
	return 0;
}

u64 system_reg_write(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	if (t32o_log.n < T32O_MAX_WRITES) {
		t32o_log.w[t32o_log.n].reg = a0;
		t32o_log.w[t32o_log.n].val = a1;
	}
	t32o_log.n++;
	return 0;
}

u32 (*t32o_rd_hook)(u32 reg);	/* set by an oracle that scripts register reads */

u64 system_reg_read(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	if (t32o_rd_hook)
		return t32o_rd_hook(a0);
	if (a0 == 0x88)
		return t32o_reg88;	/* the Bayer/format register the DPC init tests */
	return 0x5a000000u | a0;	/* only the second pipeline reads back */
}

u64 __attribute__((weak)) isp_printf(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a1; (void)a2; (void)a3;
	t32o_log.printfs++;
	return 0;
}

u64 __attribute__((weak)) system_irq_func_set(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a2; (void)a3;
	t32o_log.irq = a1;
	return 0;
}

u64 tisp_ccm_irq_frame_done(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a1; (void)a2; (void)a3;
	return 0;
}

/* kernel div64_u64(u64 dividend, u64 divisor) */
u64 div64_u64(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u64 n = (u64)a1 << 32 | a0, d = (u64)a3 << 32 | a2;

	return d ? n / d : 0;
}

/* libgcc __ashrdi3(s64, int): renamed by objcopy (host libgcc has none) */
u64 t32o___ashrdi3(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a3;
	return (u64)((int64_t)((u64)a1 << 32 | a0) >> (a2 & 63));
}

/* memcpy: renamed by objcopy so the reference never reaches libc through a
 * truncated address */
u64 t32o_memcpy(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a3;
	memmove((void *)(uintptr_t)a0, (const void *)(uintptr_t)a1, a2);
	return a0;
}

/* ---- run a reference function on a stack below 4 GiB ---- */
static u8 stack_lo[1u << 20] __attribute__((aligned(64)));
static ucontext_t ctx_main, ctx_ref;
static t32o_fn call_fn;
typedef u64 (*t32o_fn7)(u32, u32, u32, u32, u32, u32, u32, u32);
static u32 call_a[5];
u32 t32o_arg6;		/* seventh argument (a function with 7 arguments) */
u32 t32o_arg7;		/* eighth argument (a function with 8 arguments, default 0) */
u32 t32o_arg5;		/* sixth argument of the next t32o_call (a function with 6 arguments) */
static u64 call_ret;

static void trampoline(void)
{
	call_ret = ((t32o_fn7)(uintptr_t)call_fn)(call_a[0], call_a[1], call_a[2], call_a[3], call_a[4], t32o_arg5, t32o_arg6, t32o_arg7);
}

u64 t32o_call(t32o_fn fn, u32 a0, u32 a1, u32 a2, u32 a3, u32 a4)
{
	call_fn = fn;
	call_a[0] = a0; call_a[1] = a1; call_a[2] = a2; call_a[3] = a3; call_a[4] = a4;
	getcontext(&ctx_ref);
	ctx_ref.uc_stack.ss_sp = stack_lo;
	ctx_ref.uc_stack.ss_size = sizeof(stack_lo);
	ctx_ref.uc_link = &ctx_main;
	makecontext(&ctx_ref, trampoline, 0);
	swapcontext(&ctx_main, &ctx_ref);
	return call_ret;
}

/* ---- tisp_awb_ct_detect_simd_3 (0x209b8) data ---- */
/* rodata data.c:2167..2173 (bf150): index bytes 0..9, then u16 {10,20,40,80}, u16 {0,0,102,1024} */
u32 t32_data_bf150[7] = { 0x03020100, 0x07060504, 0x00000908, 0x0014000a, 0x00500028, 0x00000000, 0x04000066 };
__asm__(".globl t32_data_bf15c, t32_data_bf164\n.set t32_data_bf15c, t32_data_bf150+12\n.set t32_data_bf164, t32_data_bf150+20");
/* data.c:16887 (ce130): table indexed by zone class, 240 B up to the end of the rodata section (the table length is fixed by the phase that reads it) */
u32 t32_data_ce130[60] = { 0x01010100,0x03030302,0x05050504,0x07070706,0x01010100,0x03030302,0x05050504,0x07070706,0x01010108,0x0303030a,0x0505050c,0x0707070e,0x01010110,0x03030312,0x05050514,0x07070716,0x01010118,0x0303031a,0x0505051c,0x0707071e,0x18100800,0x19110901,0x01010101,0x01010101,0x06040200,0x0e0c0a08,0x07050301,0x0f0d0b09,0x0c080400,0x1c181410,0x0d090501,0x1d191511,0x01020100,0x01060104,0x010a0108,0x010e010c,0x01120110,0x01160114,0x011a0118,0x011e011c,0x0a080200,0x1a181210,0x0b090301,0x1b191311,0x01011210,0x05051614,0x09091a18,0x0d0d1e1c,0x01010200,0x05050604,0x09090a08,0x0d0d0e0c,0x00010001,0x00010001,0x00010001,0x00010001,0x18100800,0x19110901,0x02020202,0x02020202 };
/* the LCE simd functions address further 16 B constants of the same .rodata.cst16 table (data.c:16887 section base ce130) */
__asm__(".globl t32_data_ce140, t32_data_ce150, t32_data_ce160, t32_data_ce170, t32_data_ce180, t32_data_ce190, t32_data_ce1a0, t32_data_ce1b0, t32_data_ce1c0, t32_data_ce1d0, t32_data_ce1e0, t32_data_ce1f0\n.set t32_data_ce140, t32_data_ce130+16\n.set t32_data_ce150, t32_data_ce130+32\n.set t32_data_ce160, t32_data_ce130+48\n.set t32_data_ce170, t32_data_ce130+64\n.set t32_data_ce180, t32_data_ce130+80\n.set t32_data_ce190, t32_data_ce130+96\n.set t32_data_ce1a0, t32_data_ce130+112\n.set t32_data_ce1b0, t32_data_ce130+128\n.set t32_data_ce1c0, t32_data_ce130+144\n.set t32_data_ce1d0, t32_data_ce130+160\n.set t32_data_ce1e0, t32_data_ce130+176\n.set t32_data_ce1f0, t32_data_ce130+192\n");
/* data.c:25729 (d6b50): 20 byte column offset table of lce_self_light_correct_local_simd_test (the 3 words after it belong to other tables) */
u32 t32_data_d6b50[5] = { 0x18100800,0x38302820,0x70605040,0xb0a09080,0xf0e0d0c0 };
/* data.c:2619 (bf860): 32 x 0x400 u16 table copied by lce_hist_method_16bit_simd into a buffer it overwrites before reading; data.c:2701 (bf9a8): 1024/n for n = 0..31 */
u32 t32_data_bf860[16] = { 0x04000400,0x04000400,0x04000400,0x04000400,0x04000400,0x04000400,0x04000400,0x04000400,0x04000400,0x04000400,0x04000400,0x04000400,0x04000400,0x04000400,0x04000400,0x04000400 };
/* the 32 entry table is followed by more rodata (data.c:2717..): lce_hist_method reads it unchecked for n >= 32, 24 words (n <= 46) kept */
u32 t32_data_bf9a8[24] = { 0x04000400,0x01550200,0x00cd0100,0x009200ab,0x00720080,0x005d0066,0x004f0055,0x00440049,0x003c0040,0x00360039,0x00310033,0x002d002f,0x0029002b,0x00260027,0x00230025,0x00210022,0x00000020,0x2aab7fff,0x0ccd1555,0x06180889,0x038e0492,0x025402d8,0x01a401f0,0x01380168 };
/* printk format "%d\n" (c7d34, data.c:11122) */
u32 t32_data_c7d34[1] = { 0x000a6425 };
/* .bss scratch of the colour temperature detection, dadf0 .. dc054: one block, every OEM symbol is an offset */
u8 t32o_ctbss[0x1270] __attribute__((aligned(16)));
__asm__(".globl t32_data_dadf0\n.set t32_data_dadf0, t32o_ctbss + 0x0");
__asm__(".globl t32_data_dae00\n.set t32_data_dae00, t32o_ctbss + 0x10");
__asm__(".globl t32_data_dae10\n.set t32_data_dae10, t32o_ctbss + 0x20");
__asm__(".globl t32_data_dae20\n.set t32_data_dae20, t32o_ctbss + 0x30");
__asm__(".globl t32_data_dae30\n.set t32_data_dae30, t32o_ctbss + 0x40");
__asm__(".globl t32_data_dae40\n.set t32_data_dae40, t32o_ctbss + 0x50");
__asm__(".globl t32_data_dae50\n.set t32_data_dae50, t32o_ctbss + 0x60");
__asm__(".globl t32_data_dae60\n.set t32_data_dae60, t32o_ctbss + 0x70");
__asm__(".globl t32_data_dae70\n.set t32_data_dae70, t32o_ctbss + 0x80");
__asm__(".globl t32_data_dae98\n.set t32_data_dae98, t32o_ctbss + 0xa8");
__asm__(".globl t32_data_daec0\n.set t32_data_daec0, t32o_ctbss + 0xd0");
__asm__(".globl t32_data_daee8\n.set t32_data_daee8, t32o_ctbss + 0xf8");
__asm__(".globl t32_data_daf10\n.set t32_data_daf10, t32o_ctbss + 0x120");
__asm__(".globl t32_data_daf24\n.set t32_data_daf24, t32o_ctbss + 0x134");
__asm__(".globl t32_data_daf30\n.set t32_data_daf30, t32o_ctbss + 0x140");
__asm__(".globl t32_data_daf58\n.set t32_data_daf58, t32o_ctbss + 0x168");
__asm__(".globl t32_data_daf80\n.set t32_data_daf80, t32o_ctbss + 0x190");
__asm__(".globl t32_data_dafa8\n.set t32_data_dafa8, t32o_ctbss + 0x1b8");
__asm__(".globl t32_data_dafd0\n.set t32_data_dafd0, t32o_ctbss + 0x1e0");
__asm__(".globl t32_data_db000\n.set t32_data_db000, t32o_ctbss + 0x210");
__asm__(".globl t32_data_db010\n.set t32_data_db010, t32o_ctbss + 0x220");
__asm__(".globl t32_data_db020\n.set t32_data_db020, t32o_ctbss + 0x230");
__asm__(".globl t32_data_db030\n.set t32_data_db030, t32o_ctbss + 0x240");
__asm__(".globl t32_data_db040\n.set t32_data_db040, t32o_ctbss + 0x250");
__asm__(".globl t32_data_db050\n.set t32_data_db050, t32o_ctbss + 0x260");
__asm__(".globl t32_data_db060\n.set t32_data_db060, t32o_ctbss + 0x270");
__asm__(".globl t32_data_db150\n.set t32_data_db150, t32o_ctbss + 0x360");
__asm__(".globl t32_data_db1d0\n.set t32_data_db1d0, t32o_ctbss + 0x3e0");
__asm__(".globl t32_data_db250\n.set t32_data_db250, t32o_ctbss + 0x460");
__asm__(".globl t32_data_db2d0\n.set t32_data_db2d0, t32o_ctbss + 0x4e0");
__asm__(".globl t32_data_db3c0\n.set t32_data_db3c0, t32o_ctbss + 0x5d0");
__asm__(".globl t32_data_db4b0\n.set t32_data_db4b0, t32o_ctbss + 0x6c0");
__asm__(".globl t32_data_db5a0\n.set t32_data_db5a0, t32o_ctbss + 0x7b0");
__asm__(".globl t32_data_db690\n.set t32_data_db690, t32o_ctbss + 0x8a0");
__asm__(".globl t32_data_db710\n.set t32_data_db710, t32o_ctbss + 0x920");
__asm__(".globl t32_data_dbf10\n.set t32_data_dbf10, t32o_ctbss + 0x1120");
__asm__(".globl t32_data_dbf14\n.set t32_data_dbf14, t32o_ctbss + 0x1124");
__asm__(".globl t32_data_dbf28\n.set t32_data_dbf28, t32o_ctbss + 0x1138");
__asm__(".globl t32_data_dbf3c\n.set t32_data_dbf3c, t32o_ctbss + 0x114c");
__asm__(".globl t32_data_dbf64\n.set t32_data_dbf64, t32o_ctbss + 0x1174");
__asm__(".globl t32_data_dbf8c\n.set t32_data_dbf8c, t32o_ctbss + 0x119c");
__asm__(".globl t32_data_dbfb4\n.set t32_data_dbfb4, t32o_ctbss + 0x11c4");
__asm__(".globl t32_data_dbfdc\n.set t32_data_dbfdc, t32o_ctbss + 0x11ec");
__asm__(".globl t32_data_dc004\n.set t32_data_dc004, t32o_ctbss + 0x1214");
__asm__(".globl t32_data_dc02c\n.set t32_data_dc02c, t32o_ctbss + 0x123c");
u32 t32o_ct_stop, t32o_ct_stack[208], t32o_ct_reg[28];	/* set by the truncated ct_detect reference (trunc_ct3.sh) */
u32 t32o_lg_stop, t32o_lg_stack[116], t32o_lg_reg[28];	/* set by the truncated long_alogrithm reference (trunc_long.sh) */
u32 t32o_pr_stop, t32o_pr_reg[28];	/* set by the truncated process reference (trunc_process.sh) */

/* MXU context switches around ct_detect: nothing to do on the host */
u64 kernel_mxu_begin_partial(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 0; }
u64 kernel_mxu_end(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 0; }
/* MSCA printk format strings, only handed to isp_printf (weak: an oracle may define the real text) */
char t32_data_c9ed0[4] __attribute__((weak)) = "f";
char t32_data_c9ef8[4] __attribute__((weak)) = "f";
char t32_data_c9f58[4] __attribute__((weak)) = "f";
char t32_data_c9fa4[4] __attribute__((weak)) = "f";
char t32_data_c9fd0[4] __attribute__((weak)) = "f";
char t32_data_ca004[4] __attribute__((weak)) = "f";
char t32_data_ca030[4] __attribute__((weak)) = "f";
char t32_data_ca094[4] __attribute__((weak)) = "f";
char t32_data_ca0c4[4] __attribute__((weak)) = "f";
char t32_data_ca0f0[4] __attribute__((weak)) = "f";
char t32_data_ca10c[4] __attribute__((weak)) = "f";
char t32_data_ca154[4] __attribute__((weak)) = "f";
char t32_data_bfe44[4] __attribute__((weak)) = "f";
u8 t32_data_f3258[512] __attribute__((weak));	/* per channel mask block attribute (32 B each) */
u8 t32_data_e275c[8] __attribute__((weak));	/* per channel refresh flag */

/* shadow index <-> register map of the d03a0 = 1 runs (see t32o_reg_eq): must stay one to one */
int t32o_reg_eq(u32 vendor, u32 ours)
{
	static u32 idx2reg[1u << 20], reg2idx[9][1u << 20];	/* value + 1; 0 = not seen; the slot is (tag, channel, index) */
	u32 idx, sp;

	if (vendor == ours)
		return 1;
	if (vendor & 0x80000000u) {	/* tisp_reg_write in shadow mode: tag, channel, index */
		if ((vendor & 0xffffffu) >= (1u << 16))
			return 0;
		idx = (vendor & 0xffffu) | (vendor >> 24 & 7) << 16;
		sp = vendor >> 24 & 7;
	} else if (vendor < (1u << 16) && ours >= (1u << 16)) {
		/* a vendor function that hands the shadow index to system_reg_write (tisp_ae_roi_point, the AE trigger):
		 * a small bare address, learnt like an index in its own slot space */
		idx = vendor | 1u << 19;
		sp = 8;
	} else {
		return 0;
	}
	if (ours >= (1u << 20) || (idx2reg[idx] && idx2reg[idx] != ours + 1) || (reg2idx[sp][ours] && reg2idx[sp][ours] != idx + 1))
		return 0;
	idx2reg[idx] = ours + 1;
	reg2idx[sp][ours] = idx + 1;
	return 1;
}

u32 *t32o_shadow_arm(u32 words)
{
	u8 *blk = t32o_alloc(4 * 256 + 64);
	u32 *img = t32o_alloc(words * 4), i, p;

	for (i = 0; i < words; i++)
		img[i] = 0xa5a50000u ^ i;
	p = T32O_PTR(img);
	memcpy(blk + 200, &p, 4);	/* da844[0] -> block, block + (ch << 8) + 200 -> image of channel ch */
	t32_data_da844[0] = T32O_PTR(blk);
	return img;
}

/* decoy channels 1..3: section bases (dd0a4[1..3]) with patterned blobs that no call for channel 0 may touch */
static u8 *decoy_blob[3];
static u8 decoy_byte(unsigned int k, unsigned int i)
{
	return (u8)((i * 2654435761u >> 13) ^ (k * 0x5b) ^ (i >> 9));
}
void t32o_decoy_arm(void)
{
	unsigned int k, i;

	for (k = 0; k < 3; k++) {
		decoy_blob[k] = t32o_alloc(0x10000);
		for (i = 0; i < 0x10000; i++)
			decoy_blob[k][i] = decoy_byte(k, i);
		t32_data_dd0a4[k + 1] = T32O_PTR(decoy_blob[k]);
	}
}
int t32o_decoy_check(void)
{
	unsigned int k, i;

	for (k = 0; k < 3; k++)
		for (i = 0; i < 0x10000; i++)
			if (decoy_blob[k][i] != decoy_byte(k, i))
				return 1;
	return 0;
}

/* ---- op7-lib-rest (fix_point_*, log2, ae_lib, block leftovers) ---- */
/* printk format strings of fix_point_sub / _sub_32 / _sub_64 (c79e8) and fix_point_intp (bee70 function name,
 * c7a2c format): only handed to isp_printf */
char t32_data_c79e8[4] = "f", t32_data_bee70[4] = "f", t32_data_c7a2c[4] = "f";
/* tisp_api_get/set_bayer_type: function names (bf328, bf310) and format (c84c4), only handed to isp_printf */
char t32_data_bf328[4] = "f", t32_data_bf310[4] = "f", t32_data_c84c4[4] = "f";
/* lib_tisp_vic_get_par: VIC parameter words (.bss e3f30, e3f2c); lib_tisp_api_get_3a_info: AE debug trace
 * on / max per channel (.bss e1500, e14f0, 4 words each).  Weak: other oracles may define them too. */
u32 t32_data_e3f30[1] __attribute__((weak)), t32_data_e3f2c[1] __attribute__((weak));
u32 t32_data_e1500[4] __attribute__((weak)), t32_data_e14f0[4] __attribute__((weak));
/* lib_tisp_api_s_runningMode: t32_data_dc2b0 is byte 44 of the channel state t32_data_dc284 */
__asm__(".globl t32_data_dc2b0\n.set t32_data_dc2b0, t32_data_dc284 + 44");
