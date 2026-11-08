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
u32 t32_data_da844[4] __attribute__((aligned(16)));	/* register shadow (tisp_reg_write mode 1) */

/* top block (tisp_top_init / tisp_top_reg_cfg) */
u8 t32_data_e1460[8] __attribute__((aligned(4)));	/* per-channel "configured" flags */
u32 t32_data_e1464[4];		/* per-channel top enable word */
u32 t32_data_e1474[4];		/* per-pipeline-parity DE-IR flag */
u32 t32_data_e1484[4];		/* per-channel copy of the tuning base */
u8 t32_data_dc284[1808] __attribute__((aligned(4)));	/* 2 x 904 B channel state, +900 = pipeline count */

/* IQ bin parser (tisp_bin_data_parse), per-channel state, all .bss */
u8 t32_data_da850[704] __attribute__((aligned(4)));	/* 336 B per channel, +224 channel count, +232 14 B copy */
u8 t32_data_dd114[224] __attribute__((aligned(4)));	/* 112 B per channel: copy of the bin's section table */
u8 t32_data_dc1a4[56] __attribute__((aligned(4)));	/* 28 B per channel: largest payload sizes seen */
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
/* data.c:25533 (260 B) */
u32 t32_data_d6840[65] = { 0x00000000, 0x00900000, 0x01aa0120, 0x0281021d, 0x032c02da, 0x03bd0377, 0x043e0400, 0x04b3047a, 0x051e04e9, 0x05810550, 0x05de05b0, 0x0636060b, 0x06890660, 0x06d906b1, 0x072506ff, 0x076e074a, 0x07b40791, 0x07f807d6, 0x083a0819, 0x0879085a, 0x08b70898, 0x08f308d5, 0x092d0910, 0x0966094a, 0x099e0982, 0x09d409b9, 0x0a0909ef, 0x0a3d0a23, 0x0a700a57, 0x0aa20a89, 0x0ad30aba, 0x0b030aeb, 0x0b320b1a, 0x0b600b49, 0x0b8e0b77, 0x0bbb0ba4, 0x0be70bd1, 0x0c120bfc, 0x0c3d0c28, 0x0c670c52, 0x0c910c7c, 0x0cb90ca5, 0x0ce20cce, 0x0d0a0cf6, 0x0d310d1e, 0x0d580d45, 0x0d7e0d6b, 0x0da40d92, 0x0dca0db7, 0x0def0ddd, 0x0e140e01, 0x0e380e26, 0x0e5c0e4a, 0x0e7f0e6e, 0x0ea20e91, 0x0ec50eb4, 0x0ee70ed6, 0x0f0a0ef9, 0x0f2b0f1a, 0x0f4d0f3c, 0x0f6e0f5d, 0x0f8f0f7e, 0x0faf0f9f, 0x0fcf0fbf, 0x0fef0fdf };
/* data.c:25599 (8 B) */
u32 t32_data_d6948[2] = { 0x0000ff00, 0x01200090 };
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
u64 private_vmalloc(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	return a0 ? (u32)(uintptr_t)t32o_alloc(a0) : 0;
}

/* kernel allocators: vmalloc(0) and kmalloc(0) return NULL as in the kernel's callers expect an error */
u64 private_kmalloc(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	return a0 ? (u32)(uintptr_t)t32o_alloc(a0) : 0;
}

u64 private_kfree(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a1; (void)a2; (void)a3;
	return 0;
}

u64 private_vfree(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a1; (void)a2; (void)a3;
	return 0;
}

u64 private_spin_lock_init(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a1; (void)a2; (void)a3;
	return 0;
}

u64 __private_spin_lock_irqsave(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a1; (void)a2; (void)a3;
	return 0;	/* saved flags */
}

u64 private_spin_unlock_irqrestore(u32 a0, u32 a1, u32 a2, u32 a3)
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

u64 system_reg_read(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	if (a0 == 0x88)
		return t32o_reg88;	/* the Bayer/format register the DPC init tests */
	return 0x5a000000u | a0;	/* only the second pipeline reads back */
}

u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a1; (void)a2; (void)a3;
	t32o_log.printfs++;
	return 0;
}

u64 system_irq_func_set(u32 a0, u32 a1, u32 a2, u32 a3)
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
static u32 call_a[5];
static u64 call_ret;

static void trampoline(void)
{
	call_ret = call_fn(call_a[0], call_a[1], call_a[2], call_a[3], call_a[4]);
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
