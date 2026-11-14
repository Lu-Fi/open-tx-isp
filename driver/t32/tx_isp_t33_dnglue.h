/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) CSCCR / MDNS day-night glue (openimp #99, #507):
 * lib_tisp_set_csccr, tisp_csccr_set_day / tisp_csccr_set_night and
 * tisp_mdns_dn_params_api / tisp_mdns_dn_params_api2 (binary oracle block
 * "dnglue": every list-slot write through tisp_reg_write_t33 recorded and
 * compared with the vendor's).
 *
 * Night: lib_tisp_set_csccr(ch, 1) clears the CSCCR UV clip at once (slot 7
 * = 0).  Day: lib_tisp_set_csccr(ch, 0) arms two frame numbers; the CSCCR
 * frame done (tx_isp_t33_fd_csccr.h) then runs dn_params_api2 (the MDNS UV
 * limit word, slot 0xb8, only its byte 0) + set_day (slot 7 = 0x7f0180) on
 * the first and dn_params_api (the MDNS word with its limits) on the second.
 * Before the ISP init finished the request only goes to start_night.
 *
 * The includer declares tisp_reg_write_t33(ch, mod, reg, val) and
 * isp_printf(level, fmt, ...).  cfg: the block's cfg mode (1: list slot,
 * 0: the direct register), info: mdnsInfo[ch] (bytes 0x140c / 0x1410 /
 * 0x1414 = the UV limit bytes 0 / 1 / 2).
 */
#ifndef TX_ISP_T33_DNGLUE_H
#define TX_ISP_T33_DNGLUE_H

#include "tx_isp_t33_blk.h"

#define T33_DN_MOD_CSCCR	0x16
#define T33_DN_MOD_MDNS		0xf
#define T33_DN_CH_SIZE		1008	/* stIspInitPar stride per channel */
#define T33_DN_DAY_FLAG		41	/* stIspInitPar byte: 1 night (UV clip off) */

struct t33_dn_state {
	u8 *csccr_cfg, *csccr_refresh;		/* csccrCfgMode, tispCsccrRefresh */
	u8 *mdns_cfg, *mdns_refresh;		/* mdnsCfgMode, tispMdnsRefresh */
	u8 *uv_limit;				/* uvLimitFlag[ch] */
	u8 *init_finish, *start_night;		/* ispInitFinish[ch], start_night[ch] */
	u8 *initpar;				/* stIspInitPar */
	u32 *cnt, *cur, *cur2;			/* frameCount, curFrame, curFrame2 */
};

static inline void t33_csccr_uv(const struct t33_dn_state *s, u32 ch, u32 uv)
{
	tisp_reg_write_t33(ch & 0xff, T33_DN_MOD_CSCCR, *s->csccr_cfg ? 7 : 0x14054, uv);
	tisp_reg_write_t33(ch & 0xff, T33_DN_MOD_CSCCR, *s->csccr_cfg ? 1 : 0x14010, 3);
	*s->csccr_refresh = 1;
}

/* tisp_csccr_set_day / tisp_csccr_set_night */
static inline void t33_csccr_set_day(const struct t33_dn_state *s, u32 ch)
{
	t33_csccr_uv(s, ch, 0x7f0180);
}

static inline void t33_csccr_set_night(const struct t33_dn_state *s, u32 ch)
{
	t33_csccr_uv(s, ch, 0);
}

/* tisp_mdns_dn_params_api: the UV limit word with its limits back */
static inline int t33_mdns_dn_api(const struct t33_dn_state *s, u32 ch, const u8 *info)
{
	tisp_reg_write_t33(ch, T33_DN_MOD_MDNS, *s->mdns_cfg ? 0xb8 : 0xd1cc,
			   (info[0x1410] << 8) | (info[0x1414] << 16) | info[0x140c]);
	*s->mdns_refresh = 1;
	s->uv_limit[ch] = 1;
	return 0;
}

/* tisp_mdns_dn_params_api2: only byte 0 of the UV limit word */
static inline int t33_mdns_dn_api2(const struct t33_dn_state *s, u32 ch, const u8 *info)
{
	tisp_reg_write_t33(ch, T33_DN_MOD_MDNS, *s->mdns_cfg ? 0xb8 : 0xd1cc, info[0x140c]);
	*s->mdns_refresh = 1;
	s->uv_limit[ch] = 0;
	return 0;
}

/* lib_tisp_set_csccr(ch, mode): mode 0 day, 1 night; else -1 once the ISP init finished */
static inline int t33_lib_set_csccr(const struct t33_dn_state *s, u32 ch, u32 mode)
{
	ch &= 0xff;
	mode &= 0xff;
	if (s->init_finish[ch] == 1) {
		if (mode == 0) {
			s->initpar[ch * T33_DN_CH_SIZE + T33_DN_DAY_FLAG] = 0;
			s->cur[ch] = (s->cnt[ch] + 1) & 0xffffff;
			s->cur2[ch] = (s->cnt[ch] + 2) & 0xffffff;
			return 0;
		}
		if (mode == 1) {
			s->initpar[ch * T33_DN_CH_SIZE + T33_DN_DAY_FLAG] = 1;
			t33_csccr_set_night(s, ch);
			return 0;
		}
		isp_printf(2, "%s:%d:can not support this mode: %d!!!", "lib_tisp_set_csccr", 0x147, mode);
		return -1;
	}
	if (s->init_finish[ch] == 0)
		s->start_night[ch] = (u8)mode;
	return 0;
}

#endif /* TX_ISP_T33_DNGLUE_H */
