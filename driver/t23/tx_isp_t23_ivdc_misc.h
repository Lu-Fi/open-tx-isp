/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T23 /dev/misc-ivdc ioctl as the vendor module has it (openimp #767):
 * tx-isp-t23.ko (ISVP T23 1.3.0 SDK) ivdc_misc_unlocked_ioctl @0x1b660.
 * Written from the decompiled flow, no vendor code copied.
 *
 *   0xc004543f  GET_DIRECT    copy direct_mode to user (4 bytes)
 *   0xc0045440  SET_DIRECT    copy direct_mode from user
 *   0xc0045441  SET_PADDR     copy ivdc_mem_paddr from user (the DDR ring
 *                             libimp allocated in rmem)
 *   0xc0045442  GET_MEM_LINE  copy ivdc_mem_line to user
 *   0x2000543d  IVDC register 0x24 = 0x388   (vendor: H.264 reader)
 *   0x2000543e  IVDC register 0x24 = 0x3288  (vendor: JPEG reader)
 *   other       "unsupport ivdc ioclt cmd", -1
 *
 * Vendor quirk kept: a failed copy prints and returns the bytes not copied
 * (positive), not -EFAULT.  The copies, the register write and the print
 * are hooks so tests/t23_ivmisc_host.c runs the same code on the host.
 */
#ifndef TX_ISP_T23_IVDC_MISC_H
#define TX_ISP_T23_IVDC_MISC_H

#define T23_IVDC_IOC_MODE_H264		0x2000543dU
#define T23_IVDC_IOC_MODE_JPEG		0x2000543eU
#define T23_IVDC_IOC_GET_DIRECT		0xc004543fU
#define T23_IVDC_IOC_SET_DIRECT		0xc0045440U
#define T23_IVDC_IOC_SET_PADDR		0xc0045441U
#define T23_IVDC_IOC_GET_MEM_LINE	0xc0045442U

#define T23_IVDC_REG_MODE		0x24U
#define T23_IVDC_MODE_H264		0x388U
#define T23_IVDC_MODE_JPEG		0x3288U

struct t23_ivdc_misc_state {
	int *direct_mode;
	int *mem_line;
	unsigned int *mem_paddr;
};

struct t23_ivdc_misc_ops {
	/* bytes not copied, as copy_{from,to}_user */
	unsigned long (*from_user)(void *dst, unsigned long user, unsigned long n);
	unsigned long (*to_user)(unsigned long user, const void *src, unsigned long n);
	void (*reg_write)(unsigned int off, unsigned int val);
	void (*print)(const char *msg);
};

static inline long t23_ivdc_misc_ioctl(const struct t23_ivdc_misc_state *s,
				       const struct t23_ivdc_misc_ops *o,
				       unsigned int cmd, unsigned long arg)
{
	const char *err;
	unsigned long r;

	switch (cmd) {
	case T23_IVDC_IOC_GET_DIRECT:
		r = o->to_user(arg, s->direct_mode, 4);
		err = "Get direct_mode err! Failed to copy to user\n";
		break;
	case T23_IVDC_IOC_SET_DIRECT:
		r = o->from_user(s->direct_mode, arg, 4);
		err = "Set direct_mode err! Failed to copy form user\n";
		break;
	case T23_IVDC_IOC_SET_PADDR:
		r = o->from_user(s->mem_paddr, arg, 4);
		err = "Set ivdc_mem_paddr err! Failed to copy from user\n";
		break;
	case T23_IVDC_IOC_GET_MEM_LINE:
		r = o->to_user(arg, s->mem_line, 4);
		err = "Get ivdc_mem_line err! Failed to copy to user\n";
		break;
	case T23_IVDC_IOC_MODE_H264:
		o->reg_write(T23_IVDC_REG_MODE, T23_IVDC_MODE_H264);
		return 0;
	case T23_IVDC_IOC_MODE_JPEG:
		o->reg_write(T23_IVDC_REG_MODE, T23_IVDC_MODE_JPEG);
		return 0;
	default:
		o->print("unsupport ivdc ioclt cmd\n");
		return -1;
	}
	if (r)
		o->print(err);
	return (long)r;
}

#endif
