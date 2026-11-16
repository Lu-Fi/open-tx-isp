/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "r13F": AE API accessors with the vendor calling convention. */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_r13F.h"

extern void private_dma_cache_sync(u32 dev, u32 addr, u32 len, u32 dir);
extern u32 fix_point_div_32(u32 sh, u32 a, u32 b);
extern u32 fix_point_mult2_32(u32 sh, u32 a, u32 b);

u8 *r13F_ae_info[4];
u8 *r13F_ae_cache[4];
u8 *r13F_hist_buff[4];
u32 r13F_stat_num[8];

static const struct t33_aeapi_ops r13F_ops = {
	private_dma_cache_sync, fix_point_div_32, fix_point_mult2_32,
};

int bo_tisp_ae_api_get_scene_par(u32 ch, u8 *out)
{
	t33_ae_api_get_scene_par(&r13F_ops, r13F_ae_info[ch], r13F_ae_cache[ch], r13F_hist_buff[ch],
				 r13F_stat_num[2 * ch + 1], out);
	return 0;
}

void bo_tisp_ae_api_set_scene_par(u32 ch, const u8 *in)
{
	t33_ae_api_set_scene_par(r13F_ae_info[ch], in);
}

void bo_tisp_ae_api_get_scene_luma(u32 ch, u8 *out)
{
	t33_ae_api_get_scene_luma(&r13F_ops, r13F_ae_info[ch], r13F_ae_cache[ch], r13F_hist_buff[ch],
				  r13F_stat_num[2 * ch + 1], out);
}

void bo_tisp_ae_api_get_stable(u32 ch, u8 *out)
{
	t33_ae_api_get_stable(r13F_ae_info[ch & 0xff], out);
}

void bo_tisp_ae_api_get_luma(u32 ch, u8 *out)
{
	t33_ae_api_get_luma(r13F_ae_info[ch], r13F_hist_buff[ch], r13F_stat_num[2 * ch + 1], out);
}

int bo_tisp_ae_get_at_list(u32 ch, u8 *out)
{
	return t33_ae_get_at_list(r13F_ae_info[ch & 0xff], r13F_ae_cache[ch & 0xff], out);
}

int bo_tisp_ae_set_at_list(u32 ch, u32 mode, const u8 *list)
{
	return t33_ae_set_at_list(r13F_ae_info[ch & 0xff], r13F_ae_cache[ch & 0xff], mode, list);
}

int bo_tisp_ae_get_ev_list(u32 ch, u8 *out)
{
	return t33_ae_get_ev_list(r13F_ae_info[ch & 0xff], r13F_ae_cache[ch & 0xff], out);
}

int bo_tisp_ae_set_ev_list(u32 ch, u32 mode, const u8 *list)
{
	return t33_ae_set_ev_list(r13F_ae_info[ch & 0xff], r13F_ae_cache[ch & 0xff], mode, list);
}
