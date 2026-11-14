/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "t33mem": tisp_mdns_t33_get_mem_size (tx_isp_t33_mem.h) with the vendor calling
 * convention: the ratio comes from stMemInfo[ch] (76 bytes each, u16 at +8), the result goes to +0x20.
 */
#include <stdint.h>
#include <string.h>
#ifndef T32_HOST_U32
#define T32_HOST_U32
typedef uint32_t u32;
#endif
#ifndef T32_HOST_U8
#define T32_HOST_U8
typedef uint8_t u8;
typedef uint16_t u16;
#endif
#include "tx_isp_t33_mem.h"

u8 stMemInfo[304];
extern int isp_printf(int lvl, const char *fmt);

u32 bo_mdns_t33_get_mem_size(u32 ch, u32 w, u32 h, u32 sel, u32 pad)
{
	u8 *m = stMemInfo + (ch & 0xff) * 76;
	u32 r = t33_mdns_get_mem_size(*(u16 *)(m + 8), w, h, sel, pad);

	if (r == T33_MDNS_E_MEMOPT) {
		isp_printf(2, "");
		return r;
	}
	memcpy(m + 0x20, &r, 4);
	return r;
}
