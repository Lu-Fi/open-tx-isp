/* T40 LCE: literal OEM chain (lit/lce_lit.inc) vs readable driver/t40/tx_isp_t40_lce.h.
 * Per bin: init at several resolutions, frames of synthetic statistics through the IRQ
 * and event path, total gain changes, day/night bank switches, WDR light lock.
 * ./lce <bin>... (run.sh lce <bins>) */
#include "common.h"
static unsigned char __attribute__((aligned(4))) wdr_lce_para[12];
void lshrdi3_host(void) __asm__("__lshrdi3");
void lshrdi3_host(void) { abort(); }
/* IRQ / event plumbing */
static int side; static uintptr_t irq_cbs[2][64], ev_cbs[2][2][32];
#define irq_cb irq_cbs[side]
#define ev_cb ev_cbs[side]
static unsigned ev_pushed, ev_type;
static int32_t system_irq_func_set(int32_t irq, int32_t fn) { irq_cb[irq & 63] = (uint32_t)fn; return 0; }
static int32_t tisp_event_set_cb(int32_t ch, int32_t ev, int32_t fn) { ev_cb[ch & 1][ev & 31] = (uint32_t)fn; return 0; }
static int64_t tisp_event_push(uint32_t ch, uintptr_t a1) { ev_pushed++; ev_type = ((uint32_t *)a1)[2]; (void)ch; return 0; }
static int isp_printf(int l, const char *f, ...) { (void)l; (void)f; return 0; }
static void vfree_host(const void *p) { (void)p; }
#define private_vfree vfree_host
static void *private_vmalloc(size_t n) { return vmalloc(n); }
#define K(n) static void n(void) { abort(); }
K(Tisp_lce_soft) K(get_distance_1dim) K(lce_16bit_data_converge) K(lce_head_tail_search) K(lce_hist_filter_and_judge)
K(lce_hist_method) K(lce_light_lock_adjust_hist) K(lce_pdf_to_cdf) K(lce_self_light_correct) K(lce_std_hist_transform)
K(lce_wdr_light_lock) K(tisp_lce_awdr_to_used) K(tisp_lce_clear_stat_ram) K(tisp_lce_clr_ram0) K(tisp_lce_clr_ram1)
K(tisp_lce_curve_init_default) K(tisp_lce_deinit) K(tisp_lce_dn_params_refresh) K(tisp_lce_get_data) K(tisp_lce_init)
K(tisp_lce_main_interrupt_static) K(tisp_lce_main_process) K(tisp_lce_seco_interrupt_static) K(tisp_lce_seco_process)
K(tisp_lce_tgain_interp_strength) K(tisp_lce_tgain_update) K(tisp_lce_tool_to_awdr) K(tisp_lce_write_all_reg)
#include "lit/lce_lit.inc"
#include "../../../driver/t40/tx_isp_t40_lce.h"

/* statistics source: the RAM data ports return a deterministic stream */
static uint32_t st_status, st_seed, st_mode, st_area;
static uint32_t st_x;
static uint32_t st_next(void) { st_x ^= st_x << 13; st_x ^= st_x >> 17; st_x ^= st_x << 5; return st_x; }
static unsigned st_hist_idx, st_sum_idx;
static uint32_t st_cell_hist[32]; static uint32_t st_cell_sum;
static void st_make_cell(void)
{   /* one cell: counts sum to area/2, a bump at a random bin; luma sum in luma/2 per pixel */
    uint32_t total = st_area / 2, c = st_next() % 32, wdt = 1 + st_next() % (st_mode == 4 ? 40 : 12), i, left = total, s = 0;
    for (i = 0; i < 32; i++) {
        uint32_t d = i > c ? i - c : c - i, wgt = d < wdt ? (wdt - d) * 16 : 1 + st_next() % 3;
        st_cell_hist[i] = wgt;
        s += wgt;
    }
    st_cell_sum = 0;
    for (i = 0; i < 32; i++) {
        uint32_t v = i == 31 ? left : (uint64_t)total * st_cell_hist[i] / s;
        if (v > left) v = left;
        left -= v; st_cell_hist[i] = v;
        st_cell_sum += v * ((i * 8 + 4) / 2) * 2;
    }
}
static uint32_t st_read(uint32_t r)
{
    if (r == 0xe014) return st_status;
    if (r == 0x501c8 || r == 0x501d8) {
        uint32_t i = st_hist_idx++ % 32;
        if (st_mode == 1) return st_next() % (st_area + 1);
        if (st_mode == 2) return 0;
        if (st_mode == 5) return st_area / 64;	/* flat: normalised sum exactly 2^20 */
        if (st_mode == 3) return st_next();
        if (i == 0) st_make_cell();
        return st_cell_hist[i];
    }
    if (r == 0x50208 || r == 0x50218) {
        if (st_mode == 1) return st_next() % (st_area * 128 + 1);
        if (st_mode == 2) return 0;
        if (st_mode == 3) return st_next();
        return st_cell_sum;	/* last cell's sum: same stream for both sides */
    }
    return 0;
}

static uint8_t *bank_of(uint8_t *bin, int night) { return night ? BANK_NIGHT(bin) : BANK_DAY(bin); }
static unsigned long nwr; static unsigned degenerate;

static unsigned cmp_state(const char *tag)
{
    unsigned bad = 0;
    uint8_t *lu = (uint8_t *)(uintptr_t)*(uint32_t *)lcelit_lce_main_used_para;
    uint8_t *ls = (uint8_t *)(uintptr_t)*(uint32_t *)lcelit_lce_main_save_para;
    uint8_t *lt = (uint8_t *)(uintptr_t)*(uint32_t *)lcelit_lce_main_tool_para;
    uint8_t *lh = (uint8_t *)(uintptr_t)*(uint32_t *)lcelit_lce_stat_ram_hist0;
    uint8_t *lsy = (uint8_t *)(uintptr_t)*(uint32_t *)lcelit_lce_stat_ram_sumy0;
    struct t40_lce_ctx *c = t40_lce;
    if (!lu || !c) { if (!!lu != !!c) { printf("  %s: state alive lit %d c %d\n", tag, !!lu, !!c); bad++; } return bad; }
#define CMP(name, a, b, n) do { unsigned k; for (k = 0; k < (n); k++) if (((uint8_t *)(a))[k] != ((uint8_t *)(b))[k]) { \
        if (bad < 6) printf("  %s: %s +%u lit %02x c %02x\n", tag, name, k, ((uint8_t *)(a))[k], ((uint8_t *)(b))[k]); bad++; break; } } while (0)
    CMP("used", lu, &c->par, 388);
    {
        static const struct { const char *n; unsigned o, l; } f[] = { { "hist", 0, 5760 }, { "sumy", 5760, 180 }, { "lut", 5940, 512 },
            { "lhist", 6452, 4096 }, { "ghist", 10548, 128 }, { "gcur", 10676, 64 }, { "gtgt", 10740, 64 }, { "lcur", 10804, 2048 },
            { "ltgt", 12852, 2048 }, { "lhct", 14900, 100 }, { "graw", 15000, 128 }, { "gdbg", 15128, 256 }, { "ldbg", 15384, 384 },
            { "raw", 15768, 128 }, { "flt", 15896, 128 }, { "lock", 16024, 64 } };
        for (unsigned q = 0; q < sizeof(f) / sizeof(f[0]); q++) CMP(f[q].n, ls + f[q].o, (uint8_t *)&c->work + f[q].o, f[q].l);
    }
    CMP("tool", lt, c->tool, 984);
    CMP("hist0", lh, c->stat_hist, 5760);
    CMP("sumy0", lsy, c->stat_sumy, 180);
    if (*(uint32_t *)lcelit_lce_tgain != t40_lce_tgain) { printf("  %s: tgain\n", tag); bad++; }
    if (*(uint32_t *)lcelit_lce_data_refresh_flag != t40_lce_refresh) { printf("  %s: refresh flag %u %u\n", tag, *(uint32_t *)lcelit_lce_data_refresh_flag, t40_lce_refresh); bad++; }
    if (*(uint32_t *)lcelit_lce_frm_num != t40_lce_frames) { printf("  %s: frames\n", tag); bad++; }
    if (*(uint32_t *)lcelit_lce_wdr_save_duan != t40_lce_wdr_duan) { printf("  %s: duan\n", tag); bad++; }
    if (((uint32_t *)lcelit_lce_debug)[0] != t40_lce_idle_irqs || ((uint32_t *)lcelit_lce_debug)[1] != t40_lce_dual_irqs) { printf("  %s: debug counters lit %u %u c %u %u\n", tag, ((uint32_t *)lcelit_lce_debug)[0], ((uint32_t *)lcelit_lce_debug)[1], t40_lce_idle_irqs, t40_lce_dual_irqs); bad++; }
    return bad;
}

enum { S_INIT, S_FRAME, S_GAIN, S_DN, S_WDR, S_CLEAN };
static unsigned step(int what, uint32_t a, uint32_t b, uint8_t *bank, const char *tag)
{
    struct wr *la, *lb; unsigned na, nb, bad, d0 = lit_ub, ev0;
    tparamsN[0] = (uintptr_t)bank;
    /* literal side */
    side = 0;
    st_x = st_seed; st_hist_idx = st_sum_idx = 0; ev0 = ev_pushed;
    switch (what) {
    case S_INIT: regtrace_lce_init_lit(a, b); break;
    case S_FRAME: if (irq_cb[8]) { ((int32_t (*)(void))irq_cb[8])(); if (ev_pushed != ev0 && ev_cb[0][16]) ((int32_t (*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t))ev_cb[0][16])(0, 0, 0, 0, 0); } break;
    case S_GAIN: regtrace_lce_tgain_update_lit(a); break;
    case S_DN: regtrace_lce_dn_refresh_lit(); break;
    case S_WDR: if (!regtrace_t40_lce_lit_active) break;
        *(uint32_t *)lcelit_lce_wdr_en = a; *(uint32_t *)lcelit_lce_data_refresh_flag = 1;
        tisp_lce_tool_to_awdr_lit(0, 0, 0, 0, LCELIT_SP(lcelit_stack_ctl), NULL, 0);
        tisp_lce_awdr_to_used_lit(0, 0, 0, 0, LCELIT_SP(lcelit_stack_ctl), NULL, 0); break;
    case S_CLEAN: regtrace_lce_lit_cleanup();
        /* OEM tisp_slake_all -> tisp_lce_remove (the literal driver glue skipped it) */
        memset(lcelit_lce_wdr_en, 0, 8); memset(lcelit_lce_frm_num, 0, 8); memset(lcelit_lce_data_refresh_flag, 0, 8);
        memset(lcelit_lce_tgain, 0, 8); ((uint32_t *)lcelit_lce_wdr_save_duan)[0] = ((uint32_t *)lcelit_lce_wdr_save_duan)[1] = 31; break;
    }
    la = snap(&na);
    if (lit_ub != d0) { degenerate++; free(la); return 0x10000; }
    /* C side */
    side = 1;
    st_x = st_seed; st_hist_idx = st_sum_idx = 0; ev0 = ev_pushed;
    switch (what) {
    case S_INIT: t40_lce_init(a, b); break;
    case S_FRAME: if (irq_cb[8]) { ((int32_t (*)(void))irq_cb[8])(); if (ev_pushed != ev0 && ev_cb[0][16]) ((int32_t (*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t))ev_cb[0][16])(0, 0, 0, 0, 0); } break;
    case S_GAIN: t40_lce_tgain_update(a); break;
    case S_DN: t40_lce_dn_refresh(); break;
    case S_WDR: if (!regtrace_t40_lce_active) break;
        t40_lce_wdr_on = a; t40_lce_refresh = 1; if (t40_lce) { t40_lce_load_params(t40_lce); t40_lce_gain_strengths(t40_lce); } break;
    case S_CLEAN: t40_lce_cleanup(); break;
    }
    lb = snap(&nb);
    bad = cmp_logs(tag, la, na, lb, nb) + cmp_state(tag);
    nwr += na;
    free(la); free(lb);
    return bad;
}

/* helper-level differential test: random and realistic inputs into each OEM helper */
static uint32_t ux = 99;
static uint32_t urnd(void) { ux ^= ux << 13; ux ^= ux >> 17; ux ^= ux << 5; return ux; }
static void fill_hist(uint32_t *h, int kind)
{
    unsigned i; uint32_t s = 0;
    for (i = 0; i < 32; i++) {
        switch (kind) {
        case 0: h[i] = urnd() % 0x10000; break;			/* around Q20 / 32 */
        case 1: h[i] = urnd() % 0x80000; break;			/* sums above 2^20 */
        case 2: h[i] = urnd() & 7 ? 0 : urnd() % 0x100000; break;	/* sparse */
        case 3: h[i] = urnd(); break;				/* anything */
        case 5: h[i] = 0x10000; break;				/* scales to exactly 2^20 */
        case 6: h[i] = 0x8100 + (i & 1) * 0x40; break;		/* equal bins above 2^20 */
        default: h[i] = 0x8000 + (int32_t)(urnd() % 0x2000) - 0x1000; break;
        }
        s += h[i];
    }
    (void)s;
}
static unsigned unit_tests(unsigned n)
{
    uint32_t *a = arena_alloc(128), *b = arena_alloc(128), *o1 = arena_alloc(128), *o2 = arena_alloc(128), *in2 = arena_alloc(128);
    uint32_t *p = arena_alloc(64), *r1 = arena_alloc(16), r2[2]; uint8_t *wt = arena_alloc(64), *up = arena_alloc(32), *lo = arena_alloc(32);
    uint16_t *c1 = arena_alloc(64), *c2 = arena_alloc(64), *old = arena_alloc(64), *tgt = arena_alloc(64);
    uint32_t *hist = arena_alloc(5760), *areas = arena_alloc(16); uint16_t *wp = arena_alloc(16);
    uint32_t *stk = arena_alloc(4096); uint32_t sp = (uint32_t)(uintptr_t)(stk + 512);
    unsigned it, i, bad[10] = { 0 }, d0;
    static const char *names[10] = { "filter", "head_tail", "hist_method", "cdf", "converge", "light_correct", "wdr_lock", "lock_hist", "normalize", "distance" };
    for (it = 0; it < n; it++) {
        int kind = it % 8;
        fill_hist(a, kind); fill_hist(b, kind);
        for (i = 0; i < 5; i++) wt[i] = it & 1 ? urnd() % 9 : (uint8_t[]){ 1, 2, 8, 2, 1 }[i];
        /* filter */
        memset(o1, 0, 128); memset(o2, 0, 128);
        d0 = lit_ub; lce_hist_filter_and_judge_lit((uint32_t)(uintptr_t)a, (uint32_t)(uintptr_t)o1, (uint32_t)(uintptr_t)wt, (uint32_t)(uintptr_t)r1, sp, NULL, 0);
        t40_lce_filter(a, o2, wt, r2);
        if (lit_ub == d0 && (memcmp(o1, o2, 128) || r1[0] != r2[0] || r1[1] != r2[1])) bad[0]++;
        /* head/tail */
        { uint32_t hc = urnd() % 600, tc = urnd() % 600;
          lce_head_tail_search_lit((uint32_t)(uintptr_t)a, hc, tc, (uint32_t)(uintptr_t)r1, sp, NULL, 0); t40_lce_head_tail(a, hc, tc, r2);
          if (r1[0] != r2[0] || r1[1] != r2[1]) bad[1]++; }
        /* hist method: valid knots */
        { uint32_t h = urnd() % 32, c = urnd() % 32, t = urnd() % 32, x;
          if (h > c) { x = h; h = c; c = x; } if (c > t) { x = c; c = t; t = x; } if (h > c) { x = h; h = c; c = x; }
          p[0] = h; p[1] = c; p[2] = t; for (i = 3; i < 8; i++) p[i] = urnd() % 256;
          memset(o1, 0, 128); memset(o2, 0, 128); d0 = lit_ub;
          lce_hist_method_lit((uint32_t)(uintptr_t)a, (uint32_t)(uintptr_t)o1, (uint32_t)(uintptr_t)p, 0, sp, NULL, 0);
          t40_lce_hist_method(a, o2, p);
          if (lit_ub == d0 && memcmp(o1, o2, 128)) bad[2]++; }
        /* cdf */
        lce_pdf_to_cdf_lit((uint32_t)(uintptr_t)a, (uint32_t)(uintptr_t)c1, 0, 0, sp, NULL, 0); t40_lce_cdf(a, c2);
        if (memcmp(c1, c2, 64)) bad[3]++;
        /* converge */
        for (i = 0; i < 32; i++) { old[i] = urnd() % 1100; tgt[i] = urnd() % 1100; }
        { uint32_t ra = urnd() % 256, mi = urnd() % 64, ma = urnd() % 256;
          stk[512 + 4] = (uint32_t)(uintptr_t)old; stk[512 + 5] = (uint32_t)(uintptr_t)tgt; stk[512 + 6] = (uint32_t)(uintptr_t)c1;
          lce_16bit_data_converge_lit(ra, mi, ma, 32, sp, NULL, 0); t40_lce_converge(ra, mi, ma, 32, old, tgt, c2);
          if (memcmp(c1, c2, 64)) bad[4]++; }
        /* light correct */
        for (i = 0; i < 32; i++) { up[i] = urnd() % 129; lo[i] = urnd() % 129; }
        p[0] = urnd() % 3; p[1] = urnd() % 64; p[2] = urnd() % 2; p[3] = urnd() % 2;
        memset(o1, 0, 128); memset(o2, 0, 128); d0 = lit_ub;
        stk[512 + 4] = (uint32_t)(uintptr_t)lo;
        lce_self_light_correct_lit((uint32_t)(uintptr_t)a, (uint32_t)(uintptr_t)o1, (uint32_t)(uintptr_t)p, (uint32_t)(uintptr_t)up, sp, NULL, 0);
        t40_lce_light_correct(a, o2, p, up, lo);
        if (lit_ub == d0 && memcmp(o1, o2, 128)) bad[5]++;
        /* lock hist */
        { uint32_t thr = urnd() % 34; int32_t st = (int32_t)(urnd() % 2100) - 50;
          memset(o1, 0, 128); memset(o2, 0, 128); d0 = lit_ub;
          lce_light_lock_adjust_hist_lit((uint32_t)(uintptr_t)a, (uint32_t)(uintptr_t)o1, thr, (uint32_t)st, sp, NULL, 0);
          t40_lce_lock_hist(a, o2, thr, st);
          if (lit_ub == d0 && memcmp(o1, o2, 128)) bad[7]++; }
        /* normalize */
        { uint32_t sh = 8 + urnd() % 6, rc = kind == 5 ? 1u << (sh - 1) : urnd() % 0x40000;
          memcpy(in2, a, 128); d0 = lit_ub;
          lce_std_hist_transform_lit((uint32_t)(uintptr_t)a, rc, sh, 0, sp, NULL, 0); t40_lce_normalize(in2, rc, sh);
          if (memcmp(a, in2, 128)) bad[8]++; }
        /* distance */
        if ((uint32_t)get_distance_1dim_lit((uint32_t)(uintptr_t)a, (uint32_t)(uintptr_t)b, 32, 0, sp, NULL, 0) != t40_lce_distance(a, b)) bad[9]++;
    }
    for (i = 0; i < 10; i++) if (bad[i]) printf("unit %s: %u of %u differ\n", names[i], bad[i], n);
    return bad[0] + bad[1] + bad[2] + bad[3] + bad[4] + bad[5] + bad[7] + bad[8] + bad[9];
}

static int chain_main(int argc, char **argv)
{
    static const uint32_t res[][2] = { { 3840, 2160 }, { 1920, 1080 }, { 2560, 1440 }, { 1280, 720 }, { 2592, 1944 }, { 640, 360 } };
    unsigned total = 0, nsteps = 0, k;
    reg_read_hook = st_read;
    total += unit_tests(20000);
    for (k = 1; k < (unsigned)argc; k++) {
        uint8_t *bin = load_bin(argv[k]); unsigned bad = 0, r, f, stop = 0;
        if (!strncmp(argv[k], "fuzz:", 5)) {
            /* random centre modes 2/3 put the centre (and so the tail) past bin 31, where the OEM
             * writes past its histogram: keep the centre at peak/median like every real bin */
            for (int n = 0; n < 2; n++) for (int hf = 0; hf < 2; hf++) {
                uint8_t *t = bank_of(bin, n) + T40_LCE_TOOL_OFFSET + hf * T40_LCE_WDR_HALF;
                t[20] &= 1; t[152] &= 1;
            }
        }
        char tag[96];
        lcg_state = 77 + k;
        for (r = 0; r < sizeof(res) / sizeof(res[0]) && !stop; r++) {
            int night = r & 1;
            uint32_t w = res[r][0], h = res[r][1];
            unsigned v;
            st_area = (h / 5) * ((w >> 1) / 9 * 2);
            ((uint32_t *)wdr_lce_para)[0] = (lcg() % 64) << 7;
            snprintf(tag, sizeof(tag), "init %ux%u", w, h);
            v = step(S_INIT, w, h, bank_of(bin, night), tag); nsteps++;
            if (v & 0x10000) { stop = 1; break; } bad += v;
            for (f = 0; f < 160 && !stop; f++) {
                uint32_t x = lcg();
                st_seed = lcg() | 1;
                st_mode = f < 60 ? 0 : f < 110 ? ((x >> 8) % 3 ? 4 : 5) : (x >> 8) % 6;
                st_status = (x % 9 == 0) ? 0 : (x % 9 == 1) ? 0x101 : (x % 9 == 2) ? 0x10101 : (x % 9 == 3) ? 0x100 : 1;
                if (f % 20 == 10) { ((uint32_t *)wdr_lce_para)[0] = (lcg() % 40) << 7; }
                if (f == 20) { v = step(S_WDR, 1, 0, bank_of(bin, night), "wdr on"); nsteps++; if (v & 0x10000) { stop = 1; break; } bad += v; }
                if (f == 140) { v = step(S_WDR, 0, 0, bank_of(bin, night), "wdr off"); nsteps++; if (v & 0x10000) { stop = 1; break; } bad += v; }
                if (x % 5 == 0) { snprintf(tag, sizeof(tag), "gain"); v = step(S_GAIN, lcg() % (12u << 16), 0, bank_of(bin, night), tag); nsteps++; if (v & 0x10000) { stop = 1; break; } bad += v; }
                if (x % 37 == 0) { night = !night; v = step(S_DN, 0, 0, bank_of(bin, night), "dn"); nsteps++; if (v & 0x10000) { stop = 1; break; } bad += v; }
                snprintf(tag, sizeof(tag), "%ux%u frame %u mode %u st %x", w, h, f, st_mode, st_status);
                v = step(S_FRAME, 0, 0, bank_of(bin, night), tag); nsteps++;
                if (v & 0x10000) { stop = 1; break; } bad += v;
                if (bad > 20) { stop = 1; break; }
            }
            v = step(S_CLEAN, 0, 0, bank_of(bin, night), "cleanup"); nsteps++; if (!(v & 0x10000)) bad += v;
        }
        printf("%s: %s%s\n", argv[k], bad ? "DIFF" : "bit-exact", stop && !bad ? " (stopped at an input where the OEM code is undefined)" : "");
        total += bad;
    }
    printf("lce: %d bins, %u steps, %lu register writes compared, %u differences, %u bins stopped at undefined OEM behaviour\n", argc - 1, nsteps, nwr, total, degenerate);
    return total != 0;
}
