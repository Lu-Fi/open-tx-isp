// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle for the lens shading (LSC): the vendor tisp_lsc_init chain
 * (Matt Davis' recovered C, PR #15 @ 2e2e06bc, built for the host with env.c) against
 * our driver/t32/tx_isp_t32_lsc.h.  WORK IN PROGRESS: first the vendor log is dumped.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_iq.h"

static void put32(u8 *p, u32 v)
{
	p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
}

int main(int argc, char **argv)
{
	static u8 bin[1u << 20];
	FILE *f = fopen(argc > 1 ? argv[1] : "", "rb");
	size_t bytes;
	u32 off, len = 0, p1, p2, n1, n2, i;
	u8 *base, *info, *linfo, *v1, *v2;

	if (!f)
		return 2;
	bytes = fread(bin, 1, sizeof(bin), f);
	fclose(f);
	off = t32_iq_section(bin, (u32)bytes, T32_IQ_SEC_LSC, &len);
	n1 = (bin[off + 0x74] * 0xd88U) & 0xffff;
	n2 = (bin[off + 0x75] * 0x3602U) & 0xffff;
	p1 = off + len;
	p2 = p1 + n1;
	printf("sec off %x len %u payload1 %u payload2 %u\n", off, len, n1, n2);
	t32o_reset();
	base = t32o_alloc(0x10000);
	info = t32o_alloc(64);
	linfo = t32o_alloc(64);
	v1 = t32o_alloc(n1 + 8);
	v2 = t32o_alloc(n2 + 8);
	memcpy(base + 26820, bin + off, 120);
	memcpy(v1, bin + p1, n1);
	memcpy(v2, bin + p2, n2);
	put32(linfo + 16, T32O_PTR(v1));
	put32(linfo + 20, T32O_PTR(v2));
	put32(info, 3840); put32(info + 4, 2160);
	{
		u8 *regsh = t32o_alloc(0x10000), *dbase = t32o_alloc(0x400);

		memset(regsh, 0, 0x10000);
		*(u32 *)(dbase + 204) = T32O_PTR(regsh);
		t32_data_da844[0] = T32O_PTR(dbase);
	}
	t32_data_dd0a4[0] = T32O_PTR(base);
	t32_data_dc274[0] = T32O_PTR(linfo);
	t32o_call((t32o_fn)(void (*)(void))tisp_lsc_init, 0, T32O_PTR(info), 0, 0, 0);
	printf("vendor writes %u\n", t32o_log.n);
	{
		unsigned int n24 = 0;

		for (i = 0; i < t32o_log.n; i++) {
			if (t32o_log.w[i].reg == 0x32024) {
				n24++;
				continue;
			}
			printf("%3u %05x %08x\n", i, t32o_log.w[i].reg, t32o_log.w[i].val);
		}
		printf("port data words %u\n", n24);
	}
	{
		u32 *rs = T32O_HOST(*(u32 *)((u8 *)T32O_HOST(t32_data_da844[0]) + 204)), *ls = rs + 0x200;
		unsigned int nz = 0;

		for (i = 0; i < 0x200; i++)
			if (rs[i])
				printf("regshadow[%u] = %08x\n", i, rs[i]);
		for (i = 0; i < 0x3000; i++)
			nz += ls[i] != 0;
		printf("lut shadow nonzero words %u; first: %08x %08x %08x %08x\n", nz, ls[0], ls[1], ls[2], ls[3]);
	}
	return 0;
}
