/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "gammaev" (openimp #507): tisp_gamma_ev_update and
 * tisp_gamma_switch_para (driver/t32/tx_isp_t33_gamma.h t33_gamma_ev_update / t33_gamma_switch) built for mipsel and run against the vendor
 * tisp_gamma_ev_update / tisp_gamma_switch_para of tx-isp-t33.ko by tools/ko_emu/diff_t33_gammaev.py on the same section 13,
 * curves, strength byte, curve state and EV.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_gamma.h"

int emu_gamma_ev(const u8 *sec, const u8 *tab, u32 lo, u32 hi, u8 *str, u32 *ip, u16 *c, u16 *ap, u32 *l)
{
	return t33_gamma_ev_update(sec, tab, lo, hi, str, ip, c, ap, l);
}

void emu_gamma_sw(const u8 *sec, const u8 *tab, const u32 *ip, u8 *str, u16 *c, u16 *ap, u32 *l)
{
	t33_gamma_switch(sec, tab, ip, str, c, ap, l);
}
