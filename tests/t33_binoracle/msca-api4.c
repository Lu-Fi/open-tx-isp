/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "msca-api4": MSCA API getters/setters with the vendor calling convention
 * (spec: msca-api4.conf).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_msca_api4.h"

extern void tisp_reg_write_t33(u32 id, u32 list, u32 slot, u32 val);
extern int printk(const char *fmt, ...);

u8 *stMscaAttr[2];
u32 stIspInitPar[227];
u8 mscaCfgMode;
u8 tispMscaRefresh;

void bo_tisp_msca_t33_api_get_mirr_flip(u32 chn, u8 *p) { t33_msca_get_mirr_flip(stMscaAttr[chn & 0xff], p); }
void bo_tisp_msca_t33_api_get_osd_out_attribute_cfg(u32 chn, u8 *o) { t33_msca_get_osd_out(stMscaAttr[chn & 0xff], o); }
void bo_tisp_msca_t33_api_set_osd_out_attribute_cfg(u32 chn, u8 *i) { t33_msca_set_osd_out(stMscaAttr[chn & 0xff], i); }

void bo_tisp_msca_t33_api_set_msca_dma_stride(u32 chn, u32 ch, u32 a, u32 b)
{
	u32 s;

	if (t33_msca_stride_refused(stIspInitPar[226])) {
		printk("msca dma stride: isp not ready\n");
		return;
	}
	s = t33_msca_stride_value(a, b);
	tisp_reg_write_t33(chn & 0xff, 23, t33_msca_stride_slot(mscaCfgMode, ch, 0), s);
	tisp_reg_write_t33(chn & 0xff, 23, t33_msca_stride_slot(mscaCfgMode, ch, 1), s);
	tispMscaRefresh = 1;
}
