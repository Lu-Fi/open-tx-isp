/* T40 DPC: literal OEM chain (lit/dpc_lit.inc) vs readable driver/t40/tx_isp_t40_dpc.h.
 * ./dpc <bin>...   (run.sh dpc <bins>) */
#include "common.h"
/* dispatch keys of the literal chain (only their addresses are used) */
#define K(n) static void n(void) { abort(); }
K(tisp_dpc_all_reg_refresh) K(tisp_dpc_dn_params_refresh) K(tisp_dpc_intp) K(tisp_dpc_intp_reg_refresh)
K(tisp_dpc_main_deinit) K(tisp_dpc_main_init) K(tisp_dpc_noref_reg_cfg) K(tisp_dpc_params_refresh)
K(tisp_dpc_par_refresh) K(tisp_dpc_ref_reg_cfg) K(tisp_dpc_refresh) K(tisp_dpc_reg_trig) K(tisp_dpc_wdr_en)
#include "lit/dpc_lit.inc"
#include "../../../driver/t40/tx_isp_t40_dpc.h"

static uint8_t *bank_of(uint8_t *bin, int night) { return night ? BANK_NIGHT(bin) : BANK_DAY(bin); }

/* the same step on both sides; returns differences */
static unsigned long nwr;
enum { S_INIT, S_REFRESH, S_DN, S_WDR };
static unsigned step(int what, uint32_t arg, uint8_t *bank, const char *tag)
{
    struct wr *a, *b; unsigned na, nb, bad;
    tparamsN[0] = (uintptr_t)bank;
    switch (what) {
    case S_INIT: regtrace_dpc_init_lit(); break;
    case S_REFRESH: regtrace_dpc_refresh_lit(arg); break;
    case S_DN: regtrace_dpc_dn_refresh_lit(); break;
    case S_WDR: dpclit_tisp_dpc_wdr_en_lit(0, arg, 0, 0, DPCLIT_SP(dpclit_stack_ctl), NULL, 0);
                dpclit_tisp_dpc_refresh_lit(0, 0x30000, 0, 0, DPCLIT_SP(dpclit_stack_ctl), NULL, 0); break;
    }
    a = snap(&na);
    switch (what) {
    case S_INIT: t40_dpc_init(); break;
    case S_REFRESH: t40_dpc_refresh(arg); break;
    case S_DN: t40_dpc_dn_refresh(); break;
    case S_WDR: t40_dpc_select(&t40_dpc, arg); t40_dpc_gain_refresh(&t40_dpc, 0x30000); break;
    }
    b = snap(&nb);
    bad = cmp_logs(tag, a, na, b, nb); nwr += na;
    /* state: interpolated values and remembered gain */
    {
        uint8_t *li = (uint8_t *)(uintptr_t)*(uint32_t *)dpclit_main_dpc_intp;
        uint32_t lg = *(uint32_t *)dpclit_gain_old;
        unsigned i;
        for (i = 0; i < sizeof(t40_dpc.cur); i++)
            if (i != 13 && li[i] != t40_dpc.cur[i]) { if (bad < 8) printf("  %s: cur[%u] lit %02x c %02x\n", tag, i, li[i], t40_dpc.cur[i]); bad++; }
        if (lg != t40_dpc.gain_old) { printf("  %s: gain_old lit %x c %x\n", tag, lg, t40_dpc.gain_old); bad++; }
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
            0x3c000, 0x50000, 0x7ffff, 0x90000, 0x9ffff, 0xa0000, 0xa8000, 0xb0000, 0x200000, 0x7fffffff, 0x80000000u, 0xffff0000u, 0 };
        lcg_state = 1000 + k;
        for (int startnight = 0; startnight < 2; startnight++) {
            int night = startnight;
            bad += step(S_INIT, 0, bank_of(bin, night), "init"); nsteps++;
            for (i = 0; i < sizeof(fixed) / 4; i++) { snprintf(tag, sizeof(tag), "fixed %x", fixed[i]); bad += step(S_REFRESH, fixed[i], bank_of(bin, night), tag); nsteps++; }
            for (int round = 0; round < 6; round++) {
                uint32_t g = lcg() % (11u << 16);
                for (i = 0; i < 120; i++) {
                    /* mostly small moves around the threshold, sometimes jumps */
                    uint32_t r = lcg();
                    if (r % 7 == 0) g = lcg() % (11u << 16); else g += (int32_t)(lcg() % 1024) - 512;
                    snprintf(tag, sizeof(tag), "rnd %x", g);
                    bad += step(S_REFRESH, g, bank_of(bin, night), tag); nsteps++;
                }
                night = !night;
                bad += step(S_DN, 0, bank_of(bin, night), night ? "dn->night" : "dn->day"); nsteps++;
            }
            bad += step(S_WDR, 1, bank_of(bin, night), "wdr on"); nsteps++;
            bad += step(S_REFRESH, 0x45678, bank_of(bin, night), "wdr refresh"); nsteps++;
            bad += step(S_DN, 0, bank_of(bin, !night), "wdr dn"); nsteps++;
            bad += step(S_WDR, 0, bank_of(bin, night), "wdr off"); nsteps++;
            regtrace_dpc_lit_cleanup(); t40_dpc_cleanup();
            /* the OEM init does not reset wdr_en; keep both at linear for the next init */
        }
        printf("%s: %s\n", argv[k], bad ? "DIFF" : "bit-exact");
        total += bad;
    }
    printf("dpc: %d bins, %u steps, %lu register writes compared, %u differences\n", argc - 1, nsteps, nwr, total);
    return total != 0;
}
