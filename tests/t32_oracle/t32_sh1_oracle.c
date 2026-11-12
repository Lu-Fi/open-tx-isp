// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: t32_isp_csi_show and t32_tisp_module_version_debug of driver/t32/tx_isp_t32_show.h
 * against the vendor isp_csi_show / tisp_module_version_debug (Matt Davis' recovered C, PR #15 @ 2e2e06bc).
 * Random states: seq_file (random count), proc data / csi device NULL, error pointer or valid, CSI error words
 * zero or not; 0..4 channels whose tuning bases hold the driver's version word or a random one.  Compared: the
 * print stream (t32_show_rec.h), the result and the seq_file / state bytes after the call.
 * Exit 0 = all equal, 1 = first mismatch.
 */
#include "t32_show_rec.h"
#include "tx_isp_t32_show.h"

u64 isp_csi_show(u32, u32, u32, u32), tisp_module_version_debug(u32, u32, u32, u32);
u32 t32_data_dc608[1];


int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 3000, c, prints = 0;

	for (c = 0; c < n; c++) {
		u8 *m, *dev, *csi, *regs, m0[128], m1[128];
		u32 r0, r1, mode = sr_rnd() % 10, ch, i;

		/* ---- isp_csi_show ---- */
		t32o_reset();
		m = t32o_alloc(128); dev = t32o_alloc(512); csi = t32o_alloc(512); regs = t32o_alloc(64);
		sr_fill(m, 128); sr_fill(dev, 512); sr_fill(csi, 512); sr_fill(regs, 64);
		*(u32 *)(m + 12) = sr_rnd() % 4096;
		*(u32 *)(m + 72) = mode == 0 ? 0 : mode == 1 ? sr_errptr() : T32O_PTR(dev);
		*(u32 *)(dev + 280) = mode == 2 ? 0 : mode == 3 ? sr_errptr() : T32O_PTR(csi);
		*(u32 *)(csi + 244) = T32O_PTR(regs);
		sr_errpage();
		if (sr_rnd() % 3 == 0) *(u32 *)(regs + 0x20) = 0;
		if (sr_rnd() % 3 == 0) *(u32 *)(regs + 0x24) = 0;
		memcpy(m0, m, 128);
		sr_begin();
		r0 = (u32)t32o_call(SR_R(isp_csi_show), T32O_PTR(m), sr_rnd(), sr_rnd(), sr_rnd(), 0);
		memcpy(m1, m, 128); memcpy(m, m0, 128);
		sr_side = 1;
		r1 = (u32)t32_isp_csi_show(m);
		if (sr_cmp("t32_sh1_oracle", c, "isp_csi_show")) return 1;
		if (r0 != r1 || memcmp(m1, m, 128)) {
			printf("t32_sh1_oracle: case %u isp_csi_show: ret %08x/%08x, seq_file %s\n", c, r0, r1,
			       memcmp(m1, m, 128) ? "differs" : "equal");
			return 1;
		}
		prints += sr_n[0];

		/* ---- tisp_module_version_debug ---- */
		t32o_reset();
		m = t32o_alloc(128);
		sr_fill(m, 128);
		*(u32 *)(m + 12) = sr_rnd() % 4096;
		t32_data_dc608[0] = (c % 16 == 0) ? 0 : 1 + sr_rnd() % 4;
		for (ch = 0; ch < 4; ch++) {
			u8 *t = t32o_alloc(0xd200);

			t32_data_dd0a4[ch] = T32O_PTR(t);
			for (i = 0; i < sizeof(t32_modver) / sizeof(t32_modver[0]); i++) {
				u32 v = sr_rnd();

				if (sr_rnd() & 1)
					v = t32_modver[i].drv;
				else if (sr_rnd() % 4 == 0)
					v = t32_modver[i].drv ^ (1u << (sr_rnd() % 32));
				*(u32 *)(t + t32_modver[i].off) = v;
			}
		}
		memcpy(m0, m, 128);
		sr_begin();
		r0 = (u32)t32o_call(SR_R(tisp_module_version_debug), T32O_PTR(m), sr_rnd(), sr_rnd(), sr_rnd(), 0);
		memcpy(m1, m, 128); memcpy(m, m0, 128);
		sr_side = 1;
		r1 = (u32)t32_tisp_module_version_debug(m, t32_data_dd0a4, t32_data_dc608);
		if (sr_cmp("t32_sh1_oracle", c, "tisp_module_version_debug")) return 1;
		if (r0 != r1 || memcmp(m1, m, 128)) {
			printf("t32_sh1_oracle: case %u tisp_module_version_debug: ret %08x/%08x\n", c, r0, r1);
			return 1;
		}
		prints += sr_n[0];
	}
	printf("t32_sh1_oracle: %u cases x 2 functions compared (%u prints), 0 mismatches\n", n, prints);
	return 0;
}
