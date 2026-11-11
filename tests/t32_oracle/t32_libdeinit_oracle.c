// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our channel deinit (driver/t32/tx_isp_t32_libdeinit.h: lib_tisp_deinit) against Matt Davis'
 * mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc).
 *
 * Compared bit-exact: every callee call in order (block deinit with channel, rcfg deinit, core tuning deinit,
 * every vfree with its pointer), all touched data of all 4 channels (f0918, e4018, dd0b4 parameter buffers,
 * dd0a4, dc274 and the LUT info words, dc1a4 sizes, dc060 running flags; random start values).
 * Inputs: channel 0..3 with random upper bits, every pointer 0 or set (random), LUT info 0 or set.
 *
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_libdeinit.h"

u64 lib_tisp_deinit(u32, u32, u32, u32);

#define F(fn)	((t32o_fn)(void (*)(void))(fn))
#define ID_RCFG		100
#define ID_CORE		101
#define ID_VFREE	102

u32 t32_data_dd0b4[24], t32_data_f0918[4];
u8 t32_data_e4018[4], t32_data_dc060[4];

static u32 rs = 0x1f7b3a95u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

struct call { u32 id, a0; };
#define NC 64
static struct call vc[NC], oc[NC];
static unsigned int vn, on;
static u8 *lut_v[4];
static struct t32_lut_info lut_o[4];

static void rec(struct call *t, unsigned int *n, u32 id, u32 a0)
{
	if (*n < NC)
		t[*n] = (struct call){ id, a0 };
	(*n)++;
}

#define DI(name, blk) u64 name(u32 a0, u32 a1, u32 a2, u32 a3) \
	{ (void)a1; (void)a2; (void)a3; rec(vc, &vn, blk, a0); return rnd(); }
DI(tisp_top_deinit, T32_DI_TOP) DI(tisp_ae_deinit, T32_DI_AE) DI(tisp_awb_deinit, T32_DI_AWB)
DI(tisp_gib_deinit, T32_DI_GIB) DI(tisp_lsc_deinit, T32_DI_LSC) DI(tisp_tmo_deinit, T32_DI_TMO)
DI(tisp_defog_deinit, T32_DI_DEFOG) DI(tisp_dmsc_deinit, T32_DI_DMSC) DI(tisp_gamma_deinit, T32_DI_GAMMA)
DI(tisp_mdns_deinit, T32_DI_MDNS) DI(tisp_af_deinit, T32_DI_AF) DI(tisp_ydns_deinit, T32_DI_YDNS)
DI(tisp_cdns_deinit, T32_DI_CDNS) DI(tisp_bcsh_deinit, T32_DI_BCSH) DI(tisp_ysp_deinit, T32_DI_YSP)
DI(tisp_sdns_deinit, T32_DI_SDNS) DI(tisp_tstp_deinit, T32_DI_TSTP) DI(tisp_clm_deinit, T32_DI_CLM)
DI(tisp_ccm_deinit, T32_DI_CCM) DI(tisp_lce_deinit, T32_DI_LCE) DI(tisp_csc_deinit, T32_DI_CSC)
DI(tisp_csccr_deinit, T32_DI_CSCCR) DI(tisp_dpc_deinit, T32_DI_DPC) DI(tisp_msca_deinit, T32_DI_MSCA)
DI(tisp_rdns_deinit, T32_DI_RDNS) DI(tisp_decmp_deinit, T32_DI_DECMP) DI(tisp_ldc_deinit, T32_DI_LDC)
DI(tisp_wdr_deinit, T32_DI_WDR) DI(tisp_rcfg_deinit, ID_RCFG)
u64 tisp_core_tuning_deinit(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; rec(vc, &vn, ID_CORE, 0); return rnd(); }
u64 private_vfree(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; rec(vc, &vn, ID_VFREE, a0); return rnd(); }

/* our hooks (the LUT info struct stands for the vendor's block) */
static u32 o_ptr(const void *p)
{
	unsigned int i;

	for (i = 0; i < 4; i++)
		if (p == &lut_o[i])
			return T32O_PTR(lut_v[i]);
	return (u32)(uintptr_t)p;
}
static void o_blk(void *c, enum t32_deinit_blk b, u32 ch) { (void)c; rec(oc, &on, b, ch); }
static void o_rcfg(void *c, u32 ch) { (void)c; rec(oc, &on, ID_RCFG, ch); }
static void o_core(void *c) { (void)c; rec(oc, &on, ID_CORE, 0); }
static void o_vfree(void *c, void *p) { (void)c; rec(oc, &on, ID_VFREE, o_ptr(p)); }

static u32 rptr(void) { return rnd() % 3 ? (rnd() | 1) : 0; }

int main(void)
{
	unsigned int c, i, k, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 20000;
	unsigned int compared = 0, failed = 0;
	struct t32_deinit_hooks h = { NULL, o_blk, o_rcfg, o_core, o_vfree };
	static void *par_o[24], *tun_o[4];
	static struct t32_lut_info *lutp_o[4];
	static u32 f0918_o[4];
	static u8 e4018_o[4], sizes_o[112], run_o[4];
	struct t32_deinit_env e = { f0918_o, e4018_o, par_o, tun_o, lutp_o, sizes_o, run_o };

	for (c = 0; c < n; c++) {
		u32 ch = (rnd() & ~0xffu) | (c & 3), bad;

		t32o_reset();
		vn = on = 0;
		memset(vc, 0, sizeof(vc));
		memset(oc, 0, sizeof(oc));
		for (i = 0; i < 24; i++) {
			t32_data_dd0b4[i] = rptr();
			par_o[i] = (void *)(uintptr_t)t32_data_dd0b4[i];
		}
		for (i = 0; i < 4; i++) {
			t32_data_dd0a4[i] = rptr();
			tun_o[i] = (void *)(uintptr_t)t32_data_dd0a4[i];
			lut_v[i] = t32o_alloc(44);
			for (k = 0; k < 11; k++) {
				u32 w = k < 4 ? rnd() : rptr();

				memcpy(lut_v[i] + 4 * k, &w, 4);
				if (k < 4)
					lut_o[i].head[k] = w;
				else
					lut_o[i].buf[k - 4] = (void *)(uintptr_t)w;
			}
			if (rnd() % 4) {
				t32_data_dc274[i] = T32O_PTR(lut_v[i]);
				lutp_o[i] = &lut_o[i];
			} else {
				t32_data_dc274[i] = 0;
				lutp_o[i] = NULL;
			}
			f0918_o[i] = t32_data_f0918[i] = rnd();
			e4018_o[i] = t32_data_e4018[i] = (u8)rnd();
			run_o[i] = t32_data_dc060[i] = (u8)rnd();
		}
		for (i = 0; i < 112; i++)
			sizes_o[i] = t32_data_dc1a4[i] = (u8)rnd();

		t32o_call(F(lib_tisp_deinit), ch, rnd(), rnd(), rnd(), 0);
		t32_lib_deinit(&h, &e, ch);
		bad = vn != on || vn > NC || memcmp(vc, oc, sizeof(vc)) || memcmp(f0918_o, t32_data_f0918, 16) ||
		      memcmp(e4018_o, t32_data_e4018, 4) || memcmp(sizes_o, t32_data_dc1a4, 112) ||
		      memcmp(run_o, t32_data_dc060, 4);
		for (i = 0; !bad && i < 24; i++)
			bad = t32_data_dd0b4[i] != (u32)(uintptr_t)par_o[i];
		for (i = 0; !bad && i < 4; i++) {
			bad = t32_data_dd0a4[i] != (u32)(uintptr_t)tun_o[i] || t32_data_dc274[i] != o_ptr(lutp_o[i]);
			for (k = 0; !bad && k < 11; k++) {
				u32 w;

				memcpy(&w, lut_v[i] + 4 * k, 4);
				bad = w != (k < 4 ? lut_o[i].head[k] : (u32)(uintptr_t)lut_o[i].buf[k - 4]);
			}
		}
		compared++;
		if (bad && failed++ < 10)
			printf("MISMATCH case %u ch %x: calls %u/%u\n", c, ch, vn, on);
	}
	printf("t32_libdeinit_oracle: %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
