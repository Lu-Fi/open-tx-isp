/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T40 ISP OSD setters: plain-data helpers (#158, #160, #141), host test
 * tests/tx_isp_t40_osd_slot_test.c.
 *
 *  - t40_osd_counter: the OEM bumps a per-sensor counter of the core slot
 *    table (core + 268) after an OSD / mask / draw set, "addu a0,s0,(vinum<<2)"
 *    in BYTES.  The lift declared the table as uint32_t *, so s0 + (vinum << 2)
 *    moved 16 bytes per sensor.  The table may not exist yet (NULL) and the
 *    sensor index comes from the ioctl: both give NULL here (no bump).
 *  - t40_osd_phys: the ISP draw block is programmed with the PHYSICAL picture
 *    address (the IMP_OSD_*_ISP pool address, which libimp passes in
 *    osd_image).  GetOSDAttr / GetSingleOSDAttr return it as KSEG0 virtual
 *    (stored + 0x80000000); an application that writes the value it read back
 *    (the apitest does) hands in the KSEG0 form, which the HW cannot fetch.
 *  - t40_osd_origin_ok: left/top must lie inside the channel, otherwise the
 *    clip "full - left" wraps the u16 width/height to ~65k.
 */
#ifndef TX_ISP_T40_OSD_SLOT_H
#define TX_ISP_T40_OSD_SLOT_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#include <stddef.h>
typedef uint32_t u32;
#endif

static inline u32 *t40_osd_counter(void *slot_tbl, u32 vinum, u32 byte_off)
{
	if (!slot_tbl || vinum > 1)
		return NULL;
	return (u32 *)((char *)slot_tbl + ((size_t)vinum << 2) + byte_off);
}

static inline u32 t40_osd_phys(u32 addr)
{
	/* KSEG0 / KSEG1 (0x80000000..0xbfffffff) -> physical, others as is */
	if (addr >= 0x80000000u && addr < 0xc0000000u)
		return addr & 0x1fffffffu;
	return addr;
}

static inline int t40_osd_origin_ok(u32 left, u32 top, u32 full_w, u32 full_h)
{
	return left < full_w && top < full_h;
}

#endif
