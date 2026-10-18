/* aeexp_test: raw GetAeExpList / SetAeExpList against the T40 ISP tuning node.
 * Build: mipsel-linux-gcc -static -Os -o aeexp_test aeexp_test.c
 * Usage: aeexp_test [get|set|both] [/dev/isp-m0]
 * Wire: ioctl 0xc0105435 on /dev/isp-m0 with {vinum, dir(0=set,1=get), id=0x8000099, value=user ptr}.
 * User struct: u32 mode; 16 entries x {u32 w0..w4}; kernel stores w0..w3 into ladder slot i (16 B). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/ioctl.h>

#define TUNING_IOC 0xc0105435U
#define ID_AE_EXPLIST 0x8000099U
#define N 16

struct tuning_ctrl { uint32_t vinum, dir, id; int32_t value; };
struct explist { uint32_t mode; struct { uint32_t w[5]; } e[N]; };

static int do_ioctl(int fd, uint32_t dir, void *p)
{
    struct tuning_ctrl c = { 0, dir, ID_AE_EXPLIST, (int32_t)(uintptr_t)p };
    int r = ioctl(fd, TUNING_IOC, &c);
    printf("ioctl dir=%u ret=%d\n", dir, r);
    return r;
}

static void dump(const char *tag, struct explist *l)
{
    int i;
    printf("%s mode=%u\n", tag, l->mode);
    for (i = 0; i < N; i++)
        printf("  [%2d] %u %u %u %u %u\n", i, l->e[i].w[0], l->e[i].w[1], l->e[i].w[2], l->e[i].w[3], l->e[i].w[4]);
}

int main(int argc, char **argv)
{
    const char *what = argc > 1 ? argv[1] : "both";
    const char *dev = argc > 2 ? argv[2] : "/dev/isp-m0";
    struct explist l;
    int fd = open(dev, O_RDWR), i;
    if (fd < 0) { perror(dev); return 1; }

    if (strcmp(what, "set")) {
        memset(&l, 0, sizeof(l));
        if (do_ioctl(fd, 1, &l) == 0) dump("GET", &l);
    }
    if (strcmp(what, "get")) {
        /* sc830ai: IT 3..2275 lines, again 0..327680 (log2 16.16, 32x).
         * Ladder guess: w0 = EV key ascending (IT * 2^(again/65536)), w1 = IT, w2 = again, w3 = dgain.
         * First 10 steps: IT only at again 0; last 6 steps: IT max, again rising. */
        static const uint32_t it[N]    = { 3, 8, 16, 32, 64, 128, 256, 512, 1024, 2275, 2275, 2275, 2275, 2275, 2275, 2275 };
        static const uint32_t again[N] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 65536, 131072, 196608, 262144, 294912, 327680 };
        memset(&l, 0, sizeof(l));
        l.mode = 1;
        for (i = 0; i < N; i++) {
            l.e[i].w[0] = it[i] << (again[i] >> 16);
            l.e[i].w[1] = it[i];
            l.e[i].w[2] = again[i];
            l.e[i].w[3] = 0;
        }
        dump("SET", &l);
        do_ioctl(fd, 0, &l);
        if (!strcmp(what, "both")) {
            memset(&l, 0, sizeof(l));
            if (do_ioctl(fd, 1, &l) == 0) dump("GET-after", &l);
        }
    }
    close(fd);
    return 0;
}
