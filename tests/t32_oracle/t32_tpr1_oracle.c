// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: t32_tisp_mdns_reg_print and t32_tmo_adr_reg_params_print of driver/t32/tx_isp_t32_tprint.h
 * against the vendor tisp_mdns_reg_print / tmo_adr_reg_params_print (Matt Davis' recovered C, PR #15 @ 2e2e06bc,
 * printk renamed by fix_show.sh).  Random register values (shadow mode t32_data_d03a0 0 / 1, random channel),
 * random TMO parameter tables (channel 0..3 of t32_data_e35e0 / e35b0).  Compared: the print stream with every
 * tisp_reg_read (channel, register) in order, the result, the loop counter t32_data_e3514 and the tables.
 * Exit 0 = all equal, 1 = first mismatch.
 */
#include "t32_show_rec.h"
#include "tx_isp_t32_tprint.h"

u64 tisp_mdns_reg_print(u32, u32, u32, u32), tmo_adr_reg_params_print(u32, u32, u32, u32);
u32 t32_data_e35e0[4], t32_data_e35b0[4];
u16 t32_data_e3514[2] __attribute__((aligned(4)));

static u32 rr_val[8];
static unsigned int rr_k;
u64 tisp_reg_read(u32 a0, u32 a1, u32 a2, u32 a3)
{
	char t[32];

	(void)a2; (void)a3;
	snprintf(t, sizeof(t), "rd %u %08x", a0, a1);
	sr_put(SR_HOOK, a0, a1, 0, t);
	return rr_val[rr_k++ & 7];
}
static u32 o_rr(u32 ch, u32 reg) { return (u32)tisp_reg_read(ch, reg, 0, 0); }

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 3000, c, prints = 0, i;

	for (c = 0; c < n; c++) {
		u32 r0, r1, ch = (c & 7) == 0 ? sr_rnd() : sr_rnd() % 4;
		u8 *ttp[4], *asit[4], *tb, *ab, t0[10048], a0[800];
		u16 e0;

		/* ---- tisp_mdns_reg_print ---- */
		t32o_reset();
		for (i = 0; i < 8; i++)
			rr_val[i] = (i & 1) ? sr_rnd() : sr_rnd() & 0x9f9f9f9fu;
		t32_data_d03a0[0] = (c & 1) ? (u8)(1 + sr_rnd() % 255) : 0;
		sr_begin(); rr_k = 0;
		r0 = (u32)t32o_call(SR_R(tisp_mdns_reg_print), ch, sr_rnd(), sr_rnd(), sr_rnd(), 0);
		sr_side = 1; rr_k = 0;
		r1 = (u32)t32_tisp_mdns_reg_print(o_rr, t32_data_d03a0, ch);
		if (sr_cmp("t32_tpr1_oracle", c, "tisp_mdns_reg_print")) return 1;
		if (r0 != r1) { printf("t32_tpr1_oracle: case %u mdns: ret %08x/%08x\n", c, r0, r1); return 1; }
		prints += sr_n[0];

		/* ---- tmo_adr_reg_params_print ---- */
		ch &= 3;
		t32o_reset();
		for (i = 0; i < 4; i++) {
			u8 *p = t32o_alloc(8), *q = t32o_alloc(8);

			ttp[i] = t32o_alloc(10048); asit[i] = t32o_alloc(800);
			sr_fill(ttp[i], 10048); sr_fill(asit[i], 800);
			*(u32 *)p = T32O_PTR(ttp[i]); *(u32 *)q = T32O_PTR(asit[i]);
			t32_data_e35e0[i] = T32O_PTR(p); t32_data_e35b0[i] = T32O_PTR(q);
		}
		tb = ttp[ch]; ab = asit[ch];
		memcpy(t0, tb, sizeof(t0)); memcpy(a0, ab, sizeof(a0));
		t32_data_e3514[0] = (u16)sr_rnd(); t32_data_e3514[1] = (u16)sr_rnd();
		e0 = t32_data_e3514[0];
		sr_begin();
		r0 = (u32)t32o_call(SR_R(tmo_adr_reg_params_print), ch, sr_rnd(), sr_rnd(), sr_rnd(), 0);
		if (t32_data_e3514[0] != 4 || memcmp(t0, tb, sizeof(t0)) || memcmp(a0, ab, sizeof(a0))) {
			printf("t32_tpr1_oracle: case %u: vendor state %u\n", c, t32_data_e3514[0]);
			return 1;
		}
		t32_data_e3514[0] = e0;
		sr_side = 1;
		r1 = (u32)t32_tmo_adr_reg_params_print(t32_data_e35e0, t32_data_e35b0, ch, &t32_data_e3514[0]);
		if (sr_cmp("t32_tpr1_oracle", c, "tmo_adr_reg_params_print")) return 1;
		if (r0 != r1 || t32_data_e3514[0] != 4 || memcmp(t0, tb, sizeof(t0)) || memcmp(a0, ab, sizeof(a0))) {
			printf("t32_tpr1_oracle: case %u tmo: ret %08x/%08x idx %u\n", c, r0, r1, t32_data_e3514[0]);
			return 1;
		}
		prints += sr_n[0];
	}
	printf("t32_tpr1_oracle: %u cases x 2 functions compared (%u prints / reads), 0 mismatches\n", n, prints);
	return 0;
}
