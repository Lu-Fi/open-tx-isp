/* /dev/tisp two-opener test (#155/#157), runs in the QEMU T40 guest:
 *   mipsel-linux-gcc -static -O1 -o tisp2open tisp2open_qemu.c
 * A and B open, A closes, B keeps working (get_ae/get_awb); bad user pointers must give
 * -EFAULT without an oops; a child opens/closes in a loop while B does ioctls. */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
static unsigned char buf[0x5010 + 64];
int main(void)
{
	int a = open("/dev/tisp", O_RDWR), b = open("/dev/tisp", O_RDWR), r, fail = 0, i;
	if (a < 0 || b < 0) { printf("TISP2 open failed %d %d\n", a, b); return 2; }
	close(a);
	r = ioctl(b, 0x20007404, buf); printf("TISP2 get_ae after A closed: %d errno=%d\n", r, errno); fail |= r != 0;
	r = ioctl(b, 0x20007407, buf); printf("TISP2 get_awb after A closed: %d\n", r); fail |= r != 0;
	r = ioctl(b, 0x20007405, (void *)1); printf("TISP2 set_ae badptr: %d errno=%d\n", r, errno); fail |= r != -1 || errno != EFAULT;
	r = ioctl(b, 0x20007408, (void *)1); printf("TISP2 set_awb badptr: %d errno=%d\n", r, errno); fail |= r != -1 || errno != EFAULT;
	r = ioctl(b, 0x20007400, (void *)1); printf("TISP2 get badptr: %d errno=%d\n", r, errno);
	r = ioctl(b, 0x20007409, buf); printf("TISP2 bad nr: %d\n", r);
	if (fork() == 0) {
		for (i = 0; i < 300; i++) { int c = open("/dev/tisp", O_RDWR); if (c >= 0) { ioctl(c, 0x20007404, buf); close(c); } }
		_exit(0);
	}
	for (i = 0; i < 300; i++) { r = ioctl(b, 0x20007404, buf); if (r) { fail = 1; printf("TISP2 concurrent ioctl %d: %d\n", i, r); break; } }
	wait(NULL);
	close(b);
	printf("TISP2 RESULT %s\n", fail ? "FAIL" : "PASS");
	return fail;
}
