/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "r11C": AE API accessors with the vendor calling convention. */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_r11C.h"

extern void __private_spin_lock_irqsave(void *lock, u32 *flags);
extern void private_spin_unlock_irqrestore(void *lock, u32 flags);
extern int tisp_gsm_api_get(u32 ch, u8 *out);
extern void tisp_normal_ae_par_sensor_trig(u32 ch);

u8 our_hist[2104];
u8 *our_stAeInfo[4];
u8 *our_ae_cache[4];
u8 *our_tattr;
static u8 our_lock[8];

int bo_api_ae_get_hist_bin(u32 ch, u8 *out)
{
	u32 fl;

	__private_spin_lock_irqsave(our_lock, &fl);
	t33_ae_get_hist_bin(our_hist, ch, out);
	private_spin_unlock_irqrestore(our_lock, fl);
	return 0;
}

int bo_api_ae_get_y_zone(u32 ch, u8 *out)
{
	u32 fl = 0;

	__private_spin_lock_irqsave(our_lock, &fl);
	t33_ae_get_y_zone(our_stAeInfo, ch, out);
	private_spin_unlock_irqrestore(our_lock, fl);
	return 0;
}

int bo_tisp_ae_api_get_flicker_flag(u32 ch, u32 *out)
{
	t33_ae_get_flicker_flag(our_ae_cache, ch, out);
	return 0;
}

int bo_tisp_ae_api_get_gobal_roi_luma(u32 ch, u32 *o1, u32 *o2)
{
	t33_ae_get_gobal_roi_luma(our_ae_cache, ch, o1, o2);
	return 0;
}

int bo_tisp_g_ae_flicker_flag(u32 ch, u32 *out)
{
	return bo_tisp_ae_api_get_flicker_flag(ch & 0xff, out);
}

static void hist_cb(u32 ch, u8 *out) { bo_api_ae_get_hist_bin(ch, out); }
static void gsm_cb(u32 ch, u8 *out) { tisp_gsm_api_get(ch, out); }
static void yz_cb(u32 ch, u8 *out) { bo_api_ae_get_y_zone(ch, out); }

int bo_tisp_g_ae_statis_attr(u32 ch, u8 *out)
{
	return t33_g_ae_statis_attr(our_tattr, ch, out, hist_cb, gsm_cb, yz_cb);
}

int bo_tisp_g_face_ae_luma(u32 ch, u32 *out)
{
	t33_g_face_ae_luma(our_ae_cache, ch, out);
	return 0;
}

void bo_tisp_ae_par_sensor_trig(u32 ch)
{
	t33_ae_par_sensor_trig(ch, tisp_normal_ae_par_sensor_trig);
}

u32 bo_calculate_exponential(u32 x, u32 n, u32 a, u32 q)
{
	return t33_calculate_exponential(x, n, a, q);
}
