/* T40 BCSH literal chain on the host: day -> night (saturation 0) -> day must give the boot-day registers.
 * Build/run: ./run.sh sc830ai-t40.bin [ev]
 * The literal code keeps stack/heap addresses in uint32_t: heap and the worker stack live below 4 GiB. */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <errno.h>
#include <sys/mman.h>
#include <pthread.h>
typedef unsigned int uint;
#define module_param_named(a, b, c, d)
#define KERN_INFO ""
#define printk(...) 0
static uint8_t *arena; static size_t used;
static uintptr_t private_vmalloc(uintptr_t n) { void *p = arena + used; used += (n + 15) & ~15UL; assert(used < (64u << 20)); memset(p, 0, n); return (uintptr_t)p; }
static uint32_t regs[0x20000 / 4];
static int system_reg_write(uint32_t r, uint32_t v) { if (r < 0x20000) regs[r / 4] = v; return 0; }
static uint32_t system_reg_read(uint32_t r) { return r < 0x20000 ? regs[r / 4] : 0; }
static uint32_t tparamsN[2];
static int32_t fix_point_mult2_32(int32_t shift, int32_t left, int32_t right)
{   /* copy of the driver helper (tx_isp_t40_recovered.c, OEM 1.3.1 0x162d4) */
    uint32_t q = (uint32_t)shift & 31U, mask = 0xffffffffU >> ((0U - q) & 31U);
    uint32_t x = (uint32_t)left, y = (uint32_t)right, xi = x >> q, yi = y >> q, xf = x & mask, yf = y & mask;
    return (int32_t)(((xi * yi) << q) + xf * yi + xi * yf + ((xf * yf) >> q));
}
#define REGTRACE_LWLR(b, o) \
    ((uint32_t)*(const uint8_t *)(uintptr_t)((b) + (o)) | \
     ((uint32_t)*(const uint8_t *)(uintptr_t)((b) + (o) + 1) << 8) | \
     ((uint32_t)*(const uint8_t *)(uintptr_t)((b) + (o) + 2) << 16) | \
     ((uint32_t)*(const uint8_t *)(uintptr_t)((b) + (o) + 3) << 24))
/* CSC tables: copies of the driver's OEM .data tables (tx_isp_t40_recovered.c) */
static unsigned char __attribute__((aligned(4))) au16BT601LimitedOffsetYUVy[4] = {
    0xc0, 0x03, 0x40, 0x04, 
};
static unsigned char __attribute__((aligned(4))) au16BT601LimitedClip0[8] = {
    0x40, 0x00, 0xac, 0x03, 0x40, 0x00, 0xc0, 0x03, 
};
static unsigned char __attribute__((aligned(4))) au16BT601LimitedClip1[8] = {
    0x00, 0x00, 0x6c, 0x03, 0x40, 0x00, 0xc0, 0x03, 
};
static unsigned char __attribute__((aligned(4))) au16BT601LimitedClip2[8] = {
    0x40, 0x00, 0xac, 0x03, 0x40, 0x00, 0xc0, 0x03, 
};
static unsigned char __attribute__((aligned(4))) as32BT601LimitedMMatrix[36] = {
    0xcb, 0x41, 0x00, 0x00, 0x06, 0x81, 0x00, 0x00, 0x17, 0x19, 0x00, 0x00, 0x1d, 0xda, 0xff, 0xff, 
    0x81, 0xb5, 0xff, 0xff, 0x62, 0x70, 0x00, 0x00, 0x62, 0x70, 0x00, 0x00, 0xcb, 0xa1, 0xff, 0xff, 
    0xd3, 0xed, 0xff, 0xff, 
};
static unsigned char __attribute__((aligned(4))) as32BT601LimitedMinvMatrix[36] = {
    0x05, 0x2a, 0x01, 0x00, 0x8b, 0xff, 0xff, 0xff, 0x85, 0x98, 0x01, 0x00, 0x05, 0x2a, 0x01, 0x00, 
    0xca, 0x9b, 0xff, 0xff, 0xc0, 0x2f, 0xff, 0xff, 0x05, 0x2a, 0x01, 0x00, 0x90, 0x04, 0x02, 0x00, 
    0xae, 0xff, 0xff, 0xff, 
};
static unsigned char __attribute__((aligned(4))) au16BT601FullOffsetYUVy[4] = {
    0x00, 0x04, 0x00, 0x04, 
};
static unsigned char __attribute__((aligned(4))) au16BT601FullClip0[8] = {
    0x00, 0x00, 0xff, 0x03, 0x00, 0x00, 0xff, 0x03, 
};
static unsigned char __attribute__((aligned(4))) au16BT601FullClip1[8] = {
    0x00, 0x00, 0xff, 0x03, 0x00, 0x00, 0xff, 0x03, 
};
static unsigned char __attribute__((aligned(4))) au16BT601FullClip2[8] = {
    0x00, 0x00, 0xff, 0x03, 0x00, 0x00, 0xff, 0x03, 
};
static unsigned char __attribute__((aligned(4))) as32BT601FullMMatrix[36] = {
    0x8b, 0x4c, 0x00, 0x00, 0x46, 0x96, 0x00, 0x00, 0x2f, 0x1d, 0x00, 0x00, 0xbc, 0xd4, 0xff, 0xff, 
    0x44, 0xab, 0xff, 0xff, 0x00, 0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0xbc, 0x94, 0xff, 0xff, 
    0x44, 0xeb, 0xff, 0xff, 
};
static unsigned char __attribute__((aligned(4))) as32BT601FullMinvMatrix[36] = {
    0x00, 0x00, 0x01, 0x00, 0xc3, 0xff, 0xff, 0xff, 0xd5, 0x66, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 
    0x04, 0xa8, 0xff, 0xff, 0x2c, 0x49, 0xff, 0xff, 0x00, 0x00, 0x01, 0x00, 0xac, 0xc5, 0x01, 0x00, 
    0x41, 0x00, 0x00, 0x00, 
};
static unsigned char __attribute__((aligned(4))) au16BT709LimitedOffsetYUVy[4] = {
    0xc0, 0x03, 0x40, 0x04, 
};
static unsigned char __attribute__((aligned(4))) au16BT709LimitedClip0[8] = {
    0x40, 0x00, 0xac, 0x03, 0x40, 0x00, 0xc0, 0x03, 
};
static unsigned char __attribute__((aligned(4))) au16BT709LimitedClip1[8] = {
    0x00, 0x00, 0x6c, 0x03, 0x40, 0x00, 0xc0, 0x03, 
};
static unsigned char __attribute__((aligned(4))) au16BT709LimitedClip2[8] = {
    0x40, 0x00, 0xac, 0x03, 0x40, 0x00, 0xc0, 0x03, 
};
static unsigned char __attribute__((aligned(4))) as32BT709LimitedMMatrix[36] = {
    0xbf, 0x2e, 0x00, 0x00, 0x3c, 0x9d, 0x00, 0x00, 0xdf, 0x0f, 0x00, 0x00, 0x3f, 0xe6, 0xff, 0xff, 
    0x52, 0xa9, 0xff, 0xff, 0x6f, 0x70, 0x00, 0x00, 0x6f, 0x70, 0x00, 0x00, 0xe2, 0x99, 0xff, 0xff, 
    0xaf, 0xf5, 0xff, 0xff, 
};
static unsigned char __attribute__((aligned(4))) as32BT709LimitedMinvMatrix[36] = {
    0x17, 0x2a, 0x01, 0x00, 0x08, 0x00, 0x00, 0x00, 0xf4, 0xca, 0x01, 0x00, 0x17, 0x2a, 0x01, 0x00, 
    0x67, 0xc9, 0xff, 0xff, 0x90, 0x77, 0xff, 0xff, 0x17, 0x2a, 0x01, 0x00, 0xcb, 0x1c, 0x02, 0x00, 
    0xf0, 0xff, 0xff, 0xff, 
};
static unsigned char __attribute__((aligned(4))) au16BT709FullOffsetYUVy[4] = {
    0x00, 0x04, 0x00, 0x04, 
};
static unsigned char __attribute__((aligned(4))) au16BT709FullClip0[8] = {
    0x00, 0x00, 0xff, 0x03, 0x00, 0x00, 0xff, 0x03, 
};
static unsigned char __attribute__((aligned(4))) au16BT709FullClip1[8] = {
    0x00, 0x00, 0xff, 0x03, 0x00, 0x00, 0xff, 0x03, 
};
static unsigned char __attribute__((aligned(4))) au16BT709FullClip2[8] = {
    0x00, 0x00, 0xff, 0x03, 0x00, 0x00, 0xff, 0x03, 
};
static unsigned char __attribute__((aligned(4))) as32BT709FullMMatrix[36] = {
    0x6d, 0x36, 0x00, 0x00, 0x17, 0xb7, 0x00, 0x00, 0x7c, 0x12, 0x00, 0x00, 0xaa, 0xe2, 0xff, 0xff, 
    0x56, 0x9d, 0xff, 0xff, 0x00, 0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0xba, 0x8b, 0xff, 0xff, 
    0x46, 0xf4, 0xff, 0xff, 
};
static unsigned char __attribute__((aligned(4))) as32BT709FullMinvMatrix[36] = {
    0x00, 0x00, 0x01, 0x00, 0xf6, 0xff, 0xff, 0xff, 0x24, 0x93, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 
    0x0e, 0xd0, 0xff, 0xff, 0x29, 0x88, 0xff, 0xff, 0x00, 0x00, 0x01, 0x00, 0x09, 0xdb, 0x01, 0x00, 
    0x07, 0x00, 0x00, 0x00, 
};
/* OEM function symbols whose address the literal code loads (HI16/LO16 pairs) but never calls through */
static char tisp_bcsh_ct_interpolation, tisp_bcsh_ct_set_parameter, tisp_bcsh_ev_set_parameter, tisp_bcsh_ev_update_TransitParam, tisp_bcsh_para2reg, tisp_bcsh_reg2para, tisp_bcsh_StrenCal, tisp_bcsh_Tccm_Comp2Orig, tisp_bcsh_Tccm_RGB2YUV, tisp_bcsh_Tccm_RGBYUV, tisp_bcsh_Toffset_RGB2YUV, tisp_bcsh_Toffset_RGBYUV;
#include "../../../driver/t40/tx_isp_t40_bcsh_lit.inc"

static uint8_t *bin;
static void init_bank(int night, uint8_t sat)
{
    tparamsN[0] = (uint32_t)(uintptr_t)(bin + 0x18 + (night ? 0x18ff4 : 0));
    uint32_t ctx = (uint32_t)(uintptr_t)pstMainBcshOri_lit;

    /* the same order as regtrace_t40_refresh_daynight_pipeline: init, brightness, contrast, hue, saturation */
    regtrace_bcsh_main_init_lit(2);
    *(uint8_t *)(uintptr_t)pstMainBcshOri_lit[46] = 128; tisp_bcsh_ev_update_lit(ctx, 0);
    *(uint8_t *)(uintptr_t)pstMainBcshOri_lit[47] = 128; tisp_bcsh_ev_update_lit(ctx, 0);
    *(uint8_t *)(uintptr_t)pstMainBcshOri_lit[6] = 1;    /* regtrace_isp_m0_apply_hue(128) */
    *(uint8_t *)(uintptr_t)pstMainBcshOri_lit[25] = (128 * 120 + 255) >> 8;
    tisp_bcsh_ct_update_lit(ctx, 0);
    *(uint8_t *)(uintptr_t)pstMainBcshOri_lit[26] = 128;
    *(uint8_t *)(uintptr_t)pstMainBcshOri_lit[6] = 0;
    *(uint8_t *)(uintptr_t)pstMainBcshOri_lit[48] = sat;
    tisp_bcsh_ev_update_lit(ctx, 0);
}
static int run(int argc, char **argv)
{
    static uint32_t boot[0x100 / 4];
    unsigned o, diff = 0, cyc;
    FILE *f;
    arena = mmap(NULL, 64u << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    assert(arena != MAP_FAILED);
    bin = (uint8_t *)private_vmalloc(204800);
    f = fopen(argv[1], "rb"); assert(f && fread(bin, 1, 204800, f) == 204800); fclose(f);
    regtrace_bcsh_ev = argc > 2 ? strtoul(argv[2], 0, 0) : 286830;
    init_bank(0, 128);
    memcpy(boot, &regs[0x11000 / 4], sizeof(boot));
    for (cyc = 1; cyc <= 3; cyc++) {
        init_bank(1, 0);
        init_bank(0, 128);
        for (o = 0; o < 0x100; o += 4)
            if (regs[(0x11000 + o) / 4] != boot[o / 4]) {
                diff++;
                printf("  cycle %u %05x boot %08x now %08x\n", cyc, 0x11000 + o, boot[o / 4], regs[(0x11000 + o) / 4]);
            }
    }
    printf("BCSH day after day->night->day x3 (EV %u): %u words differ from the boot day -> %s\n",
           regtrace_bcsh_ev, diff, diff ? "FAIL" : "PASS");
    return diff ? 1 : 0;
}
static int g_argc; static char **g_argv;
static void *worker(void *a) { (void)a; return (void *)(intptr_t)run(g_argc, g_argv); }
int main(int argc, char **argv)
{
    pthread_attr_t at; pthread_t t; void *rv; size_t ss = 8u << 20;
    void *stk = mmap(NULL, ss, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (argc < 2) { fprintf(stderr, "usage: %s <sensor-t40.bin> [ev]\n", argv[0]); return 2; }
    assert(stk != MAP_FAILED);
    g_argc = argc; g_argv = argv;
    pthread_attr_init(&at); pthread_attr_setstack(&at, stk, ss);
    pthread_create(&t, &at, worker, NULL); pthread_join(t, &rv);
    return (int)(intptr_t)rv;
}
