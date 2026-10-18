#define _GNU_SOURCE
#include <sys/mman.h>
#include <assert.h>
#include <stdlib.h>
#include "or/hostshim.h"
uint32_t tparamsN[2]; uint8_t tispinfo[104];
/* oracle */
uint32_t or_call_tisp_msca_Shd_ctrl(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t tisp_msca_Shd_ctrl(uint32_t, uint32_t);
uint32_t or_call_update_isp_osd(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
uint8_t *or_sym(const char *n);
/* readable */
int32_t update_isp_osd(void);
void *rd_sym(const char *n);

static uint8_t *arena; static size_t used;
void *malloc32(size_t n) { void *p = arena + used; used += (n + 15) & ~15UL; memset(p, 0, n); return p; }
uint32_t host_a32(const void *p) { uintptr_t v = (uintptr_t)p; if (v >> 32) abort(); return (uint32_t)v; }
#define MAXW 4096
struct w { uint32_t r, v; char k; };
static struct w logs[2][MAXW]; static int nlog[2]; static int side;
static uint32_t regs[2][0x20000];
int host_reg_write(uint32_t r, uint32_t v) { if (nlog[side] < MAXW) { logs[side][nlog[side]].r = r; logs[side][nlog[side]].v = v; logs[side][nlog[side]].k = 'W'; } nlog[side]++; regs[side][(r & 0x7ffff) >> 2] = v; return 0; }
static uint32_t reg_read(uint32_t r) { uint32_t v = regs[side][(r & 0x7ffff) >> 2]; if (nlog[side] < MAXW) { logs[side][nlog[side]].r = r; logs[side][nlog[side]].v = v; logs[side][nlog[side]].k = 'R'; } nlog[side]++; return v; }
static unsigned bad;
void host_bad_call(uint32_t fn) { bad++; fprintf(stderr, "oracle bad call %x\n", fn); }
uint32_t host_ext(const char *n, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t sp, uint32_t *v1)
{
    (void)a2; (void)a3; (void)sp; (void)v1;
    if (!strcmp(n, "system_reg_write")) return host_reg_write(a0, a1);
    if (!strcmp(n, "system_reg_read")) return reg_read(a0);
    if (!strcmp(n, "isp_printf") || !strcmp(n, "__private_spin_lock_irqsave") || !strcmp(n, "private_spin_unlock_irqrestore")) return 0;
    fprintf(stderr, "oracle unhandled %s\n", n); bad++; return 0;
}
/* readable-side externals */
int32_t system_reg_write(uint32_t a0, uint32_t a1) { return host_reg_write(a0, a1); }
int32_t system_reg_read(uint32_t a0) { return (int32_t)reg_read(a0); }
int32_t isp_printf(uint32_t level, const char *fmt, ...) { (void)level; (void)fmt; return 0; }
int32_t __private_spin_lock_irqsave() { return 0; }
int32_t private_spin_unlock_irqrestore() { return 0; }

static uint8_t stk[16384] __attribute__((aligned(8)));
#define CORE_SZ 0x2000
static uint8_t *core[2], *sub[2];
static void setup(unsigned seed, uint32_t idx, uint32_t cnt, uint32_t sta)
{
    for (int s = 0; s < 2; s++) {
        srand(seed);
        for (int i = 0; i < CORE_SZ; i++) sub[s][i] = 0;
        memset(core[s], 0, CORE_SZ);
        *(uint32_t *)(core[s] + 268) = host_a32(sub[s]);
        *(uint32_t *)(sub[s] + 692) = idx;
        for (int k = 0; k < 3; k++) *(uint32_t *)(sub[s] + 656 + 12 * k) = cnt;
        memset(regs[s], 0, sizeof(regs[s]));
        nlog[s] = 0;
    }
    *(uint32_t *)or_sym("ispcore_sd") = host_a32(core[0]);
    *(uint32_t *)rd_sym("ispcore_sd") = host_a32(core[1]);
    memset(or_sym("mscaler_s0"), 0, 0x72c); memset(rd_sym("mscaler_s0"), 0, 0x72c);
    (void)sta;
}
static int compare(const char *t)
{
    int d = 0, n = nlog[0] > nlog[1] ? nlog[0] : nlog[1];
    for (int i = 0; i < n && i < MAXW; i++) {
        struct w *a = &logs[0][i], *b = &logs[1][i];
        if (i >= nlog[0] || i >= nlog[1] || a->r != b->r || a->v != b->v || a->k != b->k) {
            if (d < 6) printf("  %s #%d oem %c %05x=%08x | ours %c %05x=%08x\n", t, i, i < nlog[0] ? a->k : '-', a->r, a->v, i < nlog[1] ? b->k : '-', b->r, b->v);
            d++;
        }
    }
    if (memcmp(sub[0], sub[1], CORE_SZ)) { for (int i = 0; i < CORE_SZ; i += 4) if (*(uint32_t *)(sub[0] + i) != *(uint32_t *)(sub[1] + i)) { printf("  %s sub+%d oem=%u ours=%u\n", t, i, *(uint32_t *)(sub[0] + i), *(uint32_t *)(sub[1] + i)); d++; } }
    return d;
}
int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    arena = mmap(NULL, 16u << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    for (int s = 0; s < 2; s++) { core[s] = malloc32(CORE_SZ); sub[s] = malloc32(CORE_SZ); }
    uint32_t sp = host_a32(stk + sizeof(stk) - 256);
    int total = 0;
    for (uint32_t idx = 0; idx < 3; idx++) for (uint32_t cnt = 0; cnt < 2; cnt++) {
        char t[64]; sprintf(t, "update_isp_osd idx=%u cnt=%u", idx, cnt);
        setup(1, idx, cnt, 0);
        side = 0; uint32_t ra = or_call_update_isp_osd(0, 0, 0, 0, sp);
        side = 1; int32_t rb = update_isp_osd();
        int d = compare(t) + (ra != (uint32_t)rb);
        printf("%s: oem ret=%u ops=%d | ours ret=%d ops=%d -> %d diffs\n", t, ra, nlog[0], rb, nlog[1], d);
        total += d;
    }
    for (uint32_t ch = 0; ch < 2; ch++) for (uint32_t mode = 0; mode < 4; mode++) {
        char t[64]; sprintf(t, "Shd_ctrl ch=%u mode=%u", ch, mode);
        setup(2, 0, 1, 0);
        for (int k = 0; k < 0x72c; k++) { or_sym(ch ? "mscaler_s1" : "mscaler_s0")[k] = (uint8_t)(k * 7 + 3); ((uint8_t *)rd_sym(ch ? "mscaler_s1" : "mscaler_s0"))[k] = (uint8_t)(k * 7 + 3); }
        side = 0; uint32_t ra = or_call_tisp_msca_Shd_ctrl(ch, mode, 0, 0, sp);
        side = 1; int32_t rb = tisp_msca_Shd_ctrl(ch, mode);
        int d = compare(t) + (mode < 3 && ra != (uint32_t)rb) + memcmp(or_sym(ch ? "mscaler_s1" : "mscaler_s0"), rd_sym(ch ? "mscaler_s1" : "mscaler_s0"), 0x72c) != 0;
        printf("%s: oem ret=%u ops=%d | ours ret=%d ops=%d -> %d diffs\n", t, ra, nlog[0], rb, nlog[1], d);
        total += d;
    }
    printf("TOTAL diffs=%d bad=%u\n", total, bad);
    return 0;
}
