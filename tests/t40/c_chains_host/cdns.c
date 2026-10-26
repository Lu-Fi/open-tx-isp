/* T40 CDNS: literal OEM chain (lit/cdns_lit.inc) vs readable driver/t40/tx_isp_t40_cdns.h.
 * ./cdns <bin>... (run.sh cdns <bins>) */
#include "common.h"
#define REGTRACE_KERNEL_TREE_BUILD 1
static void *private_vmalloc(size_t n) { return vmalloc(n); }
static uint32_t regtrace_t40_force_bypass, regtrace_t40_quality_top40_final;
/* the OEM globals of the literal chain */
static uintptr_t main_cdns, main_cdns_comb, main_cdns_intp, sec_cdns, sec_cdns_comb, sec_cdns_intp;
#include "lit/cdns_lit.inc"
#include "../../../driver/t40/tx_isp_t40_cdns.h"

/* recovered-driver glue of the literal chain (tisp_cdns_refresh, dn_params_refresh,
 * param_array_set, cdns_remove, sec init) */
static void lit_refresh(uint32_t ch, uint32_t g)
{
    (void)tisp_cdns_par_refresh_lit(ch, g, lit_cdns_thresh);
    (void)tisp_cdns_all_reg_refresh_lit(ch, lit_cdns_gain_old[ch]);
}
static void lit_dn(uint32_t ch)
{
    (void)tisp_cdns_params_refresh_lit(ch);
    (void)tisp_cdns_wdr_en_lit(ch, lit_cdns_wdr_en_flags[ch]);
    (void)tisp_cdns_all_reg_refresh_lit(ch, lit_cdns_gain_old[ch]);
}
static void lit_set(uint32_t ch, const void *src)
{
    memcpy((void *)(ch ? sec_cdns : main_cdns), src, 683);
    (void)tisp_cdns_all_reg_refresh_lit(ch, lit_cdns_gain_old[ch]);
}
static void lit_remove(void)
{
    lit_cdns_gain_old[0] = lit_cdns_gain_old[1] = 0xffffffffU;
    lit_cdns_thresh = 256;
    lit_cdns_wdr_en_flags[0] = lit_cdns_wdr_en_flags[1] = 0;
}
static int lit_sec_init(void)
{
    if (!sec_cdns) {
        sec_cdns = (uintptr_t)kzalloc(683, 0);
        sec_cdns_comb = (uintptr_t)kzalloc(120, 0);
        sec_cdns_intp = (uintptr_t)kzalloc(32, 0);
    }
    if (tisp_cdns_params_refresh_lit(1))
        return -ENOENT;
    (void)tisp_cdns_wdr_en_lit(1, lit_cdns_wdr_en_flags[1]);
    lit_cdns_gain_old[1] = 0xffffffffU;
    (void)tisp_cdns_par_refresh_lit(1, 0x10000U, 0x10000U);
    lit_cdns_init_count++;
    return 0;
}

static uint8_t *bank_of(uint8_t *bin, int night) { return night ? BANK_NIGHT(bin) : BANK_DAY(bin); }
static unsigned long nwr;
enum { S_INIT, S_PAR, S_REFRESH, S_DN, S_WDR, S_SET, S_REMOVE };
static uint8_t set_src[683];

static unsigned step(int what, uint32_t ch, uint32_t ev, uint32_t thr, uint8_t *bank, const char *tag)
{
    struct wr *a, *b; unsigned na, nb, bad = 0, i;
    int lr = 0, cr = 0;
    tparamsN[ch] = (uintptr_t)bank;
    switch (what) {
    case S_INIT: lr = ch ? lit_sec_init() : lit_cdns_main_init_lit(); if (!ch) lit_cdns_dbg_init_ret = lr; break;
    case S_PAR: if ((ch ? sec_cdns : main_cdns) && !(ch == 0 && lit_cdns_dbg_init_ret)) tisp_cdns_par_refresh_lit(ch, ev, thr); break;
    case S_REFRESH: lit_refresh(ch, ev); break;
    case S_DN: lit_dn(ch); break;
    case S_WDR: tisp_cdns_wdr_en_lit(ch, ev); break;
    case S_SET: lit_set(ch, set_src); break;
    case S_REMOVE: lit_remove(); break;
    }
    a = snap(&na);
    switch (what) {
    case S_INIT: cr = t40_cdns_init(ch); if (!ch) regtrace_cdns_dbg_init_ret = cr; break;
    case S_PAR: if (!(ch == 0 && regtrace_cdns_dbg_init_ret)) t40_cdns_par_refresh(ch, ev, thr); break;
    case S_REFRESH: t40_cdns_refresh(ch, ev); break;
    case S_DN: t40_cdns_dn_refresh(ch); break;
    case S_WDR: t40_cdns_set_wdr(ch, ev); break;
    case S_SET: t40_cdns_param_set(ch, set_src); break;
    case S_REMOVE: t40_cdns_remove(); break;
    }
    b = snap(&nb);
    if (lr != cr) { printf("  %s: init ret lit %d c %d\n", tag, lr, cr); bad++; }
    bad += cmp_logs(tag, a, na, b, nb); nwr += na;
    {
        uintptr_t par = ch ? sec_cdns : main_cdns, cur = ch ? sec_cdns_intp : main_cdns_intp, comb = ch ? sec_cdns_comb : main_cdns_comb;
        struct t40_cdns_chan *c = &t40_cdns[ch];
        if (par && c->loaded) {
            if (memcmp((void *)cur, c->cur, 32)) { printf("  %s: cur differs\n", tag); bad++; }
            if (memcmp((void *)par, c->par, 683)) { printf("  %s: par differs\n", tag); bad++; }
            for (i = 0; i < 30 && ((uint32_t *)comb)[i]; i++)
                if (((uint32_t *)comb)[i] - (uint32_t)par != (uint32_t)(t40_cdns_field(c, i) - c->par)) { printf("  %s: field %u\n", tag, i); bad++; break; }
        }
        if (lit_cdns_gain_old[ch] != c->gain_old) { printf("  %s: gain_old lit %x c %x\n", tag, lit_cdns_gain_old[ch], c->gain_old); bad++; }
        if (lit_cdns_wdr_en_flags[ch] != c->wdr) { printf("  %s: wdr lit %x c %x\n", tag, lit_cdns_wdr_en_flags[ch], c->wdr); bad++; }
        if (lit_cdns_thresh != regtrace_cdns_thresh || lit_cdns_refresh_count != regtrace_cdns_refresh_count ||
            lit_cdns_skip_count != regtrace_cdns_skip_count || lit_cdns_gain_now != regtrace_cdns_gain_now ||
            lit_cdns_init_count != regtrace_cdns_init_count) { printf("  %s: counters/thresh differ\n", tag); bad++; }
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
        static const uint32_t fixed[] = { 0, 1, 255, 256, 257, 0x8000, 0xffff, 0x10000, 0x100ff, 0x10100, 0x18000, 0x20000,
            0x3c000, 0x50000, 0x7ffff, 0x90000, 0x9ffff, 0xa0000, 0xa8000, 0xb0000, 0x200000, 0x7fffffff, 0 };
        lcg_state = 700 + k;
        if (!main_cdns) {	/* the WDR switch may come before the first init */
            main_cdns = (uintptr_t)kzalloc(683, 0); main_cdns_comb = (uintptr_t)kzalloc(120, 0); main_cdns_intp = (uintptr_t)kzalloc(32, 0);
            sec_cdns = (uintptr_t)kzalloc(683, 0); sec_cdns_comb = (uintptr_t)kzalloc(120, 0); sec_cdns_intp = (uintptr_t)kzalloc(32, 0);
        }
        for (uint32_t ch = 0; ch < 2; ch++) {
            int night = 0;
            for (int wdr = 0; wdr < 2; wdr++) {
                snprintf(tag, sizeof(tag), "ch%u wdr%d pre", ch, wdr);
                bad += step(S_WDR, ch, wdr ? (lcg() | 2) : 0, 0, bank_of(bin, night), tag); nsteps++;
                snprintf(tag, sizeof(tag), "ch%u wdr%d init", ch, wdr);
                bad += step(S_INIT, ch, 0, 0, bank_of(bin, night), tag); nsteps++;
                for (i = 0; i < sizeof(fixed) / 4; i++) {
                    snprintf(tag, sizeof(tag), "ch%u fixed %x", ch, fixed[i]);
                    bad += step(S_PAR, ch, fixed[i], 256, bank_of(bin, night), tag); nsteps++;
                }
                for (int round = 0; round < 8; round++) {
                    uint32_t g = lcg() % (11u << 16);
                    for (i = 0; i < 120; i++) {
                        unsigned r = lcg() % 64;
                        if (lcg() % 7 == 0) g = lcg() % (12u << 16); else g += (int32_t)(lcg() % 0x20000) - 0x10000;
                        g &= 0x7fffffff;
                        snprintf(tag, sizeof(tag), "ch%u r%d #%u %x", ch, round, i, g);
                        if (r == 0) {	/* tuning set: random or a bank section */
                            for (unsigned j = 0; j < sizeof(set_src); j++) set_src[j] = lcg() >> 7;
                            if (lcg() & 1) memcpy(set_src, bank_of(bin, !night) + T40_CDNS_PAR_OFFSET, sizeof(set_src));
                            bad += step(S_SET, ch, 0, 0, bank_of(bin, night), tag);
                        } else if (r == 1) {
                            bad += step(S_DN, ch, 0, 0, bank_of(bin, night), tag);
                        } else if (r == 2) {	/* WDR switch without init: only the flag+half */
                            bad += step(S_WDR, ch, lcg() % 4, 0, bank_of(bin, night), tag);
                        } else if (r < 12) {
                            bad += step(S_REFRESH, ch, g, 0, bank_of(bin, night), tag);
                        } else {
                            bad += step(S_PAR, ch, g, (round & 1) ? 256 : (round & 2) ? 0x10000 : (lcg() % 0x30000), bank_of(bin, night), tag);
                        }
                        nsteps++;
                    }
                    if (round == 5) {	/* cdns_remove, then a re-init */
                        bad += step(S_REMOVE, ch, 0, 0, bank_of(bin, night), "remove"); nsteps++;
                        bad += step(S_REFRESH, ch, g, 0, bank_of(bin, night), "after remove"); nsteps++;
                    }
                    night = !night;	/* the DN path re-runs the init on the new bank */
                    bad += step(S_INIT, ch, 0, 0, bank_of(bin, night), night ? "dn->night" : "dn->day"); nsteps++;
                }
            }
        }
        printf("%s: %s\n", argv[k], bad ? "DIFF" : "bit-exact");
        total += bad;
    }
    printf("cdns: %d bins, %u steps, %lu register writes compared, %u differences\n", argc - 1, nsteps, nwr, total);
    return total != 0;
}
