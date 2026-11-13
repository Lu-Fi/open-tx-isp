// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: isp_info_system, isp_info_gib, isp_info_awb, isp_info_bin and isp_info_err of
 * driver/t32/tx_isp_t32_info.h against the recovered vendor C (Matt Davis, PR #15 @ 2e2e06bc).
 * Random device images and getter output (t32_info_common.inc); compared: the seq_printf stream
 * (format text and printed line), the getter calls with their scalar arguments, the image bytes
 * after the call and the return value.  Exit 0 = all equal, 1 = first mismatch.
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

u64 isp_info_system(u32, u32, u32, u32), isp_info_gib(u32, u32, u32, u32), isp_info_awb(u32, u32, u32, u32);
u64 isp_info_bin(u32, u32, u32, u32), isp_info_err(u32, u32, u32, u32);
typedef int (*ours_fn)(const struct t32_info_ops *, void *, u32);
static const struct { const char *name; u64 (*ref)(u32, u32, u32, u32); ours_fn ours; } fns[] = {
	{ "system", isp_info_system, t32_isp_info_system },
	{ "gib", isp_info_gib, t32_isp_info_gib },
	{ "awb", isp_info_awb, t32_isp_info_awb },
	{ "bin", isp_info_bin, t32_isp_info_bin },
	{ "err", isp_info_err, t32_isp_info_err },
};

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 4000, c, f, lines = 0;
	unsigned int hit_cl = 0, hit_frm = 0, hit_err = 0;

	for (c = 0; c < n; c++) {
		for (f = 0; f < sizeof(fns) / sizeof(fns[0]); f++) {
			u32 ch, rv, ro, i;

			case_seed = rnd();
			build_image();
			ch = (rnd() & 3) ? rnd() & 3 : (rnd() & 1) ? rnd() & 7 : rnd();
			if (f == 0) {	/* system: sensor count, cache mode, frame timing state */
				w32(ISP_OFF + 1196, (rnd() & 7) ? rnd() % 9 : 0);
				w32(ISP_OFF + 1200, rnd() % 3);
				img[ISP_OFF + 945] = (rnd() & 1) ? 4 : (u8)rnd();
				ch = rnd();
			}
			if (f == 4) {
				ch = rnd() & 3;
				for (i = 0; i < 3; i++)
					w32(ISP_OFF + 896 + 16 * i + 4 * ch, (rnd() & 1) ? 0 : rnd());
			}
			memcpy(snap, img, IMG_SZ);
			reset_side(0);
			rv = (u32)t32o_call(R(fns[f].ref), IMGW(M_OFF), ch, 0, 0, 0);
			memcpy(after0, img, IMG_SZ);
			memcpy(img, snap, IMG_SZ);
			reset_side(1);
			ro = (u32)fns[f].ours(&ops, img + M_OFF, ch);
			if (cmp_logs("t32_info1_oracle", c))
				{ printf("  (%s, ch %u)\n", fns[f].name, ch); return 1; }
			if (rv != ro || memcmp(img, after0, IMG_SZ)) {
				printf("t32_info1_oracle: case %u %s: ret %x / %x, image %s\n", c, fns[f].name, rv, ro,
				       memcmp(img, after0, IMG_SZ) ? "differs" : "equal");
				return 1;
			}
			lines += nev[0];
			for (i = 0; i < nev[0]; i++) {
				hit_cl += !strncmp(EV[0][i].t, "ISP Cache", 9);
				hit_frm += !strncmp(EV[0][i].t, "ISP ==>>", 8);
				hit_err += !strncmp(EV[0][i].t, "isp%d debug b", 13);
			}
		}
	}
	printf("t32_info1_oracle: %u cases x 5 functions, %u calls compared (cache line %u, frame timing %u, breakfrm %u), 0 mismatches\n",
	       n, lines, hit_cl, hit_frm, hit_err);
	return 0;
}
