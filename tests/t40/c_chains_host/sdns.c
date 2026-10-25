/* T40 SDNS: literal OEM chain (lit/sdns_lit.inc) vs readable driver/t40/tx_isp_t40_sdns.h.
 * ./sdns <bin>... (run.sh sdns <bins>) */
#include "common.h"
#define REGTRACE_KERNEL_TREE_BUILD 1
static void *private_vmalloc(size_t n) { return vmalloc(n); }
/* the literal chain keeps its buffers in the OEM globals; the C chain gets its own copies */
static uintptr_t lit_main_sdns, lit_main_sdns_comb, lit_main_sdns_intp, sec_sdns, sec_sdns_comb, sec_sdns_intp;
static uintptr_t main_sdns, main_sdns_comb, main_sdns_intp;	/* the C chain allocates these (host: arena below 4 GiB) */
#define main_sdns lit_main_sdns
#define regtrace_enable_sdns lit_enable_sdns
#define regtrace_sdns_dbg_init_ret lit_sdns_dbg_init_ret
#define main_sdns_comb lit_main_sdns_comb
#define main_sdns_intp lit_main_sdns_intp
#include "lit/sdns_lit.inc"
#undef main_sdns
#undef regtrace_enable_sdns
#undef regtrace_sdns_dbg_init_ret
#undef main_sdns_comb
#undef main_sdns_intp
#include "../../../driver/t40/tx_isp_t40_sdns.h"

static uint8_t *bank_of(uint8_t *bin, int night) { return night ? BANK_NIGHT(bin) : BANK_DAY(bin); }
static unsigned long nwr;
enum { S_INIT, S_REFRESH };
static unsigned step(int what, uint32_t ev, uint32_t thr, uint8_t *bank, const char *tag)
{
    struct wr *a, *b; unsigned na, nb, bad, i;
    tparamsN[0] = (uintptr_t)bank;
    if (what == S_INIT) lit_sdns_dbg_init_ret = regtrace_sdns_main_init_lit();
    else if (lit_main_sdns && !lit_sdns_dbg_init_ret) tisp_sdns_par_refresh_lit(0, ev, thr);
    a = snap(&na);
    if (what == S_INIT) t40_sdns_init(); else t40_sdns_refresh(ev, thr);
    b = snap(&nb);
    bad = cmp_logs(tag, a, na, b, nb); nwr += na;
    if (t40_sdns.ready && lit_main_sdns) {
        uint8_t *li = (uint8_t *)lit_main_sdns_intp, *lp = (uint8_t *)lit_main_sdns;
        for (i = 0; i < T40_SDNS_CUR_BYTES; i++)
            if (li[i] != t40_sdns.cur[i]) { if (bad < 8) printf("  %s: cur[%u] lit %02x c %02x\n", tag, i, li[i], t40_sdns.cur[i]); bad++; break; }
        if (memcmp(lp, t40_sdns.par, T40_SDNS_PAR_BYTES)) { printf("  %s: par differs\n", tag); bad++; }
        for (i = 0; i < T40_SDNS_FIELDS; i++)
            if (((uint32_t *)lit_main_sdns_comb)[i] - (uint32_t)lit_main_sdns != (uint32_t)(t40_sdns.f[i] - t40_sdns.par)) { printf("  %s: field %u\n", tag, i); bad++; break; }
        if (regtrace_sdns_gain_old[0] != t40_sdns.ev_old) { printf("  %s: ev_old lit %x c %x\n", tag, regtrace_sdns_gain_old[0], t40_sdns.ev_old); bad++; }
    }
    free(a); free(b);
    return bad;
}

static int chain_main(int argc, char **argv)
{
    unsigned total = 0, nsteps = 0;
    for (int k = 1; k < argc; k++) {
        uint8_t *bin = load_bin(argv[k]); unsigned bad = 0, i;
        char tag[64];
        static const uint32_t fixed[] = { 0, 1, 255, 256, 257, 0x8000, 0x10000, 0x100ff, 0x10100, 0x18000, 0x20000,
            0x3c000, 0x50000, 0x7ffff, 0x90000, 0x9ffff, 0xa0000, 0xa8000, 0xb0000, 0x200000, 0x7fffffff, 0 };
        int night = 0;
        lcg_state = 500 + k;
        bad += step(S_INIT, 0, 0, bank_of(bin, night), "init"); nsteps++;
        for (i = 0; i < sizeof(fixed) / 4; i++) { snprintf(tag, sizeof(tag), "fixed %x", fixed[i]); bad += step(S_REFRESH, fixed[i], 256, bank_of(bin, night), tag); nsteps++; }
        for (int round = 0; round < 8; round++) {
            uint32_t g = lcg() % (11u << 16);
            for (i = 0; i < 150; i++) {
                if (lcg() % 7 == 0) g = lcg() % (11u << 16); else g += (int32_t)(lcg() % 1024) - 512;
                g &= 0x7fffffff;
                snprintf(tag, sizeof(tag), "rnd %x", g);
                bad += step(S_REFRESH, g, (round & 1) ? 256 : 0x10000, bank_of(bin, night), tag); nsteps++;
            }
            night = !night;	/* the DN path re-runs the init on the new bank */
            bad += step(S_INIT, 0, 0, bank_of(bin, night), night ? "dn->night" : "dn->day"); nsteps++;
        }
        printf("%s: %s\n", argv[k], bad ? "DIFF" : "bit-exact");
        total += bad;
    }
    printf("sdns: %d bins, %u steps, %lu register writes compared, %u differences\n", argc - 1, nsteps, nwr, total);
    return total != 0;
}
