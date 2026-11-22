/* Host test of the T23 /dev/misc-ivdc ioctl (driver/t23/tx_isp_t23_ivdc_misc.h,
 * openimp #767) against the vendor flow of tx-isp-t23.ko ivdc_misc_unlocked_ioctl
 * @0x1b660 (ISVP T23 1.3.0): command numbers, copied words, register 0x24
 * values, failed-copy and unknown-command results. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../driver/t23/tx_isp_t23_ivdc_misc.h"

static int fail_copy;
static unsigned int reg_off = ~0u, reg_val;
static int prints;
static unsigned char user[16];

static unsigned long from_user(void *dst, unsigned long u, unsigned long n)
{
	if (fail_copy)
		return n;
	memcpy(dst, (const void *)u, n);
	return 0;
}

static unsigned long to_user(unsigned long u, const void *src, unsigned long n)
{
	if (fail_copy)
		return n;
	memcpy((void *)u, src, n);
	return 0;
}

static void reg_write(unsigned int off, unsigned int val) { reg_off = off; reg_val = val; }
static void print(const char *m) { (void)m; prints++; }

int main(void)
{
	int direct = 1, line = 300;
	unsigned int paddr = 0;
	const struct t23_ivdc_misc_state st = { &direct, &line, &paddr };
	const struct t23_ivdc_misc_ops ops = { from_user, to_user, reg_write, print };
	unsigned long u = (unsigned long)user;
	int v;

	assert(t23_ivdc_misc_ioctl(&st, &ops, 0xc004543fu, u) == 0);
	memcpy(&v, user, 4); assert(v == 1);
	assert(t23_ivdc_misc_ioctl(&st, &ops, 0xc0045442u, u) == 0);
	memcpy(&v, user, 4); assert(v == 300);
	v = 0x01a00000; memcpy(user, &v, 4);
	assert(t23_ivdc_misc_ioctl(&st, &ops, 0xc0045441u, u) == 0 && paddr == 0x01a00000u);
	v = 0; memcpy(user, &v, 4);
	assert(t23_ivdc_misc_ioctl(&st, &ops, 0xc0045440u, u) == 0 && direct == 0);
	assert(t23_ivdc_misc_ioctl(&st, &ops, 0x2000543du, 0) == 0 && reg_off == 0x24 && reg_val == 0x388);
	assert(t23_ivdc_misc_ioctl(&st, &ops, 0x2000543eu, 0) == 0 && reg_off == 0x24 && reg_val == 0x3288);
	/* vendor: a failed copy returns the bytes left, after one print */
	fail_copy = 1; prints = 0;
	assert(t23_ivdc_misc_ioctl(&st, &ops, 0xc004543fu, u) == 4 && prints == 1);
	fail_copy = 0;
	/* unknown command, also the T32 numbers: -1 */
	assert(t23_ivdc_misc_ioctl(&st, &ops, 0xc0045471u, u) == -1 && prints == 2);
	assert(t23_ivdc_misc_ioctl(&st, &ops, 0x2000543cu, u) == -1);
	printf("t23_ivmisc_host: ok\n");
	return 0;
}
