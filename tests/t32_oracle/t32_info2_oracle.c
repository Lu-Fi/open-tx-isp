// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: isp_info_ae of driver/t32/tx_isp_t32_info.h against the
 * recovered vendor C (Matt Davis, PR #15 @ 2e2e06bc).  Random getter output (t32_info_common.inc:
 * the exposure info, scene attributes with the HLC / BLC mode words and strength bytes, the EV / AT /
 * exposure lists with their enable words, BV, stability).  Compared: the seq_printf stream, the getter
 * calls with their channel arguments (full word / low byte), the image bytes, the return value.
 * Exit 0 = all equal, 1 = first mismatch.
 */
#include "t32_oracle_env.h"
#include "tx_isp_t32_info.h"
#include "t32_info_common.inc"

static const struct t32_info_ops ops = {
	.seq_printf = o_seq, .gib_get_blc = o_gib, .awb_global_statis = o_awbst, .awb_attr = o_awbattr,
	.ae_exprinfo = o_aeexpr, .ae_scence = o_aesc, .ae_bv = o_bv, .ae_at_list = o_at, .ae_ev_list = o_ev,
	.ae_explist = o_expl, .ae_stable = o_stable, .bin_path = o_bin, .day_or_night = o_dn, .get_tuning = o_tun,
	.hv_flip = o_flip, .antiflick = o_af, .antiflicker_step = o_step, .msca_osd_info = o_osd,
	.isp_memopt = o_memopt, .isp_cache_line = o_cl, .debug_info = o_dbg, .wait_completion_timeout = o_wait,
};

u64 isp_info_ae(u32, u32, u32, u32);

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 6000, c, lines = 0, i;
	unsigned int hit[6] = { 0 };
	static const char *const tag[6] = { "AeHLCEn : enable", "AeBLCEn : enable", "AeHLCEn : disable",
					    "AeExpoList - us", "AeExpoList : Disable", "Disable" };

	for (c = 0; c < n; c++) {
		u32 ch = (rnd() & 1) ? rnd() & 3 : rnd(), rv, ro;

		case_seed = rnd();
		build_image();
		memcpy(snap, img, IMG_SZ);
		reset_side(0);
		rv = (u32)t32o_call(R(isp_info_ae), IMGW(M_OFF), ch, 0, 0, 0);
		memcpy(after0, img, IMG_SZ);
		memcpy(img, snap, IMG_SZ);
		reset_side(1);
		ro = (u32)t32_isp_info_ae(&ops, img + M_OFF, ch);
		if (cmp_logs("t32_info2_oracle", c))
			{ printf("  (ch %x)\n", ch); return 1; }
		if (rv != ro || memcmp(img, after0, IMG_SZ)) {
			printf("t32_info2_oracle: case %u: ret %x / %x, image %s\n", c, rv, ro,
			       memcmp(img, after0, IMG_SZ) ? "differs" : "equal");
			return 1;
		}
		lines += nev[0];
		for (i = 0; i < nev[0]; i++) {
			unsigned int k;
			const char *t = EV[0][i].fn == 1 ? strchr(EV[0][i].t, '|') + 1 : "";

			for (k = 0; k < 6; k++)
				hit[k] += !strncmp(t, tag[k], strlen(tag[k]));
		}
	}
	printf("t32_info2_oracle: %u cases, %u calls compared (HLC on %u, BLC on %u, HLC off %u, expo lists %u / off %u, list off %u), 0 mismatches\n",
	       n, lines, hit[0], hit[1], hit[2], hit[3], hit[4], hit[5]);
	return 0;
}
