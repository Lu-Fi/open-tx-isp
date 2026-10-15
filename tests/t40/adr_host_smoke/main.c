#define _GNU_SOURCE
#include <sys/mman.h>
#include <assert.h>
#include "hostshim.h"
uint32_t tparamsN[2];
uint8_t tispinfo[104] __attribute__((aligned(8)));
uint32_t adrh_call_tisp_adr_main_init(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t adrh_call_tisp_adr_dn_params_refresh(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t adrh_call_tisp_adr_main_process(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t adrh_call_tisp_adr_main_deinit(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
uint8_t *adrh_sym(const char *n);
static uint8_t *arena; static size_t used;
void *malloc32(size_t n) { void *p = arena + used; used += (n + 15) & ~15UL; assert(used < (128u << 20)); return p; }
uint32_t host_a32(const void *p) { uintptr_t v = (uintptr_t)p; if (v >> 32) { fprintf(stderr, "ptr>4G %p\n", p); abort(); } return (uint32_t)v; }
static unsigned nw, bad, irqset, evset, nalloc; static uint32_t regs[0x40000];
int host_reg_write(uint32_t r, uint32_t v) { nw++; if (r < sizeof(regs)/4*4) regs[r/4] = v; return 0; }
void host_bad_call(uint32_t fn) { bad++; fprintf(stderr, "bad call %x\n", fn); }
uint32_t host_ext(const char *n, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t sp, uint32_t *v1)
{
    (void)sp;
    if (!strcmp(n, "system_reg_write")) return host_reg_write(a0, a1);
    if (!strcmp(n, "system_reg_read")) return a0 < 0x100000 ? regs[a0/4] : 0;
    if (!strcmp(n, "memcpy")) { memcpy((void *)(uintptr_t)a0, (void *)(uintptr_t)a1, a2); return a0; }
    if (!strcmp(n, "memset")) { memset((void *)(uintptr_t)a0, (int)a1, a2); return a0; }
    if (!strcmp(n, "private_vmalloc")) { nalloc++; void *p = malloc32(a0); memset(p, 0, a0); return A32(p); }
    if (!strcmp(n, "private_vfree") || !strcmp(n, "private_raw_mutex_init") || !strcmp(n, "private_mutex_lock") || !strcmp(n, "private_mutex_unlock") || !strcmp(n, "isp_printf") || !strcmp(n, "private_dma_cache_sync")) return 0;
    if (!strcmp(n, "system_irq_func_set")) { irqset++; return 0; }
    if (!strcmp(n, "tisp_event_set_cb")) { evset++; return 0; }
    if (!strcmp(n, "tisp_event_push")) return 0;
    if (!strcmp(n, "__ashldi3")) { uint64_t v = (((uint64_t)a1 << 32) | a0) << (a2 & 63); *v1 = v >> 32; return (uint32_t)v; }
    if (!strcmp(n, "__lshrdi3")) { uint64_t v = (((uint64_t)a1 << 32) | a0) >> (a2 & 63); *v1 = v >> 32; return (uint32_t)v; }
    if (!strcmp(n, "div64_u64")) { uint64_t d = ((uint64_t)a3 << 32) | a2; uint64_t v = d ? ((((uint64_t)a1 << 32) | a0) / d) : 0; *v1 = v >> 32; return (uint32_t)v; }
    fprintf(stderr, "unhandled %s\n", n); bad++; return 0;
}
static uint8_t stk[16384] __attribute__((aligned(8)));
#define SP A32(stk + sizeof(stk) - 256)
int main(int argc, char **argv)
{
    arena = mmap(NULL, 128u << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0); assert(arena != MAP_FAILED);
    FILE *f = fopen(argv[1], "rb"); uint8_t *bin = malloc32(204800); assert(fread(bin, 1, 204800, f) == 204800); fclose(f);
    tparamsN[0] = A32(bin + 0x18);
    uint32_t r = adrh_call_tisp_adr_main_init(3840, 2160, 0, 0, SP);
    setvbuf(stdout, NULL, _IONBF, 0); printf("adr init 3840x2160: ret=%d writes=%u allocs=%u irq_set=%u event_set=%u bad=%u\n", (int)r, nw, nalloc, irqset, evset, bad);
    printf("  gauss[0..5]:"); for (int i = 0; i < 6; i++) printf(" %d", ((int32_t *)adrh_sym("adr_gauss_local"))[i]); printf("\n");
    unsigned w0 = nw;
    tparamsN[0] = A32(bin + 0x18 + 0x18ff4);
    r = adrh_call_tisp_adr_dn_params_refresh(0, 0, 0, 0, SP);
    printf("dn refresh night: ret=%d writes=%u bad=%u\n", (int)r, nw - w0, bad);
    w0 = nw; tparamsN[0] = A32(bin + 0x18);
    r = adrh_call_tisp_adr_dn_params_refresh(0, 0, 0, 0, SP);
    printf("dn refresh day: ret=%d writes=%u bad=%u\n", (int)r, nw - w0, bad);
    w0 = nw;
    for (int i = 0; i < 20; i++) r = adrh_call_tisp_adr_main_process(0, 0, 0, 0, SP);
    printf("process x20 (zero stats): ret=%d writes=%u bad=%u\n", (int)r, nw - w0, bad);
    r = adrh_call_tisp_adr_main_deinit(0, 0, 0, 0, SP);
    printf("deinit ret=%d bad=%u\n", (int)r, bad);
    return bad != 0;
}
