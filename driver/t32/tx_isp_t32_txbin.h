/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 tx-isp IQ bin ioctl helpers (vendor tx_isp_switch_bin and
 * tx_isp_default_bin, .isra clones).
 *
 * Switch block (136 bytes from the user): +0 0 = take the path from
 * lib_tisp_get_bin_path (+5 = the path of slot +4), +4 u8 bin slot.
 * Default-bin block (880 bytes): +0 2 (fast start), +4 / +28 / +52 the
 * sensor settings (address, address | 0x80000000, length) of up to
 * +76 sections, +852 WDR enable, +856 day / night, +860 "first bin" flag,
 * +861 check flag, +862 sensor index.
 * ISP argument: +0 core subdevice (+280 core: +300 + 4 i input state,
 * +1164 + 4 i switch pending, +1196 sensor number), +32 + 32 i current bin
 * slot of input i, +152 switch completion.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C,
 * PR #15) by tests/t32_oracle (t32_txbin_oracle).
 */
#ifndef TX_ISP_T32_TXBIN_H
#define TX_ISP_T32_TXBIN_H

#include "tx_isp_t32_txos.h"
#include "tx_isp_t32_txioctl.h"

#define T32_SWBIN_BYTES		136U
#define T32_SWBIN_PATHS		908U
#define T32_SWBIN_PATH(k)	(((k) + 1U) * 128U + 4U)
#define T32_CORE_IN_STATE(i)	(300U + 4U * (i))
#define T32_CORE_SW_PEND(i)	(1164U + 4U * (i))
#define T32_CORE_SNS_NUM	1196U
#define T32_ISP_BIN_SLOT(i)	(32U + 32U * (i))
#define T32_ISP_SW_DONE		152U
#define T32_ETIMEDOUT		145U

#define T32_DEFBIN_BYTES	880U
#define T32_DEFBIN_NSEC		76U	/* u8 */
#define T32_DEFBIN_WDR		852U
#define T32_DEFBIN_DN		856U
#define T32_DEFBIN_FIRST	860U	/* u8 */
#define T32_DEFBIN_CHECK	861U	/* u8 */
#define T32_DEFBIN_SENSOR	862U	/* u8 */

/* provided by the driver glue (kernel) or by the oracle (host) */
extern u8 t32_tx_first_bin;		/* vendor cfb54 (data.c:18562, 1 at load) */
u32 t32_tx_os_vmalloc(u32 n);
void t32_tx_os_vfree(u32 a);
void t32_tx_os_get_bin_path(u32 ch, u32 buf);
int t32_tx_os_tisp_switch_bin(u32 ch, void *blk);
int t32_tx_os_wait_for_completion_timeout(u32 c, u32 t);
u32 t32_tx_os_sensor_start_iq_index(u32 s);
u32 t32_tx_os_sensor_section_num(u32 s, u32 iq);
u32 t32_tx_os_sensor_setting(u32 s, u32 iq, u32 k);
u32 t32_tx_os_sensor_setting_len(u32 s, u32 iq, u32 k);
void t32_tx_os_sensor_setting_md5(u32 s, u32 iq, u32 k);
void t32_tx_os_tisp_get_bin_info(u32 s, void *blk);
u32 t32_tx_os_tisp_day_or_night(u32 s);
u32 t32_tx_os_tisp_wdr_enable(u32 s);

/*
 * tx_isp_switch_bin(input, isp, user).  Vendor: the stored path is the
 * sprintf format, slot +4 is not bounded.
 */
static inline int t32_tx_switch_bin(u32 ch, u32 isp, u32 user)
{
	u32 core = t32_tx_ld(t32_tx_ld(isp, 0), T32_SD_PRIV_CORE), b[T32_SWBIN_BYTES / 4], p;
	u8 *b8 = (u8 *)b;
	int r;

	r = (int)t32_tx_os_copy_from_user(b, user, T32_SWBIN_BYTES);
	if (r) {
		t32_tx_os_isp_printf(2, "[ %s:%d ] copy error!!!\n", "tx_isp_switch_bin", 1166);
		return r;
	}
	if (!b[0]) {
		p = t32_tx_os_vmalloc(T32_SWBIN_PATHS);
		if (!p) {
			t32_tx_os_isp_printf(1, "[ %s:%d ] vmalloc failed!!!\n", "tx_isp_switch_bin", 1172);
			return -1;
		}
		t32_tx_os_get_bin_path(ch & 0xff, p);
		t32_tx_os_sprintf((char *)b8 + 5, (const char *)T32_TX_P(p + T32_SWBIN_PATH(b8[4])));
		t32_tx_os_vfree(p);
	}
	if (t32_tx_ld(core, T32_CORE_IN_STATE(ch)) != 4) {
		t32_tx_os_isp_printf(2, "[ %s:%d ] The camera is not activated!!!\n", "tx_isp_switch_bin", 1195);
		return 0;
	}
	r = t32_tx_os_tisp_switch_bin(ch, b);
	if (b8[4] == t32_tx_ld(isp, T32_ISP_BIN_SLOT(ch))) {
		t32_tx_st(core, T32_CORE_SW_PEND(ch), 1);
		if (t32_tx_os_wait_for_completion_timeout(isp + T32_ISP_SW_DONE, 100))
			return 0;
		t32_tx_os_isp_printf(1, "Timeout waiting for completion\n");
		return -(int)T32_ETIMEDOUT;
	}
	if (!r)
		return 0;
	t32_tx_os_isp_printf(1, "[ %s:%d ] switch bin failed!!!\n", "tx_isp_switch_bin", 1198);
	return r;
}

/*
 * tx_isp_default_bin(isp, user).  Vendor: the section count is not bounded
 * against the 880 byte block; the md5 result is not used.
 */
static inline int t32_tx_default_bin(u32 isp, u32 user)
{
	u32 b[T32_DEFBIN_BYTES / 4], core, s, iq, k, v;
	u8 *b8 = (u8 *)b;

	if (t32_tx_os_copy_from_user(b, user, T32_DEFBIN_BYTES)) {
		t32_tx_os_isp_printf(2, "[ %s:%d ] copy from user error\n", "tx_isp_default_bin", 1253);
		return -(int)T32_EFAULT;
	}
	s = b8[T32_DEFBIN_SENSOR];
	core = t32_tx_ld(t32_tx_ld(isp, 0), T32_SD_PRIV_CORE);
	if (t32_tx_g_d9008) {
		iq = t32_tx_os_sensor_start_iq_index(s);
		b[0] = 2;
		v = t32_tx_os_sensor_section_num(s, iq);
		b8[T32_DEFBIN_NSEC] = (u8)v;
		t32_tx_os_isp_printf(1, "to tiziano iq_index:%d binNum:%d\n", iq, v & 0xff);
		for (k = 0; (int)k < (int)b8[T32_DEFBIN_NSEC]; k++) {
			u32 a = t32_tx_os_sensor_setting(s, iq, k);

			b[1 + k] = a;
			b[7 + k] = a + 0x80000000U;
			b[13 + k] = t32_tx_os_sensor_setting_len(s, iq, k);
			t32_tx_os_sensor_setting_md5(s, iq, k);
		}
	}
	if (b8[T32_DEFBIN_CHECK] && t32_tx_ld(core, T32_CORE_SNS_NUM) - 1 == s)
		t32_tx_first_bin = 0;
	t32_tx_os_tisp_get_bin_info(s, b);
	b8[T32_DEFBIN_FIRST] = t32_tx_g_d9008 ? t32_tx_first_bin != 0 : 0;
	b[T32_DEFBIN_DN / 4] = t32_tx_os_tisp_day_or_night(s);
	b[T32_DEFBIN_WDR / 4] = t32_tx_os_tisp_wdr_enable(s);
	if (t32_tx_os_copy_to_user(user, b, T32_DEFBIN_BYTES)) {
		t32_tx_os_isp_printf(2, "[ %s:%d ] copy to user error\n", "tx_isp_default_bin", 1290);
		return -(int)T32_EFAULT;
	}
	return 0;
}

#endif
