/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "defbin": tx_isp_default_bin with the vendor calling convention. */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_defbin.h"

u32 our_zeratul_open;
u8 our_zeratulFirst;

extern int private_copy_from_user(void *to, const void *from, unsigned long n);
extern int private_copy_to_user(void *to, const void *from, unsigned long n);
extern u32 get_sensor_start_iq_index(u32 s);
extern int get_sensor_section_num(u32 s, u32 start);
extern unsigned long get_sensor_setting(u32 s, u32 start, u32 i);
extern unsigned long get_sensor_setting_len(u32 s, u32 start, u32 i);
extern unsigned long get_sensor_setting_md5(u32 s, u32 start, u32 i);
extern void release_sensor_setting(u32 s, u32 start, u32 i);
extern void lib_tisp_get_bin_info(u32 s, void *req);
extern u32 tisp_day_or_night_g_ctrl(u32 s);
extern u32 lib_tisp_get_wdr_enable(u32 s);
extern int isp_printf(int level, const char *fmt, ...);

static const struct t33_defbin_ops defbin_ops = {
	private_copy_from_user, private_copy_to_user, get_sensor_start_iq_index, get_sensor_section_num,
	get_sensor_setting, get_sensor_setting_len, get_sensor_setting_md5, release_sensor_setting,
	lib_tisp_get_bin_info, tisp_day_or_night_g_ctrl, lib_tisp_get_wdr_enable, isp_printf,
};

int bo_tx_isp_default_bin(u8 *ctx, u8 *user)
{
	return t33_default_bin(ctx, user, &our_zeratul_open, &our_zeratulFirst, &defbin_ops);
}
