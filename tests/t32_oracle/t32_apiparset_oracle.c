// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: tisp_api_param_array_set (tx_isp_t32_blkrest.h) against
 * the vendor function (Matt Davis' recovered C, PR #15 @ 2e2e06bc).  Per case
 * random id (0 .. 20, with high bits), channel 0 / 1 (with high bits), input
 * bytes (block / channel selectors mostly in range, sometimes any byte: the
 * vendor does not bound them) and callee results.  The vendor jump table
 * t32_data_c2f44 is defined here with its data.c values (lines 6132 .. 6148).
 * Compared: every callee call in order (kind, channel, value arguments, the
 * buffer bytes the vendor defines, store offset of the mask-out buffer), the
 * mask attribute / mask-out / MSCA stores (canary filled) and the return
 * value where the vendor defines it (the setter's result).  Exit 1 on the
 * first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_blkrest.h"

/* data.c:6132 .. 6148: code addresses of the vendor's switch */
u32 t32_data_c2f44[17] = { 0xb2c78, 0xb2c90, 0xb2cc8, 0xb2cf4, 0xb2dc0, 0xb2f34, 0xb31f0, 0xb2fc0, 0xb3008,
			   0xb31bc, 0xb3088, 0xb30bc, 0xb30f0, 0xb3124, 0xb3158, 0xb3198, 0xb3198 };
u8 t32_data_c2f88[4], t32_data_cb8c4[4];
u8 t32_data_e4000[32] __attribute__((aligned(4)));
#define MPB (2 * 2636 + 255 * 872 + 64)	/* channel 1 + the largest unbounded selector read */
u8 t32_data_f0928[MPB] __attribute__((aligned(4)));
extern u8 t32_data_f3258[512];
extern u64 tisp_api_param_array_set(u32, u32, u32, u32);

static u32 rs = 0x85ebca6bu;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

#define LOGN 4
#define LB 64
struct ent { u32 k, ch, x[6], off; u8 b[LB]; };
struct hlog { unsigned int n; struct ent e[LOGN]; };
static struct hlog vl, ol, *cur;
static u32 rv_seed;
/* defined bytes of the draw-block attribute (the rest is vendor stack garbage) */
static const u8 draw_def[T32_APS_DRAW_LEN] = { 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0,
					       0, 1, 1, 1, 1, 1, 1, 1, 0, 1 };
static const u8 *mo_base;
static u32 rec(u32 k, u32 ch, const u32 *x, unsigned nx, const u8 *b, unsigned nb)
{
	struct ent *e;
	unsigned i;

	if (cur->n < LOGN) {
		e = &cur->e[cur->n];
		memset(e, 0, sizeof(*e));
		e->k = k; e->ch = ch;
		for (i = 0; i < nx; i++) e->x[i] = x[i];
		if (b) {
			for (i = 0; i < nb; i++) e->b[i] = (k == 3 && !draw_def[i]) ? 0 : b[i];
			if (k == 2) e->off = (u32)(b - mo_base);
		}
	}
	cur->n++;
	return rv_seed ^ (k * 0x9e3779b9u) ^ cur->n;
}
#define VP(a) ((const u8 *)(unsigned long)(a))
/* vendor callees */
u64 tisp_day_or_night_s_ctrl(u32 a0, u32 a1, u32 a2, u32 a3) { u32 x[2] = { a1, a2 }; (void)a3; return rec(1, a0, x, 2, NULL, 0); }
u64 tisp_msca_api_set_mask_out_cfg(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; mo_base = t32_data_e4000; return rec(2, a0, NULL, 0, VP(a1), 24); }
u64 tisp_s_draw_block_attr(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return rec(3, a0, NULL, 0, VP(a1), T32_APS_DRAW_LEN); }
u64 tisp_s_osd_attr(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return rec(4, a0, NULL, 0, VP(a1), T32_APS_OSD_LEN); }
u64 tisp_s_osd_block_attr(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return rec(5, a0, NULL, 0, VP(a1), T32_APS_OSD_LEN); }
u64 tisp_msca_set_mirr_flip(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return rec(6, a0, NULL, 0, VP(a1), 1); }
u64 tisp_s_fcrop_control(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5) { u32 x[5] = { a1, a2, a3, a4, a5 }; return rec(7, a0, x, 5, NULL, 0); }
u64 tisp_msca_api_crop(u32 a0, u32 a1, u32 a2, u32 a3) { u32 x[1] = { a1 }; (void)a3; return rec(8, a0, x, 1, VP(a2), T32_APS_CHN_CROP_LEN); }
u64 tisp_msca_api_set_autozoom(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return rec(9, a0, NULL, 0, VP(a1), 60); }
u64 tisp_bcsh_api_set_brightness(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return rec(10, a0, NULL, 0, VP(a1), 1); }
u64 tisp_bcsh_api_set_contrast(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return rec(11, a0, NULL, 0, VP(a1), 1); }
u64 tisp_bcsh_api_set_saturation(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return rec(12, a0, NULL, 0, VP(a1), 1); }
u64 tisp_set_sharpness(u32 a0, u32 a1, u32 a2, u32 a3) { u32 x[1] = { a1 }; (void)a2; (void)a3; return rec(13, a0, x, 1, NULL, 0); }
u64 tisp_bcsh_api_set_hue(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return rec(14, a0, NULL, 0, VP(a1), 1); }
/* port hooks */
static u32 o_dn(u32 ch, u32 dn, u32 dn8) { u32 x[2] = { dn, dn8 }; return rec(1, ch, x, 2, NULL, 0); }
static u32 o_mo(u32 ch, u8 *c) { return rec(2, ch, NULL, 0, c, 24); }
static u32 o_draw(u32 ch, u8 *a) { return rec(3, ch, NULL, 0, a, T32_APS_DRAW_LEN); }
static u32 o_osd(u32 ch, u8 *a) { return rec(4, ch, NULL, 0, a, T32_APS_OSD_LEN); }
static u32 o_osdb(u32 ch, u8 *a) { return rec(5, ch, NULL, 0, a, T32_APS_OSD_LEN); }
static u32 o_mf(u32 ch, u8 *m) { return rec(6, ch, NULL, 0, m, 1); }
static u32 o_fc(u32 ch, u32 w0, u32 w1, u32 w2, u32 w3, u32 w4) { u32 x[5] = { w0, w1, w2, w3, w4 }; return rec(7, ch, x, 5, NULL, 0); }
static u32 o_crop(u32 ch, u32 sel, u8 *r) { u32 x[1] = { sel }; return rec(8, ch, x, 1, r, T32_APS_CHN_CROP_LEN); }
static u32 o_bcsh(enum t32_aps_bcsh w, u32 ch, u8 *v)
{
	static const u32 k[4] = { 10, 11, 12, 14 };
	return rec(k[w], ch, NULL, 0, v, 1);
}
static u32 o_sh(u32 ch, u32 v) { u32 x[1] = { v }; return rec(13, ch, x, 1, NULL, 0); }
static u32 o_az(u32 ch, u8 *b) { return rec(9, ch, NULL, 0, b, 60); }
static const struct t32_apis_ops ops = { o_dn, o_mo, o_draw, o_osd, o_osdb, o_mf, o_fc, o_crop, o_bcsh, o_sh, o_az };

#define INB 2700
static void fill(u8 *p, u32 n) { u32 i; for (i = 0; i < n; i++) p[i] = rnd(); }

int main(void)
{
	u8 *in = t32o_alloc(INB);
	static u8 mo[32], mp[MPB], ma[512];
	unsigned long n, N = 20000, per[18] = { 0 };
	u32 i;

	for (n = 0; n < N; n++) {
		struct t32_apis a;
		u32 ch = rnd() & 1, a0 = ch | (rnd() & 0xff00u), a2 = (rnd() & 3) ? rnd() % 21 : rnd(), id, rv, ro;

		if (rnd() & 1) a2 = (a2 & 0xffff) | (rnd() << 16);
		id = a2 & 0xffff;
		rv_seed = rnd();
		fill(in, INB);
		if (rnd() & 7) { in[T32_APS_CHN_SEL] %= 3; in[T32_APS_BLK_SEL] &= 7; }
		fill(t32_data_e4000, 32); memcpy(mo, t32_data_e4000, 32);
		fill(t32_data_f0928, MPB); memcpy(mp, t32_data_f0928, MPB);
		fill(ma, 512); memcpy(t32_data_f3258, ma, 512);

		memset(&ol, 0, sizeof(ol)); cur = &ol;
		mo_base = mo;
		a.ops = &ops; a.mask_attr = ma; a.mask_out = mo; a.msca_par = mp;
		ro = t32_api_param_array_set(&a, a0, in, a2);

		t32o_reset();
		memset(&vl, 0, sizeof(vl)); cur = &vl;
		rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_api_param_array_set, a0, T32O_PTR(in), a2, rnd(), 0);

		if ((id < 17 && id != 0 && id != 6 && id != 15 && id != 16 && rv != ro) || vl.n != ol.n ||
		    memcmp(vl.e, ol.e, sizeof(vl.e)) || memcmp(t32_data_f3258, ma, 512) || memcmp(t32_data_e4000, mo, 32) ||
		    memcmp(t32_data_f0928, mp, MPB)) {
			printf("t32_apiparset_oracle: FAIL case %lu id %#x ch %#x: ret %#x/%#x calls %u/%u\n", n, a2, a0, rv, ro,
			       vl.n, ol.n);
			for (i = 0; i < LOGN && i < vl.n; i++)
				if (memcmp(&vl.e[i], &ol.e[i], sizeof(vl.e[i]))) {
					unsigned j;
					printf("  call %u: k %u/%u ch %u/%u x0 %#x/%#x off %u/%u\n", i, vl.e[i].k, ol.e[i].k, vl.e[i].ch,
					       ol.e[i].ch, vl.e[i].x[0], ol.e[i].x[0], vl.e[i].off, ol.e[i].off);
					for (j = 0; j < LB; j++) if (vl.e[i].b[j] != ol.e[i].b[j]) { printf("  b[%u] %#x/%#x\n", j, vl.e[i].b[j], ol.e[i].b[j]); break; }
					break;
				}
			return 1;
		}
		per[id < 17 ? id : 17]++;
	}
	printf("t32_apiparset_oracle: PASS %lu cases (", N);
	for (i = 0; i < 18; i++) printf("%s%lu", i ? " " : "", per[i]);
	printf(" per id, last = unknown)\n");
	return 0;
}
