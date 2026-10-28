#include <time.h>
/* T40 ADR: literal OEM chain (lit/adr_lit.inc) vs readable driver/t40/tx_isp_t40_adr.h.
 * Helper-level random tests first, then whole frames per bin.
 * ./adr <bin>... (run.sh adr <bins>) */
#include "common.h"
#define EINVAL 22
#define ENOMEM 12
#define ENODEV 19
enum dma_data_direction { DMA_BIDIRECTIONAL = 0 };
static void dma_cache_sync(void *dev, void *p, size_t n, enum dma_data_direction d) { (void)dev; (void)p; (void)n; (void)d; }
struct mutex { int x; };
#define DEFINE_MUTEX(n) struct mutex n = { 0 }
static void mutex_init(struct mutex *m) { m->x = 0; }
static void mutex_lock(struct mutex *m) { m->x++; }
static void mutex_unlock(struct mutex *m) { m->x--; }
#define K(n) static void n(void) { abort(); }
K(private_dma_cache_sync) K(private_mutex_lock) K(private_mutex_unlock) K(private_raw_mutex_init) K(private_vfree) K(private_vmalloc_key)
static void *private_vmalloc(size_t n) { return vmalloc(n); }
static unsigned char tispinfo[128] __attribute__((aligned(4)));
static uint8_t *tune;
static uint32_t tisp_get_tuning(void) { return (uint32_t)(uintptr_t)tune; }
void ashldi3_host(void) __asm__("__ashldi3");
void ashldi3_host(void) { abort(); }
#define __ashldi3 ashldi3_host
void lshrdi3_host(void) __asm__("__lshrdi3");
void lshrdi3_host(void) { abort(); }
unsigned long long div64_u64_host(unsigned long long a, unsigned long long b) __asm__("div64_u64");
unsigned long long div64_u64_host(unsigned long long a, unsigned long long b) { return b ? a / b : 0; }
static int isp_printf(int l, const char *f, ...) { (void)l; (void)f; return 0; }
/* IRQ / event plumbing */
static int side; static uintptr_t irq_cbs[2][64], ev_cbs[2][2][32];
#define irq_cb irq_cbs[side]
#define ev_cb ev_cbs[side]
static unsigned ev_pushed, ev_type;
static int32_t system_irq_func_set(int32_t irq, int32_t fn) { irq_cb[irq & 63] = (uint32_t)fn; return 0; }
static int32_t tisp_event_set_cb(int32_t ch, int32_t ev, int32_t fn) { ev_cb[ch & 1][ev & 31] = (uint32_t)fn; return 0; }
static int64_t tisp_event_push(uint32_t ch, uintptr_t a1) { ev_pushed++; ev_type = ((uint32_t *)a1)[2]; (void)ch; return 0; }
K(fix_point_div) K(fix_point_div_32) K(fix_point_mult2_32) K(func_adr_map_curve1) K(func_adr_reg_write_5x5) K(func_adr_reg_write_every)
K(func_adr_reg_write_one) K(func_adr_reg_write_sometimes) K(func_gauss_local) K(func_interp1_short) K(func_local_info) K(func_map_y_filter)
K(func_map_y_filter_sp) K(ispint_adr_16) K(ispint_adr_32) K(subsection_map) K(subsection_up) K(tisp_adr_dn_params_refresh)
K(tisp_adr_main_deinit) K(tisp_adr_main_init) K(tisp_adr_main_process) K(tisp_math_exp2) K(tisp_s_drc_ratio) K(tiziano_adr_5x5_init)
K(tiziano_adr_5x5_out) K(tiziano_adr_algorithm) K(tiziano_adr_base_pars) K(tiziano_adr_ev_func) K(Tiziano_adr_fpga)
K(tiziano_adr_hard_base_refresh) K(tiziano_adr_hard_para_refresh) K(tiziano_adr_main_interrupt_static) K(tiziano_adr_params_refresh)
K(tiziano_adr_read_data) K(tiziano_adr_soft_para_refresh) K(tiziano_adr_stat_calc) K(tiziano_adr_tgain_func) K(tiziano_adr_top_para_refresh)
K(tiziano_adr_wal_para_refresh)
#include "lit/adr_lit.inc"
#define div64_u64(a, b) ((uint64_t)(a) / (uint64_t)(b))
#include "../../../driver/t40/tx_isp_t40_adr.h"

static uint32_t ux = 77;
static uint32_t ur(void) { ux ^= ux << 13; ux ^= ux >> 17; ux ^= ux << 5; return ux; }
static uint32_t pick(void)
{   /* mixed magnitudes, signs and edges */
    uint32_t k = ur() % 8, v = ur();
    switch (k) { case 0: return v % 16; case 1: return v % 4096; case 2: return v % 0x10000; case 3: return (v % 0x400000) << (ur() % 10);
    case 4: return -(int32_t)(v % 5000); case 5: return v; case 6: return v >> (ur() % 31); default: return v % 1000000; }
}
#define LSP(stk) ((uint32_t)(uintptr_t)((stk) + 4096 - 64))
static unsigned unit_tests(unsigned n)
{
    uint32_t *stk = arena_alloc(16384), sp = LSP((uint8_t *)stk), v1, it, bad[16] = { 0 }, d0;
    uint32_t *sa = (uint32_t *)(uintptr_t)(sp + 16);
    int16_t *xs = arena_alloc(512), *ys = arena_alloc(512), *xq = arena_alloc(512), *o1 = arena_alloc(512), *o2 = arena_alloc(512);
    int32_t *x32 = arena_alloc(1024);
    static const char *nm[16] = { "div32", "div64", "mul32", "exp2", "int16", "int32", "interp1", "subsection_map", "subsection_up", "local_info", "map_filter", "map_filter_sp", "gauss_local", "map_curve1" };
    for (it = 0; it < n; it++) {
        uint32_t a = pick(), b = pick(), c = pick(), nb = ur() % 34, r1, r2, i, cnt;
        /* div32 */
        d0 = lit_ub; r1 = fix_point_div_32_lit(nb, a, c ? c : 1, 0, sp, &v1, 0); r2 = t40_adr_div32(nb, a, c ? c : 1);
        if (lit_ub == d0 && r1 != r2) { if (bad[0] < 3) printf("div32 n %u %u/%u lit %x c %x\n", nb, a, c, r1, r2); bad[0]++; }
        /* div64 */
        { uint64_t x = ((uint64_t)pick() << (ur() % 32)) ^ pick(), y = ((uint64_t)pick() << (ur() % 20)) ^ pick(), q; uint32_t n6 = ur() % 40;
          if (!y) y = 1; sa[0] = (uint32_t)y; sa[1] = (uint32_t)(y >> 32);
          r1 = fix_point_div_lit(n6, 0, (uint32_t)x, (uint32_t)(x >> 32), sp, &v1, 0); q = t40_adr_div64(n6, x, y);
          if (r1 != (uint32_t)q || v1 != (uint32_t)(q >> 32)) { if (bad[1] < 3) printf("div64 mismatch\n"); bad[1]++; } }
        /* mul32 */
        r1 = fix_point_mult2_32_lit(nb % 32, a, b, 0, sp, &v1, 0); r2 = t40_adr_mul32(nb % 32, a, b); if (r1 != r2) bad[2]++;
        /* exp2 */
        { uint32_t fb = ur() % 17, ob = ur() % 31, x = ur() % (16u << fb);
          r1 = tisp_math_exp2_lit(x, fb, ob, 0, sp, &v1, 0); r2 = t40_adr_exp2(x, fb, ob); if (r1 != r2) { if (bad[3] < 3) printf("exp2 %u %u %u lit %x c %x\n", x, fb, ob, r1, r2); bad[3]++; } }
        /* tables */
        cnt = 1 + ur() % 40;
        { int32_t acc = (int32_t)(ur() % 200) - 100;
          for (i = 0; i < 160; i++) { acc += ur() % 4 ? (int32_t)(ur() % 300) : 0; xs[i] = acc; x32[i] = acc * 3; ys[i] = (int16_t)(ur() % 8192) - 2048; xq[i] = (int16_t)(ur() % 12000) - 500; } }
        { int32_t x = (int16_t)(ur() % 12000) - 500;
          d0 = lit_ub; r1 = ispint_adr_16_lit(x, (uint32_t)(uintptr_t)xs, (uint32_t)(uintptr_t)ys, cnt, sp, &v1, 0);
          if (lit_ub == d0 && r1 != (uint32_t)t40_adr_int16(x, xs, ys, cnt)) bad[4]++;
          d0 = lit_ub; r1 = ispint_adr_32_lit(x * 3, (uint32_t)(uintptr_t)x32, (uint32_t)(uintptr_t)ys, cnt, sp, &v1, 0);
          if (lit_ub == d0 && r1 != (uint32_t)t40_adr_int32(x * 3, x32, ys, cnt)) bad[5]++; }
        { int32_t nq = 1 + ur() % 40;
          memset(o1, 0x5a, 512); memset(o2, 0x5a, 512); sa[0] = cnt; sa[1] = nq; d0 = lit_ub;
          func_interp1_short_lit((uint32_t)(uintptr_t)xs, (uint32_t)(uintptr_t)ys, (uint32_t)(uintptr_t)xq, (uint32_t)(uintptr_t)o1, sp, &v1, 0);
          t40_adr_interp1(xs, ys, xq, o2, cnt, nq);
          if (lit_ub == d0 && memcmp(o1, o2, 512)) bad[6]++; }
    }
    /* table helpers with realistic tables: monotone 129-knot curves, 512-entry level table */
    {
        int16_t *P = arena_alloc(512), *Q = arena_alloc(512); int32_t *T = arena_alloc(4096), *o32a = arena_alloc(256), *o32b = arena_alloc(256);
        int32_t *va = arena_alloc(128), *vb = arena_alloc(128); int16_t *ma = arena_alloc(64), *mb = arena_alloc(64), *prm = arena_alloc(64);
        uint8_t *soft = arena_alloc(256);
        for (it = 0; it < n / 20; it++) {
            uint32_t i, sh = 10, sh2 = it % 5 == 0 ? ur() % 12 : 10; int32_t acc = 0, acc2 = 0;
            P[-1] = (int16_t)ur(); Q[-1] = (int16_t)ur();	/* the OEM reads one before */
            for (i = 0; i < 129; i++) { acc += 1 + ur() % 64; acc2 += ur() % 70; P[i] = acc; Q[i] = it & 1 ? 32 * i : acc2; }
            for (i = 0; i < 512; i++) T[i] = (int32_t)(ur() % 20000) - (it % 7 == 0 ? 500 : 0);
            if (it & 2) for (i = 0; i < 512; i++) T[i] = i * 10000 / 512;
            {   int32_t x = (int32_t)(ur() % 12000) - 1000, c6 = (int32_t)(ur() % 5000) - 200, c2 = (int32_t)(ur() % 300), m = 1 + ur() % 20, mode = ur() % 3;
                sa[0] = (uint32_t)(uintptr_t)Q; sa[1] = (uint32_t)(uintptr_t)T; sa[2] = m; sa[3] = sh2; sa[4] = sh; sa[5] = mode; d0 = lit_ub;
                uint32_t r1 = subsection_map_lit(x, c6, c2, (uint32_t)(uintptr_t)P, sp, &v1, 0);
                uint32_t r2 = t40_adr_subsection_map(x, c6, c2, P, Q, T, m, sh2, sh, mode);
                if (lit_ub == d0 && r1 != r2) { if (bad[7] < 3) printf("smap x %d c6 %d c2 %d m %d sh2 %u mode %d lit %d c %d\n", x, c6, c2, m, sh2, mode, r1, r2); bad[7]++; }
                memset(o32a, 0x5a, 256); memset(o32b, 0x5a, 256); m = 8 + m;
                sa[0] = (uint32_t)(uintptr_t)T; sa[1] = m; sa[2] = sh; sa[3] = sh; sa[4] = mode; d0 = lit_ub;
                subsection_up_lit((uint32_t)(uintptr_t)o32a, c2, (uint32_t)(uintptr_t)P, (uint32_t)(uintptr_t)Q, sp, &v1, 0);
                t40_adr_subsection_up(o32b, c2, P, Q, T, m, sh, sh, mode);
                if (lit_ub == d0 && memcmp(o32a, o32b, 256)) { if (bad[8] < 3) { printf("sup differ m %d c2 %d mode %d:", m, c2, mode); for (int q = 0; q < 11; q++) printf(" %d/%d", o32a[q], o32b[q]); printf("\n"); } bad[8]++; } }
            {   int32_t v = ur() % 10000, b = ur() % 4096, c = 1 + ur() % 2000, d = 1 + ur() % 2000, e = (int32_t)(ur() % 4200) - 50;
                sa[0] = e; d0 = lit_ub;
                uint32_t r1 = func_local_info_lit(v, b, c, d, sp, &v1, 0);
                if (lit_ub == d0 && r1 != (uint32_t)t40_adr_local_info(v, b, c, d, e)) { if (bad[9] < 3) printf("linfo %d %d %d %d %d lit %d c %d\n", v, b, c, d, e, r1, t40_adr_local_info(v, b, c, d, e)); bad[9]++; } }
            {   int32_t lim = ur() % 18, iters = ur() % 6, a = 0, mm = 0;
                for (i = 0; i < 16; i++) { mm += 1 + ur() % 400; ma[i] = mb[i] = i ? mm : 0; }
                for (i = 0; i < 14; i++) { a += ur() % 400; va[i] = vb[i] = a < 4095 ? a : 4095; }
                d0 = lit_ub; func_map_y_filter_lit((uint32_t)(uintptr_t)ma, (uint32_t)(uintptr_t)va, iters, lim, sp, &v1, 0); t40_adr_map_filter(mb, vb, iters, lim);
                if (lit_ub == d0 && (memcmp(va, vb, 56) || memcmp(ma, mb, 32))) { if (bad[10] < 3) printf("mapf differ\n"); bad[10]++; }
                for (i = 0; i < 16; i++) mb[i] = ma[i] = i ? (i * 4095) / 15 + (int)(ur() % 50) - 25 : 0;
                for (i = 0; i < 14; i++) vb[i] = va[i] = (i + 1) * 270 + (int)(ur() % 100);
                d0 = lit_ub; func_map_y_filter_sp_lit((uint32_t)(uintptr_t)ma, (uint32_t)(uintptr_t)va, iters, 0, sp, &v1, 0); t40_adr_map_filter_sp(mb, vb, iters);
                if (lit_ub == d0 && (memcmp(va, vb, 56) || memcmp(ma, mb, 32))) { if (bad[11] < 3) printf("mapfsp differ\n"); bad[11]++; } }
            {   int16_t old[4]; memcpy(old, adrlit_adr_gauss_old, 8);
                for (i = 0; i < 16; i++) prm[i] = (int16_t)(ur() % 1400) - 100;
                if (it % 3 == 0) prm[9] = (int16_t)(ur() % 9) - 2;
                *(uint32_t *)adrlit_adr_main_soft_para = (uint32_t)(uintptr_t)soft; *(uint32_t *)(soft + 136) = (uint32_t)(uintptr_t)prm;
                memcpy(o32b, adrlit_adr_gauss_local, 96); d0 = lit_ub;
                func_gauss_local_lit((uint32_t)(uintptr_t)adrlit_adr_gauss_local, 0, 0, 0, sp, &v1, 0);
                t40_adr_gauss_local(o32b, old, prm);
                if (lit_ub == d0 && (memcmp(o32b, adrlit_adr_gauss_local, 96) || memcmp(old, adrlit_adr_gauss_old, 8))) { if (bad[12] < 3) printf("gauss differ\n"); bad[12]++; }
                memcpy(adrlit_adr_gauss_old, old, 8); memcpy(adrlit_adr_gauss_local, o32b, 96); }
            {   int32_t idx = (int32_t)(ur() % 300) - 20, k = (int32_t)(ur() % 300) - 20;
                memset(o32a, 0x5a, 64); memset(o32b, 0x5a, 64); d0 = lit_ub;
                func_adr_map_curve1_lit((uint32_t)(uintptr_t)o32a, idx, k, 0, sp, &v1, 0); t40_adr_map_curve1(o32b, idx, k);
                if (lit_ub == d0 && memcmp(o32a, o32b, 56)) { if (bad[13] < 3) printf("curve1 idx %d k %d differ\n", idx, k); bad[13]++; } }
        }
    }
    for (it = 0; it < 16; it++) if (bad[it]) printf("unit %s: %u of %u differ\n", nm[it] ? nm[it] : "?", bad[it], n);
    { unsigned t = 0; for (it = 0; it < 16; it++) t += bad[it]; return t; }
}

/* ---- whole chain ---- */
#define ST_PHYS 0x20000000u
static uint8_t *st_buf; static uint32_t st_slot, st_seed, st_mode, st_n;
static uint32_t st_read(uint32_t r) { return r == 0x9540 ? ST_PHYS + st_slot * 0x1000 : 0; }
static uint32_t sx;
static uint32_t sr(void) { sx ^= sx << 13; sx ^= sx >> 17; sx ^= sx << 5; return sx; }
static void put_bits(uint8_t *b, unsigned bit, unsigned nb, uint32_t v)
{
    unsigned i; for (i = 0; i < nb; i++) { unsigned k = bit + i; if (v >> i & 1) b[k >> 3] |= 1u << (k & 7); else b[k >> 3] &= ~(1u << (k & 7)); }
}
static void st_fill(void)
{
    uint8_t *b = st_buf + st_slot * 0x1000; unsigned k, r, cc, j;
    sx = st_seed;
    for (k = 0; k < 0x1000; k++) b[k] = sr() >> 7;
    if (st_mode == 0) return;	/* garbage */
    if (st_mode == 2) { memset(b, 0, 0x1000); return; }
    for (r = 0; r < 4; r++) for (cc = 0; cc < 6; cc++) {
        uint8_t *blk = b + 8 + 336 * r + 56 * cc; uint32_t nb = st_n / 24 + 1;
        for (j = 0; j < 20; j++) put_bits(blk, 18 * j, 18, st_mode == 3 ? (j == 19 ? nb : 0) : sr() % (nb / 6 + 1));
        put_bits(blk, 360, 4, 0); put_bits(blk, 364, 12, sr() % 4096); put_bits(blk, 376, 12, sr() % 4096);
        put_bits(blk, 384, 29, sr() % (nb * 256 + 1)); put_bits(blk, 416, 29, sr() % (nb * 256 + 1));
    }
    for (k = 0; k < 512; k++) put_bits(b + 0x548, 21 * k + (k / 3) * 1, 21, 0);	/* clear */
    for (k = 0; k < 512; k++) {
        uint32_t v = st_mode == 3 ? (k == 500 ? st_n : 0) : sr() % (st_n / 256 + 2);
        unsigned w = k / 3, f = k % 3;
        put_bits(b + 0x548 + 8 * w, 21 * f, 21, v);
    }
}

static unsigned long nwr; static unsigned degenerate;
static uint8_t *lp(uint8_t *slot) { return (uint8_t *)(uintptr_t)*(uint32_t *)slot; }
static unsigned cmp_state(const char *tag)
{
    unsigned bad = 0, i;
    struct t40_adr_ctx *c = t40_adr;
    uint8_t *lpar = lp(adrlit_main_adr_paras);
#define CMP(name, a, b, n) do { unsigned k; for (k = 0; k < (n); k++) if (((uint8_t *)(a))[k] != ((uint8_t *)(b))[k]) { \
        if (bad < 6) printf("  %s: %s +%u lit %02x c %02x\n", tag, name, k, ((uint8_t *)(a))[k], ((uint8_t *)(b))[k]); bad++; break; } } while (0)
    if (!lpar || !c) { if (!!lpar != !!c) { printf("  %s: state alive lit %d c %d\n", tag, !!lpar, !!c); bad++; } goto words; }
    CMP("par", lpar, c->par, 5108);
    CMP("gamma", lp(adrlit_main_adr_gamma), c->gamma, 516);
    CMP("map_y_old", lp(adrlit_adr_main_map_y_old), c->map_y_old, 672);
    CMP("ctc1_y_tmp", lp(adrlit_adr_main_ctc1_y_tmp), c->ctc1_y_tmp, 46);
    CMP("stat", lp(adrlit_adr_main_stat_info), c->stat, 10056);
    CMP("lut20", lp(adrlit_adr_w_20_lut_main), c->lut20, 32); CMP("lut02", lp(adrlit_adr_w_02_lut_main), c->lut02, 32);
    CMP("lut12", lp(adrlit_adr_w_12_lut_main), c->lut12, 32); CMP("lut22", lp(adrlit_adr_w_22_lut_main), c->lut22, 32);
    CMP("lut21", lp(adrlit_adr_w_21_lut_main), c->lut21, 32);
    CMP("coord_m", lp(adrlit_adr_block_coord_m_main), c->coord_m, 10); CMP("coord_n", lp(adrlit_adr_block_coord_n_main), c->coord_n, 14);
    CMP("dis", lp(adrlit_adr_w_5x5_dis_main), c->dis5x5, 62);
    { uint8_t *ll[8] = { adrlit_adr_local_b_light3_main, adrlit_adr_local_b_light4_main, adrlit_adr_local_b_light5_main, adrlit_adr_local_b_light6_main,
                         adrlit_adr_local_b_light7_main, adrlit_adr_local_b_light8_main, adrlit_adr_local_b_light9_main, adrlit_adr_local_b_light10_main };
      for (i = 0; i < 8; i++) CMP("light", lp(ll[i]), c->light[i], 22); }
words:
    CMP("knee_x", adrlit_adr_kneepoint_x, t40_adr_knee_x, 32);
    CMP("gauss_local", adrlit_adr_gauss_local, t40_adr_gauss_w, 96);
    CMP("gauss_old", adrlit_adr_gauss_old, t40_adr_gauss_old, 8);
    CMP("cut", adrlit_adr_cut_base_up, t40_adr_cut, 44);
    if (*(uint16_t *)adrlit_adr_ctc1_y_change != t40_adr_ctc1_changed) { printf("  %s: ctc1 changed\n", tag); bad++; }
    if (*(uint16_t *)adrlit_adr_ev_changed != t40_adr_ev_changed || *(uint32_t *)adrlit_adr_ev_top != t40_adr_ev_top || *(uint32_t *)adrlit_adr_ev != t40_adr_ev) { printf("  %s: ev state\n", tag); bad++; }
    if (*(uint16_t *)adrlit_adr_tgain_changed != t40_adr_tgain_changed || *(uint32_t *)adrlit_adr_tgain_top != t40_adr_tgain_top || *(uint32_t *)adrlit_adr_tg_f_top != t40_adr_tg_f_top) { printf("  %s: tgain state\n", tag); bad++; }
    if (*(uint32_t *)adrlit_count_adr_test != t40_adr_frames) { printf("  %s: frames lit %u c %u\n", tag, *(uint32_t *)adrlit_count_adr_test, t40_adr_frames); bad++; }
    if (regtrace_t40_adr_lit_active != regtrace_t40_adr_active) { printf("  %s: active\n", tag); bad++; }
    return bad;
}

enum { S_INIT, S_FRAME, S_EV, S_TG, S_DN, S_DRC, S_CLEAN };
static unsigned step(int what, uint32_t a, uint32_t b, uint64_t ev, uint8_t *bank, const char *tag)
{
    struct wr *la, *lb; unsigned na, nb, bad, d0 = lit_ub, e0;
    tparamsN[0] = (uintptr_t)bank;
    side = 0; st_fill(); e0 = ev_pushed;
    switch (what) {
    case S_INIT: regtrace_adr_main_init_lit(a, b); break;
    case S_FRAME: if (irq_cb[9]) { ((int32_t (*)(void))irq_cb[9])(); if (ev_pushed != e0 && ev_type == 2 && ev_cb[0][2]) ((int32_t (*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t))ev_cb[0][2])(0, 0, 0, 0, 0); } break;
    case S_EV: regtrace_adr_ev_update_lit(ev); break;
    case S_TG: regtrace_adr_tgain_update_lit(a); break;
    case S_DN: regtrace_adr_dn_refresh_lit(0); break;
    case S_DRC: regtrace_adr_drc_ratio_lit(); break;
    case S_CLEAN: regtrace_adr_lit_cleanup(); break;
    }
    la = snap(&na);
    if (lit_ub != d0) { if (getenv("ADRUB")) printf("UB at %s\n", tag);	/* undefined OEM input: resync the module-lifetime words, the bin stops */
        degenerate++; free(la);
        t40_adr_frames = *(uint32_t *)adrlit_count_adr_test; memcpy(t40_adr_knee_x, adrlit_adr_kneepoint_x, 32);
        memcpy(t40_adr_gauss_w, adrlit_adr_gauss_local, 96); memcpy(t40_adr_gauss_old, adrlit_adr_gauss_old, 8);
        memcpy(t40_adr_cut, adrlit_adr_cut_base_up, 44); t40_adr_ctc1_changed = *(uint16_t *)adrlit_adr_ctc1_y_change;
        t40_adr_ev_changed = *(uint16_t *)adrlit_adr_ev_changed; t40_adr_ev_top = *(uint32_t *)adrlit_adr_ev_top; t40_adr_ev = *(uint32_t *)adrlit_adr_ev;
        t40_adr_tgain_changed = *(uint16_t *)adrlit_adr_tgain_changed; t40_adr_tgain_top = *(uint32_t *)adrlit_adr_tgain_top; t40_adr_tg_f_top = *(uint32_t *)adrlit_adr_tg_f_top;
        return 0x10000; }
    side = 1; st_fill(); e0 = ev_pushed;
    switch (what) {
    case S_INIT: t40_adr_init(a, b); break;
    case S_FRAME: if (irq_cb[9]) { ((int32_t (*)(void))irq_cb[9])(); if (ev_pushed != e0 && ev_type == 2 && ev_cb[0][2]) ((int32_t (*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t))ev_cb[0][2])(0, 0, 0, 0, 0); } break;
    case S_EV: t40_adr_ev_update(ev); break;
    case S_TG: t40_adr_tgain_update(a); break;
    case S_DN: t40_adr_dn_refresh(); break;
    case S_DRC: t40_adr_drc_ratio(); break;
    case S_CLEAN: t40_adr_cleanup(); break;
    }
    lb = snap(&nb);
    bad = cmp_logs(tag, la, na, lb, nb) + cmp_state(tag);
    nwr += na;
    free(la); free(lb);
    return bad;
}

/* parameter variants: s16 knots at the OEM parameter pointers */
static void pv(uint8_t *bank, unsigned wal, unsigned k, int16_t v) { int16_t *p = (int16_t *)(void *)(bank + T40_ADR_PAR_OFFSET + t40_adr_wal_lin[wal]); p[k] = v; }
static void patch(uint8_t *bank, unsigned var)
{
    if (!var) return;
    pv(bank, 53, 0, (var - 1) % 4);			/* local light mode 0..3 */
    pv(bank, 64, 0, 1); pv(bank, 64, 5, (var - 1) % 5);	/* curve mix on, selection */
    pv(bank, 64, 6, var % 3); pv(bank, 64, 7, 1 + var % 2); pv(bank, 64, 8, var % 4);
    pv(bank, 61, 9, var % 4); pv(bank, 61, 8, 1 + var % 3); pv(bank, 61, 7, 5 + var % 9);	/* curve filter */
    pv(bank, 61, 0, var & 1); pv(bank, 63, 5, (var >> 1) & 1); pv(bank, 49, 14, (var >> 2) & 1);	/* EV overrides */
    pv(bank, 65, 0, 1); pv(bank, 65, 1, 20 + 40 * (var % 3)); pv(bank, 65, 4, var & 1); pv(bank, 65, 5, 1000); pv(bank, 65, 6, 6000); pv(bank, 65, 7, 300);
    pv(bank, 54, 0, var & 1);				/* envelope */
    pv(bank, 60, 10, (var >> 1) & 1);			/* luma gains */
    pv(bank, 59, 19, var & 1);				/* tone curve through the gamma */
    pv(bank, 49, 15, var & 1);				/* gauss weights */
    *(int16_t *)(void *)(bank + T40_ADR_PAR_OFFSET + 0x60) = var % 6;	/* debug readout selection */
    *(int16_t *)(void *)(bank + T40_ADR_PAR_OFFSET + 0x62) = var % 3;
    if (var >= 6) {	/* full strength, equalisation curve against the rate limit and its clamps, fade index edges */
        unsigned i;
        pv(bank, 53, 0, 3); for (i = 5; i < 29; i++) pv(bank, 53, i, 32000);
        pv(bank, 6, 0, 200); pv(bank, 64, 5, 1); pv(bank, 61, 9, 0);
        for (i = 0; i < 11; i++) pv(bank, 28, i, 3000 + 100 * i);
        pv(bank, 65, 1, var == 6 ? 512 : 3); pv(bank, 65, 4, 1); pv(bank, 65, 5, 0); pv(bank, 65, 6, 20000); pv(bank, 65, 7, var == 6 ? 511 : 700);
    }
}

/* ADRBENCH=<frames>: only the driver chain, one bin, 3840x2160, statistics slots filled once (#133 profile) */
static int adr_bench(const char *path, unsigned n)
{
    struct timespec t0, t1; unsigned f, w0 = 0;
    uint8_t *bin;
    reg_read_hook = st_read;
    st_buf = arena_alloc(4 * 0x1000);
    *(uint32_t *)(tispinfo + 48) = (uint32_t)(uintptr_t)st_buf;
    *(uint32_t *)(tispinfo + 52) = ST_PHYS;
    tune = arena_alloc(4096); memset(tune, 0, 4096);
    bin = load_bin(path);
    tparamsN[0] = (uintptr_t)BANK_DAY(bin);
    side = 1; st_n = 3840 * 2160 / 4; st_mode = 1;
    for (st_slot = 0; st_slot < 4; st_slot++) { st_seed = 2 * st_slot + 1; st_fill(); }
    if (getenv("ADRSTATS")) {	/* camera dumps: "a.bin,b.bin,c.bin" (4 KiB slots) */
        char list[512], *t, *sv = NULL; unsigned k = 0;
        snprintf(list, sizeof(list), "%s", getenv("ADRSTATS"));
        for (t = strtok_r(list, ",", &sv); t && k < 4; t = strtok_r(NULL, ",", &sv), k++) {
            FILE *fs = fopen(t, "rb");
            if (!fs || fread(st_buf + k * 0x1000, 1, 0x1000, fs) != 0x1000) { fprintf(stderr, "bad stats %s\n", t); return 2; }
            fclose(fs);
        }
        for (; k < 4; k++) memcpy(st_buf + k * 0x1000, st_buf, 0x1000);
    }
    t40_adr_init(3840, 2160);
    nlog = 0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (f = 0; f < n; f++) {
        unsigned e0 = ev_pushed;
        st_slot = f % 4;
        if (irq_cb[9]) { ((int32_t (*)(void))irq_cb[9])(); if (ev_pushed != e0 && ev_type == 2 && ev_cb[0][2]) ((int32_t (*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t))ev_cb[0][2])(0, 0, 0, 0, 0); }
        if (!f) w0 = nlog;
        nlog = 0;
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    printf("adr bench: %u frames, %.1f us/frame, %u register writes in frame 0\n", n,
           ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / 1e3 / (n ? n : 1), w0);
    return 0;
}

static int chain_main(int argc, char **argv)
{
    if (getenv("ADRBENCH") && argc > 1)
        return adr_bench(argv[1], strtoul(getenv("ADRBENCH"), 0, 0));
    static const uint32_t res[][2] = { { 3840, 2160 }, { 1920, 1080 }, { 2560, 1440 }, { 1280, 720 } };
    unsigned total = 0, nsteps = 0, k, var;
    total += unit_tests(argc > 1 ? 20000 : 200000);
    reg_read_hook = st_read;
    st_buf = arena_alloc(4 * 0x1000);
    *(uint32_t *)(tispinfo + 48) = (uint32_t)(uintptr_t)st_buf;
    *(uint32_t *)(tispinfo + 52) = ST_PHYS;
    tune = arena_alloc(4096); memset(tune, 0, 4096);
    for (k = 1; k < (unsigned)argc; k++) {
        size_t mark = used;
        uint8_t *bin0 = load_bin(argv[k]), *bin = arena_alloc(BIN_BYTES); unsigned bad = 0, r, f, stop = 0;
        char tag[96];
        for (var = 0; var < 8 && !stop; var++) {
            memcpy(bin, bin0, BIN_BYTES);
            patch(BANK_DAY(bin), var); patch(BANK_NIGHT(bin), var);
            lcg_state = 31 + 7 * k + var;
            for (r = 0; r < sizeof(res) / sizeof(res[0]) && !stop; r++) {
                int night = r & 1; uint32_t w = res[r][0], h = res[r][1], v; uint64_t ev = 0;
#define DO(...) do { v = step(__VA_ARGS__); nsteps++; if (v & 0x10000) { stop = 1; } else bad += v; } while (0)
                st_n = w * h / 4;
                snprintf(tag, sizeof(tag), "var %u init %ux%u", var, w, h);
                DO(S_INIT, w, h, 0, night ? BANK_NIGHT(bin) : BANK_DAY(bin), tag);
                for (f = 0; f < 30 && !stop; f++) {
                    uint32_t x = lcg(); uint8_t *bank = night ? BANK_NIGHT(bin) : BANK_DAY(bin);
                    st_seed = lcg() | 1; st_slot = x % 5 == 4 ? 6 : x % 4; st_mode = (x >> 8) % 4;
                    if (x % 3 == 0) { ev = (x >> 4) % 2 ? (uint64_t)(lcg() % 400000) << 10 : ev + ((uint64_t)(lcg() % 4000) << 10); DO(S_EV, 0, 0, ev, bank, "ev"); }
                    if (x % 4 == 1) DO(S_TG, lcg() % (20u << 16), 0, 0, bank, "tgain");
                    if (x % 13 == 3) { static const uint8_t st[] = { 0, 1, 64, 128, 129, 200, 255 };
                        *(uint32_t *)(tune + 848) = lcg() % 3; tune[852] = st[lcg() % 7]; DO(S_DRC, 0, 0, 0, bank, "drc"); }
                    if (x % 17 == 5) { night = !night; DO(S_DN, 0, 0, 0, night ? BANK_NIGHT(bin) : BANK_DAY(bin), "dn"); }
                    snprintf(tag, sizeof(tag), "var %u %ux%u frame %u mode %u slot %u", var, w, h, f, st_mode, st_slot);
                    DO(S_FRAME, 0, 0, 0, night ? BANK_NIGHT(bin) : BANK_DAY(bin), tag);
                    if (bad > 20) stop = 1;
                }
                if (r % 2) DO(S_CLEAN, 0, 0, 0, BANK_DAY(bin), "cleanup");
            }
        }
        if (stop) { unsigned dn; side = 0; regtrace_adr_lit_cleanup(); side = 1; t40_adr_cleanup(); free(snap(&dn)); }
        else { unsigned v = step(S_CLEAN, 0, 0, 0, BANK_DAY(bin), "cleanup"); nsteps++; bad += v; }
        printf("%s: %s%s\n", argv[k], bad ? "DIFF" : "bit-exact", stop && !bad ? " (stopped at an input where the OEM code is undefined)" : "");
        total += bad;
        used = mark;
    }
    printf("adr: %d bins, %u steps, %lu register writes compared, %u differences, %u bins stopped at undefined OEM behaviour\n", argc - 1, nsteps, nwr, total, degenerate);
    return total != 0;
}
