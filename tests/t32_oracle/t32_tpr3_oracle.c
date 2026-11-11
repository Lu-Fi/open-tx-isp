// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: t32_tisp_defog_print_struct of driver/t32/tx_isp_t32_tprint.h against the vendor
 * tisp_defog_print_struct (Matt Davis' recovered C, PR #15 @ 2e2e06bc).  Random defog parameter blocks of
 * channel 0..3 (t32_data_e2130[ch] + 12), mode word 0 / 1 / 2 / random.  Compared: the print stream, the
 * result and that the block is unchanged.  Exit 0 = all equal, 1 = first mismatch.
 */
#include "t32_show_rec.h"
#include "tx_isp_t32_tprint.h"

u64 tisp_defog_print_struct(u32, u32, u32, u32);
extern u32 t32_data_e2130[4];

#define BSZ	704

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 3000, c, prints = 0, i, k;
	static u8 b0[BSZ];

	for (c = 0; c < n; c++) {
		u32 r0, r1, ch = sr_rnd() % 4, mode = (c % 8 == 7) ? sr_rnd() : c % 8 == 6 ? 2 : c & 1;
		u8 *blk = 0;

		t32o_reset();
		for (i = 0; i < 4; i++) {
			u8 *x = t32o_alloc(64), *b = t32o_alloc(BSZ);

			sr_fill(b, BSZ);
			for (k = 0; k < 16; k++) {	/* the other context words: decoy blocks */
				u8 *d = t32o_alloc(BSZ);

				sr_fill(d, BSZ);
				((u32 *)x)[k] = T32O_PTR(d);
			}
			*(u32 *)(x + 12) = T32O_PTR(b);
			t32_data_e2130[i] = T32O_PTR(x);
			if (i == ch)
				blk = b;
		}
		memcpy(b0, blk, BSZ);
		sr_begin();
		r0 = (u32)t32o_call(SR_R(tisp_defog_print_struct), ch, mode, sr_rnd(), sr_rnd(), 0);
		sr_side = 1;
		r1 = (u32)t32_tisp_defog_print_struct(t32_data_e2130, ch, mode);
		if (sr_cmp("t32_tpr3_oracle", c, "tisp_defog_print_struct")) return 1;
		if (r0 != r1 || memcmp(b0, blk, BSZ)) {
			printf("t32_tpr3_oracle: case %u: ret %08x/%08x\n", c, r0, r1);
			return 1;
		}
		prints += sr_n[0];
	}
	printf("t32_tpr3_oracle: %u cases compared (%u prints), 0 mismatches\n", n, prints);
	return 0;
}
