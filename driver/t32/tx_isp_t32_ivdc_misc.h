/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 IVDC misc device ioctl from the vendor module (Matt Davis' recovered C,
 * PR #15): ivdc_misc_unlocked_ioctl (0xd894).
 *
 * Get / set of four module words through 4-byte user copies: the direct
 * mode (t32_data_d9014, struct t32_ivdc .mode), the memory line count
 * (d9010), the memory physical address (d98b8, set only) and the memory
 * stride (ce230, get only).  0x2000546f / 0x20005470 are accepted and do
 * nothing.  The user copies are hooks of struct t32_ivdc_uos.
 *
 * Vendor quirks kept: a failed copy returns the number of bytes not copied
 * (positive, not -EFAULT) after its printk (the set word may be partly
 * written by the copy); an unknown command prints and returns -1.
 *
 * Checked bit-exact against the vendor flow by tests/t32_oracle
 * (t32_ivmisc_oracle).
 */
#ifndef TX_ISP_T32_IVDC_MISC_H
#define TX_ISP_T32_IVDC_MISC_H

#include "tx_isp_t32_ivdc.h"

#define T32_IVDC_IOC_NOP0		0x2000546fU
#define T32_IVDC_IOC_NOP1		0x20005470U
#define T32_IVDC_IOC_GET_MODE		0xc0045471U
#define T32_IVDC_IOC_SET_MODE		0xc0045472U
#define T32_IVDC_IOC_SET_PADDR		0xc0045473U
#define T32_IVDC_IOC_GET_LINE		0xc0045474U
#define T32_IVDC_IOC_GET_STRIDE		0xc0045475U
#define T32_IVDC_IOC_SET_LINE		0xc0045476U

struct t32_ivdc_mem {
	u32 line;	/* t32_data_d9010 */
	u32 paddr;	/* t32_data_d98b8 */
	u32 stride;	/* t32_data_ce230 */
};

struct t32_ivdc_uos {
	u32 (*copy_from_user)(u8 *dst, u32 user, u32 n);	/* bytes left */
	u32 (*copy_to_user)(u32 user, const u8 *src, u32 n);
};

/*
 * SET_MODE, validated before it is published (openimp #780, beyond vendor): the vendor copies the user
 * word straight into the live direct_mode (tx-isp-t32.ko / tx-isp-t33.ko ivdc_misc_unlocked_ioctl), and the
 * driver clamped it afterwards, so a stream-on running at the same time could see 2..4 or a partly copied
 * word.  Here the word goes into a local, a value above 1 (several sensors, not supported) becomes 0, and
 * only then one aligned store updates d->mode.  A failed copy leaves d->mode unchanged.  *bad gets the
 * rejected value (0 if none).
 * #783 (beyond vendor, user decision 2026-11-22): while a stream runs (live: from the IVDC start in the
 * ISP stream-on until the stream-off) the mode is fixed: a different (validated) mode returns -EBUSY
 * (-16) and is not stored; the same mode is accepted.  A mode flip during a direct-mode start reset the
 * T33L by the watchdog (the IVDC, the frame channel and the ISP top read the mode at different times).
 */
#define T32_IVDC_EBUSY	16
static inline long t32_ivdc_set_mode_checked(struct t32_ivdc *iv, const struct t32_ivdc_uos *uo, u32 arg,
					     u32 *bad, int live)
{
	u32 v = 0, r;

	*bad = 0;
	r = uo->copy_from_user((u8 *)&v, arg, 4);
	if (r) {
		isp_printf(2, "Set direct_mode err! Failed to copy form user\n");
		return (long)(int)r;
	}
	if (v > 1) {
		*bad = v;
		v = 0;
	}
	if (live && v != iv->mode)
		return -T32_IVDC_EBUSY;
	*(volatile u32 *)&iv->mode = v;
	return 0;
}

static inline long t32_ivdc_misc_unlocked_ioctl(struct t32_ivdc *iv, struct t32_ivdc_mem *m,
						const struct t32_ivdc_uos *uo, u32 cmd, u32 arg)
{
	const char *err;
	u32 r;

	switch (cmd) {
	case T32_IVDC_IOC_NOP0:
	case T32_IVDC_IOC_NOP1:
		return 0;
	case T32_IVDC_IOC_GET_MODE:
		r = uo->copy_to_user(arg, (const u8 *)&iv->mode, 4);
		err = "Get direct_mode err! Failed to copy to user\n";
		break;
	case T32_IVDC_IOC_SET_MODE:
		r = uo->copy_from_user((u8 *)&iv->mode, arg, 4);
		err = "Set direct_mode err! Failed to copy form user\n";
		break;
	case T32_IVDC_IOC_SET_PADDR:
		r = uo->copy_from_user((u8 *)&m->paddr, arg, 4);
		err = "Set ivdc_mem_paddr err! Failed to copy from user\n";
		break;
	case T32_IVDC_IOC_GET_LINE:
		r = uo->copy_to_user(arg, (const u8 *)&m->line, 4);
		err = "Get ivdc_mem_line err! Failed to copy to user\n";
		break;
	case T32_IVDC_IOC_GET_STRIDE:
		r = uo->copy_to_user(arg, (const u8 *)&m->stride, 4);
		err = "Get ivdc_mem_stride err! Failed to copy to user\n";
		break;
	case T32_IVDC_IOC_SET_LINE:
		r = uo->copy_from_user((u8 *)&m->line, arg, 4);
		err = "Set ivdc_mem_line err! Failed to copy form user\n";
		break;
	default:
		isp_printf(2, "unsupport ivdc ioclt cmd\n");
		return -1;
	}
	if (r)
		isp_printf(2, err);
	return (long)(int)r;
}

#endif
