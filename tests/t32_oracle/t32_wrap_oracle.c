// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: the empty stub, one-call wrappers and small accessors of
 * tx_isp_t32_wrap.h against the vendor functions (Matt Davis' recovered C,
 * PR #15 @ 2e2e06bc).  The wrapped block functions are recording stubs here
 * (callee id, the four argument registers, a random result), the ours side
 * uses hooks that record the same way.  Compared per call: return value,
 * callee log (which callee, arguments, count), the AE/AWB run flags of every
 * channel (canaries around them), the LSC shadow read back.  Random channel
 * numbers 0..255 (masking), flags 0/1/other.  Exit 1 on the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_wrap.h"

u32 t32_data_e15b8[4] __attribute__((aligned(16), weak));	/* .bss: AE "algorithm running" word per channel */
extern u8 t32_data_e16c4[];					/* env.c: AWB 16 B per channel */

extern u64 tisp_msca_Shd_ctrl(u32, u32, u32, u32);
extern u64 tisp_ae_algo_deinit(u32, u32, u32, u32), tisp_awb_algo_deinit(u32, u32, u32, u32),
	   tisp_api_ae_get_reg_state(u32, u32, u32, u32), tisp_ae_face_get(u32, u32, u32, u32),
	   tisp_ae_face_set(u32, u32, u32, u32), tisp_awb_face_get(u32, u32, u32, u32),
	   tisp_awb_face_set(u32, u32, u32, u32), tisp_msca_channel_start(u32, u32, u32, u32),
	   tisp_msca_irq_frame_done(u32, u32, u32, u32), tisp_ae_algo_deinit_internal(u32, u32, u32, u32),
	   tisp_awb_algo_deinit_internal(u32, u32, u32, u32), tisp_day_or_night_g_ctrl(u32, u32, u32, u32),
	   tisp_single_lsc_reg_read(u32, u32, u32, u32), tisp_single_lsc_reg_lut_read(u32, u32, u32, u32);

static u32 rs = 0x6d2b79f5u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

/* callee log: one entry per call (id, a0..a3), shared shape for both sides */
struct clog { unsigned int n; u32 e[8][5]; };
static struct clog vlog, olog;
static u64 cret;	/* result every stub returns in this case */
static u64 rec(struct clog *l, u32 id, u32 a0, u32 a1, u32 a2, u32 a3)
{
	if (l->n < 8) {
		l->e[l->n][0] = id; l->e[l->n][1] = a0; l->e[l->n][2] = a1;
		l->e[l->n][3] = a2; l->e[l->n][4] = a3;
	}
	l->n++;
	return cret;
}

/* vendor callees */
u64 tisp_ae_get_reg_state(u32 a0, u32 a1, u32 a2, u32 a3) { return rec(&vlog, 4, a0, a1, a2, a3); }
u64 tisp_ae_api_get_faceae(u32 a0, u32 a1, u32 a2, u32 a3) { return rec(&vlog, 5, a0, a1, a2, a3); }
u64 tisp_ae_sepc_area(u32 a0, u32 a1, u32 a2, u32 a3) { return rec(&vlog, 6, a0, a1, a2, a3); }
u64 tisp_awb_api_get_face(u32 a0, u32 a1, u32 a2, u32 a3) { return rec(&vlog, 7, a0, a1, a2, a3); }
u64 tisp_awb_api_set_face(u32 a0, u32 a1, u32 a2, u32 a3) { return rec(&vlog, 8, a0, a1, a2, a3); }
u64 tisp_msca_chx_cfg_load(u32 a0, u32 a1, u32 a2, u32 a3) { return rec(&vlog, 9, a0, a1, a2, a3); }
u64 tisp_msca_cfg_shd(u32 a0, u32 a1, u32 a2, u32 a3) { return rec(&vlog, 10, a0, a1, a2, a3); }

/* ours: hooks; the deinit hooks run our accessor on the ours copy of the flags */
static u32 o_ae[4], o_awb[16];
static u64 h_aed(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	return (u32)t32_ae_algo_deinit_internal(o_ae, a0);
}
static u64 h_awbd(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	return (u32)t32_awb_algo_deinit_internal(o_awb, a0);
}
static u64 h_rs(u32 a0, u32 a1, u32 a2, u32 a3) { return rec(&olog, 4, a0, a1, a2, a3); }
static u64 h_fg(u32 a0, u32 a1, u32 a2, u32 a3) { return rec(&olog, 5, a0, a1, a2, a3); }
static u64 h_fs(u32 a0, u32 a1, u32 a2, u32 a3) { return rec(&olog, 6, a0, a1, a2, a3); }
static u64 h_wg(u32 a0, u32 a1, u32 a2, u32 a3) { return rec(&olog, 7, a0, a1, a2, a3); }
static u64 h_ws(u32 a0, u32 a1, u32 a2, u32 a3) { return rec(&olog, 8, a0, a1, a2, a3); }
static u64 h_cl(u32 a0, u32 a1, u32 a2, u32 a3) { return rec(&olog, 9, a0, a1, a2, a3); }
static u64 h_sh(u32 a0, u32 a1, u32 a2, u32 a3) { return rec(&olog, 10, a0, a1, a2, a3); }

static const struct t32_wrap_ops ops = {
	.ae_algo_deinit_internal = h_aed, .awb_algo_deinit_internal = h_awbd,
	.ae_get_reg_state = h_rs, .ae_api_get_faceae = h_fg, .ae_sepc_area = h_fs,
	.awb_api_get_face = h_wg, .awb_api_set_face = h_ws, .msca_chx_cfg_load = h_cl, .msca_cfg_shd = h_sh,
};

#define NF 15
static const char *const names[NF] = {
	"msca_Shd_ctrl", "ae_algo_deinit", "awb_algo_deinit", "api_ae_get_reg_state", "ae_face_get",
	"ae_face_set", "awb_face_get", "awb_face_set", "msca_channel_start", "msca_irq_frame_done",
	"ae_algo_deinit_internal", "awb_algo_deinit_internal", "day_or_night_g_ctrl",
	"single_lsc_reg_read", "single_lsc_reg_lut_read"
};

static u32 flag(void)
{
	u32 r = rnd() % 4;

	return r == 0 ? 0 : r == 1 ? 1 : r == 2 ? 0x101 : rnd();
}

#define CALL(f, a0, a1, a2, a3) t32o_call((t32o_fn)(void (*)(void))(f), a0, a1, a2, a3, 0)

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 20000;
	unsigned int c, k, i, cmp = 0;
	u32 *awb_v = (u32 *)t32_data_e16c4;

	for (c = 0; c < n; c++)
		for (k = 0; k < NF; k++) {
			u32 a0 = rnd(), a1 = rnd(), a2 = rnd(), a3 = rnd(), rv = 0, ro = 0;
			u32 *img = NULL, *root = NULL;

			if (c & 1)
				a0 &= 3;
			if (c % 3 == 0)
				a1 &= 0xff;
			cret = ((u64)rnd() << 32) | rnd();
			memset(&vlog, 0, sizeof(vlog));
			memset(&olog, 0, sizeof(olog));
			t32o_reset();
			for (i = 0; i < 4; i++)
				t32_data_e15b8[i] = o_ae[i] = flag();
			for (i = 0; i < 16; i++)
				awb_v[i] = o_awb[i] = (i & 3) ? rnd() : flag();
			for (i = 0; i < sizeof(t32_data_dc284); i++)
				t32_data_dc284[i] = (u8)rnd();
			if (k >= 13) {	/* LSC shadow: root with 4 x 256 B, image of 1024 words per channel */
				root = t32o_alloc(4 * 256);
				for (i = 0; i < 4; i++) {
					unsigned int j;

					img = t32o_alloc(1024 * 4);
					for (j = 0; j < 1024; j++)
						img[j] = rnd();
					root[i * 64 + 51] = T32O_PTR(img);
				}
				t32_data_da844[0] = T32O_PTR(root);
				a0 &= 3;
				a1 &= 0x1ff;
				a2 &= 0x1ff;
			}
			switch (k) {
			case 0: rv = (u32)CALL(tisp_msca_Shd_ctrl, a0, a1, a2, a3); ro = (u32)t32_msca_shd_ctrl(); break;
			case 1:
				a0 &= 3;
				rv = (u32)CALL(tisp_ae_algo_deinit, a0, a1, a2, a3);
				ro = (u32)t32_ae_algo_deinit(&ops, a0, a1, a2, a3);
				break;
			case 2:
				a0 &= 3;
				rv = (u32)CALL(tisp_awb_algo_deinit, a0, a1, a2, a3);
				ro = (u32)t32_awb_algo_deinit(&ops, a0, a1, a2, a3);
				break;
			case 3:
				rv = (u32)CALL(tisp_api_ae_get_reg_state, a0, a1, a2, a3);
				ro = (u32)t32_api_ae_get_reg_state(&ops, a0, a1, a2, a3);
				break;
			case 4: rv = (u32)CALL(tisp_ae_face_get, a0, a1, a2, a3); ro = (u32)t32_ae_face_get(&ops, a0, a1, a2, a3); break;
			case 5: rv = (u32)CALL(tisp_ae_face_set, a0, a1, a2, a3); ro = (u32)t32_ae_face_set(&ops, a0, a1, a2, a3); break;
			case 6: rv = (u32)CALL(tisp_awb_face_get, a0, a1, a2, a3); ro = (u32)t32_awb_face_get(&ops, a0, a1, a2, a3); break;
			case 7: rv = (u32)CALL(tisp_awb_face_set, a0, a1, a2, a3); ro = (u32)t32_awb_face_set(&ops, a0, a1, a2, a3); break;
			case 8:
				rv = (u32)CALL(tisp_msca_channel_start, a0, a1, a2, a3);
				ro = (u32)t32_msca_channel_start(&ops, a0, a2, a3);
				break;
			case 9:
				rv = (u32)CALL(tisp_msca_irq_frame_done, a0, a1, a2, a3);
				ro = (u32)t32_msca_irq_frame_done(&ops, a0, a2, a3);
				break;
			case 10:
				a0 &= 3;
				rv = (u32)CALL(tisp_ae_algo_deinit_internal, a0, a1, a2, a3);
				ro = (u32)t32_ae_algo_deinit_internal(o_ae, a0);
				break;
			case 11:
				a0 &= 3;
				rv = (u32)CALL(tisp_awb_algo_deinit_internal, a0, a1, a2, a3);
				ro = (u32)t32_awb_algo_deinit_internal(o_awb, a0);
				break;
			case 12:
				if (c & 2)
					a0 = (a0 & ~0xffu) | (a0 & 1);	/* channels 0/1 of the 2 x 904 B image, high bits set */
				else
					a0 &= 1;
				rv = (u32)CALL(tisp_day_or_night_g_ctrl, a0, a1, a2, a3);
				ro = t32_day_or_night_g_ctrl(t32_data_dc284, a0);
				break;
			case 13:
				rv = (u32)CALL(tisp_single_lsc_reg_read, a0, a1, a2, a3);
				ro = t32_single_lsc_reg_read((const u8 *)root, a0, a1);
				break;
			default:
				a3 = rnd() & 0x1ff;
				rv = (u32)CALL(tisp_single_lsc_reg_lut_read, a0, a1, a2, a3);
				ro = t32_single_lsc_reg_lut_read((const u8 *)root, a0, a1, a2);
				break;
			}
			if (rv != ro || vlog.n != olog.n || memcmp(&vlog, &olog, sizeof(vlog)) ||
			    memcmp(t32_data_e15b8, o_ae, sizeof(o_ae)) || memcmp(awb_v, o_awb, sizeof(o_awb))) {
				printf("t32_wrap_oracle: case %u %s (a0 0x%x a1 0x%x): return vendor 0x%x ours 0x%x, calls %u/%u, "
				       "log %s, AE flags %s, AWB flags %s\n", c, names[k], a0, a1, rv, ro, vlog.n, olog.n,
				       memcmp(&vlog, &olog, sizeof(vlog)) ? "differs" : "equal",
				       memcmp(t32_data_e15b8, o_ae, sizeof(o_ae)) ? "differ" : "equal",
				       memcmp(awb_v, o_awb, sizeof(o_awb)) ? "differ" : "equal");
				return 1;
			}
			cmp++;
		}
	printf("t32_wrap_oracle: %u calls compared (%d functions: return, callee log, AE/AWB flags, LSC shadow), 0 mismatches\n",
	       cmp, NF);
	return 0;
}
