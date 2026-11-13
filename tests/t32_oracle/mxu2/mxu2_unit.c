// SPDX-License-Identifier: GPL-2.0
/*
 * Unit tests of the MXU2 host model (t32_mxu2.h): hand-computed results from the
 * pseudo-code of the Ingenic MXU2 programming manual, at least one per mnemonic family
 * the T32 module uses, with the edge cases (saturation, rounding, sign) the manual spells out.
 */
#include <stdio.h>
#include "t32_mxu2.h"

static unsigned bad, n;
#define EQ(what, got, want) do { n++; if ((got) != (want)) { bad++; printf("FAIL %s: got %llx want %llx\n", what, (unsigned long long)(got), (unsigned long long)(want)); } } while (0)

static void setw(mx_v *v, uint32_t a, uint32_t b, uint32_t c, uint32_t d) { v->w[0] = a; v->w[1] = b; v->w[2] = c; v->w[3] = d; }

int main(void)
{
	mx_v vr[32];
	int i;

	memset(vr, 0, sizeof(vr));
	/* ADDW / SUBW wrap, MULW low word, MADDW accumulates */
	setw(&vr[1], 0xffffffffu, 5, 0x80000000u, 7);
	setw(&vr[2], 2, 3, 0x80000000u, 9);
	mx_3r(vr, MX_ADD, 4, 3, 1, 2);
	EQ("ADDW0", vr[3].w[0], 1u); EQ("ADDW1", vr[3].w[1], 8u); EQ("ADDW2", vr[3].w[2], 0u);
	mx_3r(vr, MX_SUB, 4, 3, 1, 2);
	EQ("SUBW0", vr[3].w[0], 0xfffffffdu); EQ("SUBW3", vr[3].w[3], 0xfffffffeu);
	mx_3r(vr, MX_MUL, 4, 3, 1, 2);
	EQ("MULW0", vr[3].w[0], 0xfffffffeu); EQ("MULW3", vr[3].w[3], 63u);
	setw(&vr[3], 100, 100, 100, 100);
	mx_3r(vr, MX_MADD, 4, 3, 1, 2);
	EQ("MADDW1", vr[3].w[1], 115u);
	/* halfword add with wrap */
	for (i = 0; i < 8; i++) { vr[1].h[i] = 0xfff0; vr[2].h[i] = 0x20; }
	mx_3r(vr, MX_ADD, 2, 3, 1, 2);
	EQ("ADDH", vr[3].h[5], 0x10u);
	/* saturating adds: signed (ADDSSW) and unsigned (ADDUUH), subtract (SUBSSW, SUBUUH, SUBUUB) */
	setw(&vr[1], 0x7fffffffu, 0x80000000u, 1, 0); setw(&vr[2], 1, 0xffffffffu, 2, 0);
	mx_3r(vr, MX_ADDSS, 4, 3, 1, 2);
	EQ("ADDSSW max", vr[3].w[0], 0x7fffffffu); EQ("ADDSSW min", vr[3].w[1], 0x80000000u); EQ("ADDSSW mid", vr[3].w[2], 3u);
	mx_3r(vr, MX_SUBSS, 4, 3, 2, 1);
	EQ("SUBSSW min", vr[3].w[0], 0x80000002u); /* 1 - 0x7fffffff */
	for (i = 0; i < 8; i++) { vr[1].h[i] = 0xfffe; vr[2].h[i] = 5; }
	mx_3r(vr, MX_ADDUU, 2, 3, 1, 2);
	EQ("ADDUUH sat", vr[3].h[0], 0xffffu);
	mx_3r(vr, MX_SUBUU, 2, 3, 2, 1);
	EQ("SUBUUH floor", vr[3].h[0], 0u);
	for (i = 0; i < 16; i++) { vr[1].b[i] = 100; vr[2].b[i] = 200; }
	mx_3r(vr, MX_SUBUU, 1, 3, 1, 2);
	EQ("SUBUUB floor", vr[3].b[0], 0u);
	/* absolute differences */
	setw(&vr[1], 3, 0xfffffffeu, 10, 0); setw(&vr[2], 7, 5, 10, 0x80000000u);
	mx_3r(vr, MX_SUBUA, 4, 3, 1, 2);
	EQ("SUBUAW", vr[3].w[0], 4u); EQ("SUBUAW big", vr[3].w[1], 0xfffffff9u);
	for (i = 0; i < 8; i++) { vr[1].h[i] = 0xfff0; vr[2].h[i] = 0x10; }
	mx_3r(vr, MX_SUBSA, 2, 3, 1, 2);
	EQ("SUBSAH", vr[3].h[0], 0x20u);	/* |-16 - 16| */
	/* compares */
	setw(&vr[1], 0xffffffffu, 1, 5, 9); setw(&vr[2], 1, 1, 6, 9);
	mx_3r(vr, MX_CLTS, 4, 3, 1, 2);
	EQ("CLTSW signed", vr[3].w[0], 0xffffffffu); EQ("CLTSW eq", vr[3].w[1], 0u); EQ("CLTSW lt", vr[3].w[2], 0xffffffffu);
	mx_3r(vr, MX_CLTU, 4, 3, 1, 2);
	EQ("CLTUW unsigned", vr[3].w[0], 0u);
	mx_3r(vr, MX_CLEU, 4, 3, 1, 2);
	EQ("CLEUW eq", vr[3].w[3], 0xffffffffu); EQ("CLEUW gt", vr[3].w[0], 0u);
	for (i = 0; i < 16; i++) { vr[1].b[i] = i; vr[2].b[i] = 7; }
	mx_3r(vr, MX_CEQ, 1, 3, 1, 2);
	EQ("CEQB", vr[3].b[7], 0xffu); EQ("CEQB ne", vr[3].b[6], 0u);
	setw(&vr[1], 0, 0x80000000u, 3, 0);
	mx_2rz(vr, MX_CLTZ, 4, 3, 1); EQ("CLTZW", vr[3].w[1], 0xffffffffu); EQ("CLTZW pos", vr[3].w[2], 0u);
	for (i = 0; i < 8; i++) vr[1].h[i] = i == 2 ? 0 : 1;
	mx_2rz(vr, MX_CEQZ, 2, 3, 1); EQ("CEQZH", vr[3].h[2], 0xffffu);
	mx_2rz(vr, MX_CNEZ, 2, 3, 1); EQ("CNEZH", vr[3].h[2], 0u); EQ("CNEZH2", vr[3].h[0], 0xffffu);
	/* unsigned division */
	setw(&vr[1], 100, 7, 0xffffffffu, 9); setw(&vr[2], 7, 7, 2, 4);
	mx_3r(vr, MX_DIVU, 4, 3, 1, 2);
	EQ("DIVUW", vr[3].w[0], 14u); EQ("DIVUW big", vr[3].w[2], 0x7fffffffu);
	/* register shifts: counts from vrt, modulo the element width; SRLR adds the last shifted-out bit */
	setw(&vr[1], 0x80000001u, 0x80000001u, 0xf, 0xf); setw(&vr[2], 1, 33, 1, 2);
	mx_3r(vr, MX_SLL, 4, 3, 1, 2); EQ("SLLW", vr[3].w[0], 2u); EQ("SLLW mod", vr[3].w[1], 2u);
	mx_3r(vr, MX_SRL, 4, 3, 1, 2); EQ("SRLW", vr[3].w[0], 0x40000000u);
	mx_3r(vr, MX_SRA, 4, 3, 1, 2); EQ("SRAW", vr[3].w[0], 0xc0000000u);
	mx_3r(vr, MX_SRLR, 4, 3, 1, 2); EQ("SRLRW round", vr[3].w[0], 0x40000001u); EQ("SRLRW 2", vr[3].w[3], 4u);
	/* immediate shifts */
	setw(&vr[1], 0x80000003u, 0xfffffff0u, 5, 6);
	mx_shi(vr, MX_SLLI, 4, 3, 1, 4); EQ("SLLIW", vr[3].w[0], 0x30u);
	mx_shi(vr, MX_SRLI, 4, 3, 1, 4); EQ("SRLIW", vr[3].w[0], 0x08000000u);
	mx_shi(vr, MX_SRAI, 4, 3, 1, 4); EQ("SRAIW", vr[3].w[0], 0xf8000000u); EQ("SRAIW neg", vr[3].w[1], 0xffffffffu);
	mx_shi(vr, MX_SRARI, 4, 3, 1, 1); EQ("SRARIW round", vr[3].w[2], 3u); /* 5 >> 1 = 2, +1 */
	for (i = 0; i < 8; i++) vr[1].h[i] = 0x8003;
	mx_shi(vr, MX_SRARI, 2, 3, 1, 2); EQ("SRARIH", vr[3].h[0], 0xe001u); /* 0xe000 + bit1 (1) */
	mx_shi(vr, MX_SRLI, 2, 3, 1, 18); EQ("SRLIH mod", vr[3].h[0], 0x2000u); /* count 18 & 15 = 2 */
	/* logic and select */
	setw(&vr[1], 0xff00ff00u, 1, 2, 3); setw(&vr[2], 0x0ff00ff0u, 1, 1, 1);
	mx_andv(vr, 3, 1, 2); EQ("ANDV", vr[3].w[0], 0x0f000f00u);
	mx_orv(vr, 3, 1, 2); EQ("ORV", vr[3].w[0], 0xfff0fff0u);
	setw(&vr[4], 0xffff0000u, 0, 0xffffffffu, 0);
	mx_bselv(vr, 3, 1, 2, 4); EQ("BSELV hi", vr[3].w[0], 0xff00ff00u & 0 ? 0 : (0xffff0000u & 0x0ff00ff0u) | (0x0000ffffu & 0xff00ff00u));
	EQ("BSELV all", vr[3].w[2], 1u); EQ("BSELV none", vr[3].w[1], 1u);
	/* shuffle: interleaved pairs {vrs byte, vrt byte}, selector bit 7 zeroes */
	for (i = 0; i < 16; i++) { vr[1].b[i] = 0x10 + i; vr[2].b[i] = 0x20 + i; vr[4].b[i] = (uint8_t)i; }
	vr[4].b[0] = 0; vr[4].b[1] = 1; vr[4].b[2] = 5; vr[4].b[3] = 0x80; vr[4].b[4] = 31;
	mx_shufv(vr, 3, 1, 2, 4);
	EQ("SHUFV 0 = vrt[0]", vr[3].b[0], 0x20u); EQ("SHUFV 1 = vrs[0]", vr[3].b[1], 0x10u);
	EQ("SHUFV 5 = vrs[2]", vr[3].b[2], 0x12u); EQ("SHUFV zero", vr[3].b[3], 0u); EQ("SHUFV 31 = vrs[15]", vr[3].b[4], 0x1fu);
	/* element moves */
	setw(&vr[1], 11, 22, 33, 44);
	mx_repi(vr, 4, 3, 1, 2); EQ("REPIW", vr[3].w[0], 33u); EQ("REPIW3", vr[3].w[3], 33u);
	vr[1].d[0] = 0x1122334455667788ULL; vr[1].d[1] = 5;
	mx_repi(vr, 8, 3, 1, 1); EQ("REPID", vr[3].d[0], 5u); EQ("REPID1", vr[3].d[1], 5u);
	setw(&vr[3], 1, 2, 3, 4); setw(&vr[1], 99, 0, 0, 0);
	mx_insfmxu(vr, 4, 3, 1, 3); EQ("INSFMXUW", vr[3].w[3], 99u); EQ("INSFMXUW keep", vr[3].w[2], 3u);
	vr[3].d[0] = 1; vr[3].d[1] = 2; vr[1].d[0] = 77;
	mx_insfmxu(vr, 8, 3, 1, 1); EQ("INSFMXUD", vr[3].d[1], 77u); EQ("INSFMXUD keep", vr[3].d[0], 1u);
	mx_mfcpu(vr, 4, 3, 0xdeadbeefu); EQ("MFCPUW", vr[3].w[2], 0xdeadbeefu);
	mx_mfcpu(vr, 2, 3, 0x12345u); EQ("MFCPUH", vr[3].h[7], 0x2345u);
	setw(&vr[1], 0, 0x80000000u, 0, 0); vr[1].h[6] = 0x8001;
	EQ("MTCPUSW", mx_mtcpu(vr, 4, 1, 1, 1), 0x80000000u);
	EQ("MTCPUSH", mx_mtcpu(vr, 2, 1, 6, 1), 0xffff8001u);
	EQ("MTCPUUH", mx_mtcpu(vr, 2, 1, 6, 0), 0x8001u);
	EQ("MTCPUUW", mx_mtcpu(vr, 4, 1, 1, 0), 0x80000000u);
	/* LI: 15-bit immediate, sign extended to the element (byte: low 8 bits) */
	mx_li(vr, 4, 3, 0x7fff); EQ("LIW -1", vr[3].w[1], 0xffffffffu);
	mx_li(vr, 4, 3, 0x1234); EQ("LIW", vr[3].w[0], 0x1234u);
	mx_li(vr, 2, 3, 0x4000); EQ("LIH sign", vr[3].h[0], 0xc000u);
	mx_li(vr, 1, 3, 0x1ff); EQ("LIB", vr[3].b[9], 0xffu);
	printf("mxu2_unit: %u checks, %u failures\n", n, bad);
	return bad != 0;
}
