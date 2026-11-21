/* SPDX-License-Identifier: GPL-2.0 */
/*
 * binary oracle, block "t32-bcshapi": tisp_bcsh_api_set_brightness / _contrast / _saturation / _hue of the vendor
 * tx-isp-t32.ko (T32_OEM_KO) vs t32_bcsh_api_set_* of driver/t32/tx_isp_t32_bcsh.h (openimp #716, the T32
 * SetBrightness / SetContrast / SetSaturation / SetBcshHue path).  The vendor keeps the block in bcsh_para[ch] ->
 * { sec, state, cp, m112, ap, p5, spin lock }; state = { ev lo, ev hi, ct, ct code, ctr ev, ctr ct, pend, b19 }.
 * The wrapper loads our struct t32_bcsh from those blocks, runs our port and stores it back, so both sides leave
 * the same bytes.  The vendor's tisp_bcsh_interp_by_ev / tisp_bcsh_calc_para_refresh and their callees run
 * unchanged on its side; its spin lock wrappers are NOPs.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t32_bcsh.h"

u8 *bcsh_para[4];
static struct t32_bcsh bo_st;

static u32 bo_r32(const u8 *p) { return p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }
static void bo_w32(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }
static u8 *bo_p(const u8 *S, unsigned int k) { return (u8 *)(uintptr_t)bo_r32(S + 4 * k); }

static void bo_in(u32 ch)
{
	const u8 *S = bcsh_para[ch], *s = bo_p(S, 1);

	memset(&bo_st, 0, sizeof(bo_st));
	bo_st.sec = bo_p(S, 0);
	memcpy(bo_st.cp, bo_p(S, 2), sizeof(bo_st.cp));
	memcpy(bo_st.m112, bo_p(S, 3), sizeof(bo_st.m112));
	memcpy(bo_st.ap, bo_p(S, 4), sizeof(bo_st.ap));
	memcpy(bo_st.p5, bo_p(S, 5), sizeof(bo_st.p5));
	bo_st.ev_lo = bo_r32(s);
	bo_st.ev_hi = bo_r32(s + 4);
	bo_st.ct = bo_r32(s + 8);
	bo_st.ct_code = bo_r32(s + 12);
	bo_st.ctr_ev = s[16];
	bo_st.ctr_ct = s[17];
	bo_st.pend = s[18];
	bo_st.b19 = s[19];
}

static void bo_out(u32 ch)
{
	const u8 *S = bcsh_para[ch];
	u8 *s = bo_p(S, 1);

	memcpy(bo_p(S, 2), bo_st.cp, sizeof(bo_st.cp));
	memcpy(bo_p(S, 3), bo_st.m112, sizeof(bo_st.m112));
	memcpy(bo_p(S, 4), bo_st.ap, sizeof(bo_st.ap));
	memcpy(bo_p(S, 5), bo_st.p5, sizeof(bo_st.p5));
	bo_w32(s, bo_st.ev_lo);
	bo_w32(s + 4, bo_st.ev_hi);
	bo_w32(s + 8, bo_st.ct);
	bo_w32(s + 12, bo_st.ct_code);
	s[16] = bo_st.ctr_ev;
	s[17] = bo_st.ctr_ct;
	s[18] = bo_st.pend;
	s[19] = bo_st.b19;
}

void bo_tisp_bcsh_api_set_brightness(u32 ch, const u8 *v) { bo_in(ch); t32_bcsh_api_set_brightness(&bo_st, *v); bo_out(ch); }
void bo_tisp_bcsh_api_set_contrast(u32 ch, const u8 *v) { bo_in(ch); t32_bcsh_api_set_contrast(&bo_st, *v); bo_out(ch); }
void bo_tisp_bcsh_api_set_saturation(u32 ch, const u8 *v) { bo_in(ch); t32_bcsh_api_set_saturation(&bo_st, *v); bo_out(ch); }
void bo_tisp_bcsh_api_set_hue(u32 ch, const u8 *v) { bo_in(ch); t32_bcsh_api_set_hue(&bo_st, *v); bo_out(ch); }
