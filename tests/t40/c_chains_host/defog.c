/* T40 defog: literal OEM chain (lit/defog_lit.inc) vs readable driver/t40/tx_isp_t40_defog.h.
 * Per bin and parameter variant: init at several resolutions (bank weights or count_weight),
 * frames of synthetic statistics through IRQ 10 / event 4, EV walks across the knots, IMP
 * strength changes, day/night refreshes, WDR tables, session cleanup.
 * ./defog <bin>... (run.sh defog <bins>) */
#include "common.h"
#define EINVAL 22
#define ENOMEM 12
#define ENODEV 19
enum dma_data_direction { DMA_BIDIRECTIONAL = 0 };
static void dma_cache_sync(void *dev, void *p, size_t n, enum dma_data_direction d) { (void)dev; (void)p; (void)n; (void)d; }
static void private_dma_cache_sync(void) { abort(); }	/* address-only key */
struct kmem_cache { size_t size; };
static struct kmem_cache caches[16];
static uint32_t kmalloc_caches[16];
static void *kmem_cache_alloc(struct kmem_cache *c, gfp_t f) { return kmalloc(c->size, f); }
static unsigned char tispinfo[128] __attribute__((aligned(4))), tispSecdInfo[128] __attribute__((aligned(4)));
static uint8_t *tune;
static uint32_t tisp_get_tuning(void) { return (uint32_t)(uintptr_t)tune; }
void ashrdi3_host(void) __asm__("__ashrdi3");
void ashrdi3_host(void) { abort(); }
/* IRQ / event plumbing */
static int side; static uintptr_t irq_cbs[2][64], ev_cbs[2][2][32];
#define irq_cb irq_cbs[side]
#define ev_cb ev_cbs[side]
static unsigned ev_pushed, ev_type;
static int32_t system_irq_func_set(int32_t irq, int32_t fn) { irq_cb[irq & 63] = (uint32_t)fn; return 0; }
static int32_t tisp_event_set_cb(int32_t ch, int32_t ev, int32_t fn) { ev_cb[ch & 1][ev & 31] = (uint32_t)fn; return 0; }
static int64_t tisp_event_push(uint32_t ch, uintptr_t a1) { ev_pushed++; ev_type = ((uint32_t *)a1)[2]; (void)ch; return 0; }
#define K(n) static void n(void) { abort(); }
K(Tiziano_defog_soft) K(defog_count_weight35abc) K(defog_wei_interpcot) K(dfg_array_cmp) K(tisp_defog_deinit)
K(tisp_defog_dn_params_refresh) K(tisp_defog_get_data) K(tisp_defog_img_filter25) K(tisp_defog_img_filter9)
K(tisp_defog_init) K(tisp_defog_interp_trans_k) K(tisp_defog_main_interrupt_static) K(tisp_defog_main_process)
K(tisp_defog_max_filter) K(tisp_defog_seco_interrupt_static) K(tisp_defog_seco_process) K(tisp_defog_strength_itp)
K(tisp_defog_update_reg_para2) K(tisp_defog_update_trans_k) K(tisp_defog_wdr_choose_para) K(tisp_defog_write_reg_para1)
K(tisp_defog_write_reg_para2) K(tisp_defog_write_reg_para3) K(tisp_s_defog_ratio) K(tisp_defog_get_data_part_0)
K(tisp_defog_ev_update)
#include "lit/defog_lit.inc"
#include "../../../driver/t40/tx_isp_t40_defog.h"

/* statistics: 4 DMA slots, 0xc2b4 names the filled one */
#define ST_PHYS 0x10000000u
static uint8_t *st_buf; static uint32_t st_slot, st_seed, st_mode;
static uint32_t st_read(uint32_t r) { return r == 0xc2b4 ? ST_PHYS + st_slot * 2048 : 0; }
static void st_fill(void)
{
    uint32_t x = st_seed, k; uint8_t *b = st_buf + st_slot * 2048;
    for (k = 0; k < 2048; k++) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; b[k] = x >> 5; }
    if (st_mode == 0) return;			/* garbage */
    for (k = 0; k < T40_DFG_CELLS; k++) {	/* three 21-bit sums, shifted down for darker cells */
        uint64_t w = 0; unsigned i;
        for (i = 0; i < 3; i++) {
            uint32_t v;
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            v = (x & 0x1fffff) >> (st_mode == 1 ? (x >> 24) % 5 : st_mode == 2 ? 3 : 0);
            if (st_mode == 3) v = 0x1fffff;
            w |= (uint64_t)v << (21 * i);
        }
        memcpy(b + 8 + 8 * k, &w, 8);
    }
}

static unsigned long nwr; static unsigned degenerate;
static uint8_t *lit_ptr(uint8_t *slot) { return (uint8_t *)(uintptr_t)*(uint32_t *)slot; }

static unsigned cmp_state(const char *tag)
{
    unsigned bad = 0;
    uint8_t *lt = lit_ptr(dfglit_defog_main_tool_para), *ls = lit_ptr(dfglit_defog_main_soft_para);
    uint8_t *lr = lit_ptr(dfglit_defog_main_reg2_para);
    struct t40_dfg_ctx *c = t40_dfg;
    if (!lt || !c) { if (!!lt != !!c) { printf("  %s: state alive lit %d c %d\n", tag, !!lt, !!c); bad++; } goto words; }
#define CMP(name, a, b, o, n) do { unsigned k; for (k = (o); k < (o) + (n); k++) if (((uint8_t *)(a))[k] != ((uint8_t *)(b))[k]) { \
        if (bad < 6) printf("  %s: %s +%u lit %02x c %02x\n", tag, name, k, ((uint8_t *)(a))[k], ((uint8_t *)(b))[k]); bad++; break; } } while (0)
    CMP("tool", lt, c->tool, 0, 812);
    CMP("soft", ls, &c->soft, 0, 2448);
    CMP("soft", ls, &c->soft, 2456, 3948 - 2456);
    CMP("reg2", lr, &c->reg2, 0, 206);
words:
    if (*(uint32_t *)dfglit_defog_ev != t40_dfg_ev) { printf("  %s: ev lit %u c %u\n", tag, *(uint32_t *)dfglit_defog_ev, t40_dfg_ev); bad++; }
    if (*(uint32_t *)dfglit_defog_frmnum != t40_dfg_frames) { printf("  %s: frames\n", tag); bad++; }
    if (dfglit_defog_zoomk_saved[0] != t40_dfg_zoomk) { printf("  %s: zoomk\n", tag); bad++; }
    if (dfglit_defog_strength_attr[0] != t40_dfg_strength) { printf("  %s: strength\n", tag); bad++; }
    if (regtrace_t40_defog_lit_active != regtrace_t40_defog_active) { printf("  %s: active lit %d c %d\n", tag, regtrace_t40_defog_lit_active, regtrace_t40_defog_active); bad++; }
    return bad;
}

enum { S_INIT, S_FRAME, S_EV, S_RATIO, S_DN, S_WDR, S_CLEAN, S_FRMSET };
static unsigned step(int what, uint32_t a, uint32_t b, uint64_t ev, uint8_t *bank, const char *tag)
{
    struct wr *la, *lb; unsigned na, nb, bad, d0 = lit_ub, ev0;
    tparamsN[0] = (uintptr_t)bank;
    side = 0;
    st_fill(); ev0 = ev_pushed;
    switch (what) {
    case S_INIT: regtrace_defog_init_lit(a, b); break;
    case S_FRAME: if (irq_cb[10]) { ((int32_t (*)(void))irq_cb[10])(); if (ev_pushed != ev0 && ev_type == 4 && ev_cb[0][4]) ((int32_t (*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t))ev_cb[0][4])(0, 0, 0, 0, 0); } break;
    case S_EV: regtrace_defog_ev_update_lit(ev); break;
    case S_RATIO: regtrace_defog_ratio_lit(); break;
    case S_DN: regtrace_defog_dn_refresh_lit(a, b); break;
    case S_WDR: ((uint32_t *)dfglit_defog_wdr_en)[0] = a; break;
    case S_CLEAN: regtrace_defog_lit_cleanup(); break;
    case S_FRMSET: *(uint32_t *)dfglit_defog_frmnum = a; break;
    }
    la = snap(&na);
    if (lit_ub != d0) { degenerate++; free(la); return 0x10000; }
    side = 1;
    st_fill(); ev0 = ev_pushed;
    switch (what) {
    case S_INIT: t40_defog_init(a, b); break;
    case S_FRAME: if (irq_cb[10]) { ((int32_t (*)(void))irq_cb[10])(); if (ev_pushed != ev0 && ev_type == 4 && ev_cb[0][4]) ((int32_t (*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t))ev_cb[0][4])(0, 0, 0, 0, 0); } break;
    case S_EV: t40_defog_ev_update(ev); break;
    case S_RATIO: t40_defog_ratio(); break;
    case S_DN: t40_defog_dn_refresh(a, b); break;
    case S_WDR: t40_dfg_wdr = a; break;
    case S_CLEAN: t40_defog_cleanup(); break;
    case S_FRMSET: t40_dfg_frames = a; break;
    }
    lb = snap(&nb);
    bad = cmp_logs(tag, la, na, lb, nb) + cmp_state(tag);
    nwr += na;
    free(la); free(lb);
    return bad;
}

/* parameter variants: the real bins leave most control bytes at one value */
static void patch(uint8_t *bank, unsigned var)
{
    uint8_t *t = bank + T40_DFG_TOOL_OFFSET;
    switch (var) {
    case 0: break;
    case 1: t[0] = 0; t[1] = 0; t[5] = 1; t[9] = 1; break;	/* per-channel curve, motion, smoothing */
    case 2: t[0] = 0; t[1] = 1; t[5] = 1; t[9] = 1; break;	/* one curve for all */
    case 3: t[0] = 1; t[5] = 1; t[9] = 0; break;		/* simple luma mode */
    case 4: t[706] = 1; t[435] = 100; t[5] = 1; break;	/* manual levels, zoomed weights */
    case 5: t[435] = 128; t[708] = 0x80; t[709] = 0x07; t[710] = 0x38; t[711] = 0x04; t[0] = 0; t[5] = 1; t[9] = 1; break; /* bank weights for 1920x1080 */
    }
    if (var) {	/* motion thresholds inside the defined range (OEM divides by them) */
        if (!t[6]) t[6] = 1;
        if ((t[8] << 6) <= (t[7] << 2)) t[8] = (t[7] >> 4) + 1;
    }
}

/* ---- the better-than-OEM fixes (C side only; the comparison above runs with them off) ---- */
static uint8_t ref_curve(const struct t40_dfg_soft *s, uint32_t v)
{
    int32_t a = s->curve[v >> 4], b = s->curve[(v >> 4) + 1], x = a + (((b - a) * (int32_t)(v & 15) + 8) >> 4);
    uint32_t y = (uint32_t)x * (uint32_t)(s->ctl[3] - s->ctl[2]) + 127;
    return (y * 257 + ((uint32_t)s->ctl[2] << 16)) >> 16 & 0xff;
}
static uint8_t ref_trans(const uint8_t *t, int32_t y)
{
    const uint8_t *kn = t + DFG_T_YKNOTS, *lv = t + DFG_T_CUR; int j;
    if (y < kn[0]) return lv[0];
    for (j = 0; j < 4; j++)
        if (y < kn[j + 1]) return lv[j] + ((y - kn[j]) * (lv[j + 1] - lv[j]) + ((kn[j + 1] - kn[j]) >> 1)) / (kn[j + 1] - kn[j]);
    return lv[4];
}
/* cached values that no longer match the current curves (stale entries) */
static unsigned stale(void)
{
    const struct t40_dfg_soft *s = &t40_dfg->soft; unsigned v, n = 0;
    for (v = 0; v < 256; v++) {
        if ((s->flags[v] & 1) && s->cache_curve[v] != ref_curve(s, v)) n++;
        if ((s->flags[v] & 0x10) && s->cache_trans[v] != ref_trans(t40_dfg->tool, v)) n++;
    }
    return n;
}
static void c_frames(unsigned n, uint32_t mode)
{
    unsigned i, na;
    side = 1;
    for (i = 0; i < n; i++) {
        uint32_t e0 = ev_pushed;
        st_seed = lcg() | 1; st_slot = i % 4; st_mode = mode; st_fill();
        ((int32_t (*)(void))irq_cbs[1][10])();
        if (ev_pushed != e0) ((int32_t (*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t))ev_cbs[1][0][4])(0, 0, 0, 0, 0);
    }
    free(snap(&na));
}
/* motion weights with the window variance computed independently */
static unsigned motion_ref_diff(void)
{
    struct t40_dfg_ctx *c = t40_dfg; const struct t40_dfg_soft *s = &c->soft;
    int32_t off = s->ctl[6] << 4, lo = s->ctl[7] << 2, hi = s->ctl[8] << 6; int r, q, y, x; unsigned bad = 0;
    t40_dfg_motion(c);
    for (r = 0; r < 10; r++) for (q = 0; q < 18; q++) {
        int64_t sum = 0, asum = 0, var = 0; int n = 0, mean; int64_t v; uint8_t w;
        for (y = r - 2; y <= r + 2; y++) for (x = q - 2; x <= q + 2; x++)
            if (y >= 0 && y < 10 && x >= 0 && x < 18) { int d = c->dy[y * 18 + x]; sum += d; asum += d < 0 ? -d : d; n++; }
        mean = (int)(sum / n);
        for (y = r - 2; y <= r + 2; y++) for (x = q - 2; x <= q + 2; x++)
            if (y >= 0 && y < 10 && x >= 0 && x < 18) { int d = c->dy[y * 18 + x] - mean; var += d * d; }
        v = (int32_t)((uint32_t)(var / n + off) * (uint32_t)((asum << 6) / n)) / off;
        w = v > hi ? 255 : v < lo ? 0 : ((v - lo) << 8) / (hi - lo);
        if (c->motion[r * 18 + q] != w) bad++;
    }
    return bad;
}
static unsigned better_tests(const char *path)
{
    uint8_t *bin = load_bin(path), *day = BANK_DAY(bin), *night = BANK_NIGHT(bin), *td, *tn;
    unsigned fail = 0, on, st[2][3], i;
    patch(day, 1); patch(night, 1);
    td = day + T40_DFG_TOOL_OFFSET; tn = night + T40_DFG_TOOL_OFFSET;
    for (i = 0; i < 17; i++) tn[DFG_T_CURVE + i] = 255 - td[DFG_T_CURVE + i] / 2;	/* night curve differs */
    for (i = 0; i < 5; i++) tn[DFG_T_YKNOTS + i] = td[DFG_T_YKNOTS + i] / 2 + 10 * i;
    for (i = 0; i < 9; i++) {	/* day levels that follow the EV, knots 0..8000 */
        t40_dfg_put32(td, DFG_T_KNOTS + 4 * i, 1000 * i);
        for (unsigned j = 0; j < 5; j++) td[DFG_T_LEVELS + 9 * j + i] = 40 + 20 * i + 3 * j;
    }
    td[DFG_T_MANUAL] = 0;
    for (on = 0; on < 2; on++) {
        t40_defog_dn_cache_reset = t40_defog_trans_reset_all = t40_defog_motion_var = on;
        side = 1; tparamsN[0] = (uintptr_t)day; lcg_state = 4242;
        t40_defog_init(1920, 1080); c_frames(6, 1); c_frames(4, 3);	/* fills curve and transmission caches up to 255 */
        /* 1: night bank arrives */
        tparamsN[0] = (uintptr_t)night; t40_defog_dn_refresh(1920, 1080); c_frames(1, 2);
        st[on][0] = stale();
        /* 2: EV jump to another knot (levels change), bright cells cached before */
        tparamsN[0] = (uintptr_t)day; t40_defog_dn_refresh(1920, 1080); c_frames(4, 3);
        t40_defog_ev_update(500ull << 10); c_frames(4, 3);
        t40_defog_ev_update(7500ull << 10); c_frames(1, 2);
        st[on][1] = stale();
        /* 3: motion weights against an independent window variance */
        for (i = 0; i < 180; i++) t40_dfg->dy[i] = (int16_t)(lcg() % 120) - 60;
        if (!t40_dfg->soft.ctl[6]) t40_dfg->soft.ctl[6] = 1;
        st[on][2] = motion_ref_diff();
        t40_defog_cleanup();
    }
    printf("better: stale caches after day/night OEM %u fixed %u; after EV change OEM %u fixed %u; motion cells off the window variance OEM %u fixed %u\n",
           st[0][0], st[1][0], st[0][1], st[1][1], st[0][2], st[1][2]);
    for (i = 0; i < 3; i++) if (st[1][i] || !st[0][i]) fail++;	/* fixed must be clean, the OEM case must show the flaw */
    t40_defog_dn_cache_reset = t40_defog_trans_reset_all = t40_defog_motion_var = 0;
    return fail;
}

static int chain_main(int argc, char **argv)
{
    static const uint32_t res[][2] = { { 1920, 1080 }, { 3840, 2160 }, { 2560, 1440 }, { 1280, 720 }, { 2592, 1944 }, { 640, 360 }, { 658, 814 } };	/* 658x814: H*W/2025 rounds at the edge */
    unsigned total = 0, nsteps = 0, k, i;
    reg_read_hook = st_read;
    for (i = 0; i < 16; i++) { caches[i].size = 1u << i; kmalloc_caches[i] = (uint32_t)(uintptr_t)&caches[i]; }
    st_buf = arena_alloc(4 * 2048);
    *(uint32_t *)(tispinfo + 60) = (uint32_t)(uintptr_t)st_buf;
    *(uint32_t *)(tispinfo + 64) = ST_PHYS;
    tune = arena_alloc(4096); memset(tune, 0, 4096);
    /* OEM-exact comparison: the better-than-OEM fixes off */
    t40_defog_dn_cache_reset = t40_defog_trans_reset_all = t40_defog_motion_var = 0;
    for (k = 1; k < (unsigned)argc; k++) {
        size_t mark = used;	/* everything of a bin is gone after its final cleanup (kfree is a no-op here) */
        uint8_t *bin0 = load_bin(argv[k]), *bin = arena_alloc(BIN_BYTES); unsigned bad = 0, r, f, stop = 0, var;
        char tag[96];
        for (var = 0; var < 6 && !stop; var++) {
            memcpy(bin, bin0, BIN_BYTES);
            patch(BANK_DAY(bin), var); patch(BANK_NIGHT(bin), var);
            lcg_state = 91 + k * 7 + var;
            for (r = 0; r < sizeof(res) / sizeof(res[0]) && !stop; r++) {
                int night = r & 1;
                uint32_t w = res[r][0], h = res[r][1], v;
                uint64_t ev = 0;
#define DO(...) do { v = step(__VA_ARGS__); nsteps++; if (v & 0x10000) { stop = 1; break; } bad += v; } while (0)
                snprintf(tag, sizeof(tag), "var %u init %ux%u", var, w, h);
                DO(S_INIT, w, h, 0, night ? BANK_NIGHT(bin) : BANK_DAY(bin), tag);
                if (stop) break;
                if (var == 1 && r == 2) DO(S_WDR, 1, 0, 0, night ? BANK_NIGHT(bin) : BANK_DAY(bin), "wdr on");
                if (r == 1) DO(S_FRMSET, 0x3ffffffe + var, 0, 0, BANK_DAY(bin), "frame counter wrap");	/* 2^30 frames: OEM restarts at 255 */
                for (f = 0; f < 70 && !stop; f++) {
                    uint32_t x = lcg();
                    uint8_t *bank = night ? BANK_NIGHT(bin) : BANK_DAY(bin);
                    st_seed = lcg() | 1; st_slot = x % 5 == 4 ? 7 : x % 4; st_mode = (x >> 8) % 4;
                    if (x % 3 == 0) {	/* EV walk: small steps, jumps, the knots themselves */
                        uint32_t y = lcg();
                        ev = y % 4 == 0 ? (uint64_t)(lcg() % 200000) << 10 : y % 4 == 1 ? ev + ((uint64_t)(lcg() % 3000) << 10) :
                             y % 4 == 2 ? (uint64_t)t40_dfg_u32(bank + T40_DFG_TOOL_OFFSET, DFG_T_KNOTS + 4 * (lcg() % 9)) << 10 : (uint64_t)lcg() << (lcg() % 20);
                        DO(S_EV, 0, 0, ev, bank, "ev");
                    }
                    if (x % 23 == 5) {
                        static const uint8_t st[] = { 0, 1, 64, 127, 128, 129, 200, 255 };
                        *(uint32_t *)(tune + 864) = lcg() % 3; tune[868] = st[lcg() % 8];
                        DO(S_RATIO, 0, 0, 0, bank, "ratio");
                    }
                    if (x % 29 == 7) { night = !night; DO(S_DN, w, h, 0, night ? BANK_NIGHT(bin) : BANK_DAY(bin), "dn"); }
                    snprintf(tag, sizeof(tag), "var %u %ux%u frame %u mode %u slot %u", var, w, h, f, st_mode, st_slot);
                    DO(S_FRAME, 0, 0, 0, night ? BANK_NIGHT(bin) : BANK_DAY(bin), tag);
                    if (bad > 20) { stop = 1; break; }
                }
                if (var == 1 && r == 2) DO(S_WDR, 0, 0, 0, BANK_DAY(bin), "wdr off");
                if (r % 2 == 0) { v = step(S_CLEAN, 0, 0, 0, BANK_DAY(bin), "cleanup"); nsteps++; if (!(v & 0x10000)) bad += v; }
            }
        }
        { unsigned v = step(S_CLEAN, 0, 0, 0, BANK_DAY(bin), "cleanup"); nsteps++; if (!(v & 0x10000)) bad += v; }
        printf("%s: %s%s\n", argv[k], bad ? "DIFF" : "bit-exact", stop && !bad ? " (stopped at an input where the OEM code is undefined)" : "");
        total += bad;
        used = mark;
    }
    if (argc > 1) { size_t m = used; total += better_tests(argv[1]); used = m; }
    printf("defog: %d bins, %u steps, %lu register writes compared, %u differences, %u bins stopped at undefined OEM behaviour\n", argc - 1, nsteps, nwr, total, degenerate);
    return total != 0;
}
