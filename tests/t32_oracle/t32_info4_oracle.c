// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: isp_info_show of driver/t32/tx_isp_t32_info.h against the
 * recovered vendor C (Matt Davis, PR #15 @ 2e2e06bc).  Random device images (vinum around 4, the
 * channel state around 4, debug type 0..3, random item words), the isp_debug_info recorder returns
 * random values for item 0 and may flip a bit of the item word (the vendor reads it again before each
 * test), the completion wait times out at random.  Compared: the seq_printf stream, every
 * isp_debug_info / wait call with its arguments, the image bytes (the debug request word), the return
 * value.  Exit 0 = all equal, 1 = first mismatch.
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

u64 isp_info_show(u32, u32, u32, u32);

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 20000, c, lines = 0, i;
	unsigned int ndbg = 0, nwait = 0, nend = 0;

	for (c = 0; c < n; c++) {
		u32 rv, ro, it;

		case_seed = rnd();
		build_image();
		w32(ISP_OFF + 756, (rnd() & 7) ? rnd() & 3 : (rnd() & 1) ? 4 + (rnd() & 3) : rnd());
		w32(ISP_OFF + 744, (rnd() & 7) ? rnd() & 3 : rnd());
		it = rnd();
		w32(ISP_OFF + 748, (rnd() & 3) ? it & rnd() & rnd() : (rnd() & 1) ? it & 0xff : it);
		memcpy(snap, img, IMG_SZ);
		reset_side(0);
		rv = (u32)t32o_call(R(isp_info_show), IMGW(M_OFF), 0, 0, 0, 0);
		memcpy(after0, img, IMG_SZ);
		memcpy(img, snap, IMG_SZ);
		reset_side(1);
		ro = (u32)t32_isp_info_show(&ops, img + M_OFF);
		if (cmp_logs("t32_info4_oracle", c))
			return 1;
		if (rv != ro || memcmp(img, after0, IMG_SZ)) {
			printf("t32_info4_oracle: case %u: ret %x / %x, image %s\n", c, rv, ro,
			       memcmp(img, after0, IMG_SZ) ? "differs" : "equal");
			return 1;
		}
		lines += nev[0];
		for (i = 0; i < nev[0]; i++) {
			ndbg += EV[0][i].fn == 100 + F_DBG;
			nwait += EV[0][i].fn == 100 + F_WAIT;
			nend += EV[0][i].fn == 1 && strstr(EV[0][i].t, "INFO End") != NULL;
		}
	}
	printf("t32_info4_oracle: %u cases, %u calls compared (isp_debug_info %u, waits %u, complete %u), 0 mismatches\n",
	       n, lines, ndbg, nwait, nend);
	return 0;
}
