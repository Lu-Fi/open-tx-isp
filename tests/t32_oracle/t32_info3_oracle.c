// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: isp_info_sensor and isp_info_base of driver/t32/tx_isp_t32_info.h against the
 * recovered vendor C (Matt Davis, PR #15 @ 2e2e06bc).  Random device images and getter output
 * (t32_info_common.inc), every media bus code 0x4f00..0x5400 plus random codes for the Bayer name,
 * zero frame rates / VTS (the vendor divides without a check), bad proc data / no ISP device for
 * isp_info_sensor.  Compared: the seq_printf stream, the getter calls, the image bytes, the return
 * value.  Not compared (vendor faults / reads past its buffer, named in the header): a NULL ISP device
 * in isp_info_base, antiflicker counts >= 120.  T32O_BAYERDUMP=1 prints the vendor's code -> name map.
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

u64 isp_info_sensor(u32, u32, u32, u32), isp_info_base(u32, u32, u32, u32);

static u32 sensor_setup(u32 c, u32 code)
{
	u32 ch = (rnd() & 7) ? rnd() & 3 : rnd() % 7, s = ISP_OFF + 324 + 104 * ch, a, k;

	w32(ISP_OFF + 300 + 4 * ch, (rnd() & 3) ? 4 + (rnd() & 3) : rnd() & 7);
	w32(s + 8, code);
	k = rnd() & 7;		/* attribute = one of the 8; the private object */
	a = ATTR_OFF + 0x140 * k;
	w32(s + 52, IMGW(a));
	w32(s + 56, IMGW(SPRIV_OFF + 0x80 * (rnd() & 7)));
	w32(s + 68, (rnd() & 15) ? (((rnd() % 120) + 1) << 16) | ((rnd() % 4) + 1) : rnd() & 0x1ffff);
	if ((rnd() & 15) == 0)
		img[a + 278] = img[a + 279] = 0;	/* VTS 0 */
	switch (c % 23) {	/* bad proc data / no ISP device */
	case 0: w32(M_OFF + 72, 0); break;
	case 1: w32(M_OFF + 72, 0xfffff001u + rnd() % 0xfff); break;
	case 2: w32(PRIV_OFF + 280, 0u - 324 - 104 * ch); break;	/* sensor info address wraps to 0 */
	default: break;
	}
	return ch;
}

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 6000, c, f, lines = 0;
	unsigned int hit_step = 0, hit_osd = 0, codes = 0x4f00;

	if (getenv("T32O_BAYERDUMP")) {
		for (c = 0x4f00; c < 0x5400; c++) {
			build_image();
			w32(ISP_OFF + 300, 4);
			w32(ISP_OFF + 324 + 8, c);
			w32(ISP_OFF + 324 + 68, 0x10001);
			reset_side(0);
			t32o_call(R(isp_info_sensor), IMGW(M_OFF), 0, 0, 0, 0);
			printf("%04x %s\n", c, strchr(EV[0][5].t, '|') + 1);
		}
		return 0;
	}
	for (c = 0; c < n; c++) {
		for (f = 0; f < 2; f++) {
			u32 ch, rv, ro, i;

			case_seed = rnd();
			build_image();
			if (f == 0) {
				ch = sensor_setup(c, codes < 0x5400 ? codes++ : (rnd() & 1) ? 0x5000 + rnd() % 0x340 : rnd());
			} else {
				ch = (rnd() & 7) ? rnd() & 3 : rnd() & 7;
				w32(ISP_OFF + 300 + 4 * ch, (rnd() & 3) ? 4 + (rnd() & 3) : rnd() & 7);
			}
			memcpy(snap, img, IMG_SZ);
			reset_side(0);
			rv = (u32)t32o_call(R(f ? isp_info_base : isp_info_sensor), IMGW(M_OFF), ch, 0, 0, 0);
			memcpy(after0, img, IMG_SZ);
			memcpy(img, snap, IMG_SZ);
			reset_side(1);
			ro = (u32)(f ? t32_isp_info_base : t32_isp_info_sensor)(&ops, img + M_OFF, ch);
			if (cmp_logs("t32_info3_oracle", c))
				{ printf("  (%s, ch %u)\n", f ? "base" : "sensor", ch); return 1; }
			if (rv != ro || memcmp(img, after0, IMG_SZ)) {
				printf("t32_info3_oracle: case %u %s: ret %x / %x, image %s\n", c, f ? "base" : "sensor", rv, ro,
				       memcmp(img, after0, IMG_SZ) ? "differs" : "equal");
				return 1;
			}
			lines += nev[0];
			for (i = 0; i < nev[0]; i++) {
				hit_step += !strncmp(EV[0][i].t, "%d. ", 4);
				hit_osd += !strncmp(EV[0][i].t, "ISP OSD", 7);
			}
		}
	}
	printf("t32_info3_oracle: %u cases x 2 functions, %u calls compared (antiflicker steps %u, osd %u), 0 mismatches\n",
	       n, lines, hit_step, hit_osd);
	return 0;
}
