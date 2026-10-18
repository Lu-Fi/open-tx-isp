/*
 * Literal mips2c CDNS (chroma denoise) chain from the OEM tx-isp-t40.ko
 * (sc830ai production module; same tuning-bin layout as the SDNS lit port:
 * SDNS bank +0x17b00, CDNS bank +0x11b8b).  Self-contained _lit
 * implementation; the recovered fragment tisp_cdns_* functions stay unused
 * because their lift is broken (tail calls "jr.hb t9" to all_reg_refresh
 * are unmatched fragments, params_refresh copies onto tparamsN itself, and
 * par_refresh addresses its gain-old word through &sclk_name - 14092).
 *
 * OEM unit: reg_trig ((ch+0x380)<<7)+4 = 0x1c004 main.  Params 683 B,
 * comb 120 B (30 ptrs), intp working buf 32 B.  Init (OEM
 * tisp_cdns_main_init): params_refresh(0) -> wdr_en(0, flags) ->
 * gain_old = -1 -> refresh(0, 0x10000).  The tgain callback (event 6)
 * calls tisp_cdns_refresh(0, tgain); here the frame-done DNS block calls
 * tisp_cdns_par_refresh_lit with the same 16.16 log2 gain (dns_ev).
 *
 * enable_cdns=1 runs it and clears TOP 0x40 bit 19 only when init
 * returned 0.  Default 0 until a device run.
 */
static bool regtrace_enable_cdns;
static int regtrace_cdns_dbg_init_ret = -1;
static unsigned int regtrace_cdns_init_count;
static unsigned int regtrace_cdns_refresh_count;
static unsigned int regtrace_cdns_skip_count;
static uint32_t regtrace_cdns_gain_now;
/* OEM .data threshold word: 0x10000 at load, cdns_remove sets 256 */
static uint regtrace_cdns_thresh = 0x10000U;
module_param_named(cdns_thresh, regtrace_cdns_thresh, uint, 0644);
module_param_named(enable_cdns, regtrace_enable_cdns, bool, 0644);
MODULE_PARM_DESC(enable_cdns, "1 runs the source-derived CDNS chroma denoise and clears TOP bit 19 (default 0)");
module_param_named(cdns_dbg_init_ret, regtrace_cdns_dbg_init_ret, int, 0444);
module_param_named(cdns_init_count, regtrace_cdns_init_count, uint, 0444);
module_param_named(cdns_refresh_count, regtrace_cdns_refresh_count, uint, 0444);
module_param_named(cdns_skip_count, regtrace_cdns_skip_count, uint, 0444);
module_param_named(cdns_gain_now, regtrace_cdns_gain_now, uint, 0444);

/* OEM .bss 0x46f8 (wdr flag per channel, 8 bytes; the recovered global
 * cdns_wdr_en is only 4) and the .data gain-old pair / threshold. */
static uint32_t regtrace_cdns_wdr_en_flags[2];
static uint32_t regtrace_cdns_gain_old[2] = { 0xffffffffU, 0xffffffffU };

#undef REGCALL_system_reg_write
#undef REGCALL_tisp_simple_intp_int8
#define REGCALL_system_reg_write(a, b, c, d) system_reg_write((a), (b))
#define REGCALL_tisp_simple_intp_int8(a, b, c, d) \
    tisp_simple_intp_int8((int32_t)(a), (int32_t)(b), (void *)(uintptr_t)(c))

static int32_t tisp_cdns_reg_trig_lit(uint32_t a0_in);
static int32_t tisp_cdns_intp_lit(uint32_t a0_in, uint32_t a1_in);
static int32_t tisp_cdns_reg_cfg_lit(uint32_t a0_in);
static int32_t tisp_cdns_wdr_en_lit(uint32_t a0_in, uint32_t a1_in);
static int32_t tisp_cdns_par_refresh_lit(uint32_t ch, uint32_t gain, uint32_t thresh);
static int32_t regtrace_cdns_main_init_lit(void);

static int32_t tisp_cdns_reg_trig_lit(uint32_t a0_in)
{
    uint32_t r_v0 = 0, r_v1 = 0;
    uint32_t r_t0 = 0, r_t1 = 0, r_t2 = 0, r_t3 = 0, r_t4 = 0, r_t5 = 0;
    uint32_t r_t6 = 0, r_t7 = 0, r_t8 = 0, r_t9 = 0;
    uint32_t r_s0 = 0, r_s1 = 0, r_s2 = 0, r_s3 = 0, r_s4 = 0, r_s5 = 0;
    uint32_t r_s6 = 0, r_s7 = 0, r_s8 = 0, r_ra = 0, r_at = 0;
    uint32_t mips_lo = 0, mips_hi = 0, mips_cond = 0;
    uint32_t r_a0 = a0_in;
    uint32_t r_a1 = 0;
    uint32_t r_a2 = 0;
    uint32_t r_a3 = 0;
    uint8_t mips_stack[448] __attribute__((aligned(8)));
    uint32_t r_sp = (uint32_t)(uintptr_t)(mips_stack + 384);

    (void)r_at; (void)r_ra; (void)r_t8; (void)r_t9; (void)mips_lo;
    (void)mips_hi; (void)r_sp; (void)mips_cond; (void)r_a2; (void)r_a3;
/* literal translation of tisp_cdns_reg_trig; warnings: 1 */
/* WARN 612f0: jr via t9 (tail call sym=system_reg_write) */
    r_a0 = r_a0 + 0x380U;
    r_a0 = r_a0 << 7;
    r_t9 = (uint32_t)(uintptr_t)&system_reg_write; /* HI16 system_reg_write */
    r_a1 = 1U;
    r_t9 = (uint32_t)(uintptr_t)&system_reg_write;
    r_a0 = r_a0 + 0x4U;
    r_v0 = (uint32_t)REGCALL_system_reg_write(r_a0, r_a1, r_a2, r_a3); goto fn_exit;
fn_exit:
    return (int32_t)r_v0;
}

static int32_t tisp_cdns_intp_lit(uint32_t a0_in, uint32_t a1_in)
{
    uint32_t r_v0 = 0, r_v1 = 0;
    uint32_t r_t0 = 0, r_t1 = 0, r_t2 = 0, r_t3 = 0, r_t4 = 0, r_t5 = 0;
    uint32_t r_t6 = 0, r_t7 = 0, r_t8 = 0, r_t9 = 0;
    uint32_t r_s0 = 0, r_s1 = 0, r_s2 = 0, r_s3 = 0, r_s4 = 0, r_s5 = 0;
    uint32_t r_s6 = 0, r_s7 = 0, r_s8 = 0, r_ra = 0, r_at = 0;
    uint32_t mips_lo = 0, mips_hi = 0, mips_cond = 0;
    uint32_t r_a0 = a0_in;
    uint32_t r_a1 = a1_in;
    uint32_t r_a2 = 0;
    uint32_t r_a3 = 0;
    uint8_t mips_stack[448] __attribute__((aligned(8)));
    uint32_t r_sp = (uint32_t)(uintptr_t)(mips_stack + 384);

    (void)r_at; (void)r_ra; (void)r_t8; (void)r_t9; (void)mips_lo;
    (void)mips_hi; (void)r_sp; (void)mips_cond; (void)r_a2; (void)r_a3;
/* literal translation of tisp_cdns_intp; warnings: 0 */
    r_sp = r_sp + -0x30U;
    *(uint32_t *)(uintptr_t)(r_sp + 44) = (uint32_t)r_ra;
    *(uint32_t *)(uintptr_t)(r_sp + 40) = (uint32_t)r_s5;
    *(uint32_t *)(uintptr_t)(r_sp + 36) = (uint32_t)r_s4;
    *(uint32_t *)(uintptr_t)(r_sp + 32) = (uint32_t)r_s3;
    *(uint32_t *)(uintptr_t)(r_sp + 28) = (uint32_t)r_s2;
    *(uint32_t *)(uintptr_t)(r_sp + 24) = (uint32_t)r_s1;
    mips_cond = (r_a0 == 0) ? 1U : 0U;
    *(uint32_t *)(uintptr_t)(r_sp + 20) = (uint32_t)r_s0;
    if (mips_cond) goto L_615e8;
    r_v0 = 0; /* HI16 .bss anchor */
    r_s5 = *(uint32_t *)(uintptr_t)((uintptr_t)&sec_cdns);
    r_v0 = 0; /* HI16 .bss anchor */
    r_s2 = *(uint32_t *)(uintptr_t)((uintptr_t)&sec_cdns_intp);
    r_v0 = 0; /* HI16 .bss anchor */
    r_s4 = *(uint32_t *)(uintptr_t)((uintptr_t)&sec_cdns_comb);
L_61334:
    r_s3 = (uint32_t)((int32_t)r_a1 >> 16);
    r_s0 = r_a1 & 0xffffU;
    r_s1 = (uint32_t)(uintptr_t)&tisp_simple_intp_int8; /* HI16 tisp_simple_intp_int8 */
    r_s1 = (uint32_t)(uintptr_t)&tisp_simple_intp_int8;
    r_a2 = r_s5 + 0x11fU;
    r_a1 = r_s0;
    r_a0 = r_s3;
    r_v0 = (uint32_t)REGCALL_tisp_simple_intp_int8(r_a0, r_a1, r_a2, r_a3);
    *(uint8_t *)(uintptr_t)(r_s2 + 26) = (uint8_t)r_v0;
    r_a2 = r_s5 + 0x12aU;
    r_a1 = r_s0;
    r_a0 = r_s3;
    r_v0 = (uint32_t)REGCALL_tisp_simple_intp_int8(r_a0, r_a1, r_a2, r_a3);
    *(uint8_t *)(uintptr_t)(r_s2 + 27) = (uint8_t)r_v0;
    r_a2 = *(uint32_t *)(uintptr_t)(r_s4 + 0);
    r_a1 = r_s0;
    r_a0 = r_s3;
    r_v0 = (uint32_t)REGCALL_tisp_simple_intp_int8(r_a0, r_a1, r_a2, r_a3);
    *(uint8_t *)(uintptr_t)(r_s2 + 0) = (uint8_t)r_v0;
    r_a2 = *(uint32_t *)(uintptr_t)(r_s4 + 4);
    r_a1 = r_s0;
    r_a0 = r_s3;
    r_v0 = (uint32_t)REGCALL_tisp_simple_intp_int8(r_a0, r_a1, r_a2, r_a3);
    *(uint8_t *)(uintptr_t)(r_s2 + 1) = (uint8_t)r_v0;
    r_a2 = *(uint32_t *)(uintptr_t)(r_s4 + 8);
    r_a1 = r_s0;
    r_a0 = r_s3;
    r_v0 = (uint32_t)REGCALL_tisp_simple_intp_int8(r_a0, r_a1, r_a2, r_a3);
    *(uint8_t *)(uintptr_t)(r_s2 + 2) = (uint8_t)r_v0;
    r_a2 = *(uint32_t *)(uintptr_t)(r_s4 + 12);
    r_a1 = r_s0;
    r_a0 = r_s3;
    r_v0 = (uint32_t)REGCALL_tisp_simple_intp_int8(r_a0, r_a1, r_a2, r_a3);
    *(uint8_t *)(uintptr_t)(r_s2 + 3) = (uint8_t)r_v0;
    r_a2 = *(uint32_t *)(uintptr_t)(r_s4 + 16);
    r_a1 = r_s0;
    r_a0 = r_s3;
    r_v0 = (uint32_t)REGCALL_tisp_simple_intp_int8(r_a0, r_a1, r_a2, r_a3);
    *(uint8_t *)(uintptr_t)(r_s2 + 4) = (uint8_t)r_v0;
    r_a2 = *(uint32_t *)(uintptr_t)(r_s4 + 20);
    r_a1 = r_s0;
    r_a0 = r_s3;
    r_v0 = (uint32_t)REGCALL_tisp_simple_intp_int8(r_a0, r_a1, r_a2, r_a3);
    *(uint8_t *)(uintptr_t)(r_s2 + 5) = (uint8_t)r_v0;
    r_a2 = *(uint32_t *)(uintptr_t)(r_s4 + 24);
    r_a1 = r_s0;
    r_a0 = r_s3;
    r_v0 = (uint32_t)REGCALL_tisp_simple_intp_int8(r_a0, r_a1, r_a2, r_a3);
    *(uint8_t *)(uintptr_t)(r_s2 + 6) = (uint8_t)r_v0;
    r_a2 = *(uint32_t *)(uintptr_t)(r_s4 + 28);
    r_a1 = r_s0;
    r_a0 = r_s3;
    r_v0 = (uint32_t)REGCALL_tisp_simple_intp_int8(r_a0, r_a1, r_a2, r_a3);
    *(uint8_t *)(uintptr_t)(r_s2 + 7) = (uint8_t)r_v0;
    r_a2 = *(uint32_t *)(uintptr_t)(r_s4 + 32);
    r_a1 = r_s0;
    r_a0 = r_s3;
    r_v0 = (uint32_t)REGCALL_tisp_simple_intp_int8(r_a0, r_a1, r_a2, r_a3);
    *(uint8_t *)(uintptr_t)(r_s2 + 8) = (uint8_t)r_v0;
    r_a2 = *(uint32_t *)(uintptr_t)(r_s4 + 36);
    r_a1 = r_s0;
    r_a0 = r_s3;
    r_v0 = (uint32_t)REGCALL_tisp_simple_intp_int8(r_a0, r_a1, r_a2, r_a3);
    *(uint8_t *)(uintptr_t)(r_s2 + 9) = (uint8_t)r_v0;
    r_a2 = *(uint32_t *)(uintptr_t)(r_s4 + 40);
    r_a1 = r_s0;
    r_a0 = r_s3;
    r_v0 = (uint32_t)REGCALL_tisp_simple_intp_int8(r_a0, r_a1, r_a2, r_a3);
    *(uint8_t *)(uintptr_t)(r_s2 + 10) = (uint8_t)r_v0;
    r_a2 = *(uint32_t *)(uintptr_t)(r_s4 + 44);
    r_a1 = r_s0;
    r_a0 = r_s3;
    r_v0 = (uint32_t)REGCALL_tisp_simple_intp_int8(r_a0, r_a1, r_a2, r_a3);
    *(uint8_t *)(uintptr_t)(r_s2 + 11) = (uint8_t)r_v0;
    r_a2 = *(uint32_t *)(uintptr_t)(r_s4 + 48);
    r_a1 = r_s0;
    r_a0 = r_s3;
    r_v0 = (uint32_t)REGCALL_tisp_simple_intp_int8(r_a0, r_a1, r_a2, r_a3);
    *(uint8_t *)(uintptr_t)(r_s2 + 12) = (uint8_t)r_v0;
    r_a2 = *(uint32_t *)(uintptr_t)(r_s4 + 52);
    r_a1 = r_s0;
    r_a0 = r_s3;
    r_v0 = (uint32_t)REGCALL_tisp_simple_intp_int8(r_a0, r_a1, r_a2, r_a3);
    *(uint8_t *)(uintptr_t)(r_s2 + 13) = (uint8_t)r_v0;
    r_a2 = *(uint32_t *)(uintptr_t)(r_s4 + 56);
    r_a1 = r_s0;
    r_a0 = r_s3;
    r_v0 = (uint32_t)REGCALL_tisp_simple_intp_int8(r_a0, r_a1, r_a2, r_a3);
    *(uint8_t *)(uintptr_t)(r_s2 + 14) = (uint8_t)r_v0;
    r_a2 = *(uint32_t *)(uintptr_t)(r_s4 + 60);
    r_a1 = r_s0;
    r_a0 = r_s3;
    r_v0 = (uint32_t)REGCALL_tisp_simple_intp_int8(r_a0, r_a1, r_a2, r_a3);
    *(uint8_t *)(uintptr_t)(r_s2 + 15) = (uint8_t)r_v0;
    r_a2 = *(uint32_t *)(uintptr_t)(r_s4 + 64);
    r_a1 = r_s0;
    r_a0 = r_s3;
    r_v0 = (uint32_t)REGCALL_tisp_simple_intp_int8(r_a0, r_a1, r_a2, r_a3);
    *(uint8_t *)(uintptr_t)(r_s2 + 16) = (uint8_t)r_v0;
    r_a2 = *(uint32_t *)(uintptr_t)(r_s4 + 68);
    r_a1 = r_s0;
    r_a0 = r_s3;
    r_v0 = (uint32_t)REGCALL_tisp_simple_intp_int8(r_a0, r_a1, r_a2, r_a3);
    *(uint8_t *)(uintptr_t)(r_s2 + 17) = (uint8_t)r_v0;
    r_a2 = *(uint32_t *)(uintptr_t)(r_s4 + 72);
    r_a1 = r_s0;
    r_a0 = r_s3;
    r_v0 = (uint32_t)REGCALL_tisp_simple_intp_int8(r_a0, r_a1, r_a2, r_a3);
    *(uint8_t *)(uintptr_t)(r_s2 + 18) = (uint8_t)r_v0;
    r_a2 = *(uint32_t *)(uintptr_t)(r_s4 + 76);
    r_a1 = r_s0;
    r_a0 = r_s3;
    r_v0 = (uint32_t)REGCALL_tisp_simple_intp_int8(r_a0, r_a1, r_a2, r_a3);
    *(uint8_t *)(uintptr_t)(r_s2 + 19) = (uint8_t)r_v0;
    r_a2 = *(uint32_t *)(uintptr_t)(r_s4 + 80);
    r_a1 = r_s0;
    r_a0 = r_s3;
    r_v0 = (uint32_t)REGCALL_tisp_simple_intp_int8(r_a0, r_a1, r_a2, r_a3);
    *(uint8_t *)(uintptr_t)(r_s2 + 20) = (uint8_t)r_v0;
    r_a2 = *(uint32_t *)(uintptr_t)(r_s4 + 84);
    r_a1 = r_s0;
    r_a0 = r_s3;
    r_v0 = (uint32_t)REGCALL_tisp_simple_intp_int8(r_a0, r_a1, r_a2, r_a3);
    *(uint8_t *)(uintptr_t)(r_s2 + 21) = (uint8_t)r_v0;
    r_a2 = *(uint32_t *)(uintptr_t)(r_s4 + 88);
    r_a1 = r_s0;
    r_a0 = r_s3;
    r_v0 = (uint32_t)REGCALL_tisp_simple_intp_int8(r_a0, r_a1, r_a2, r_a3);
    *(uint8_t *)(uintptr_t)(r_s2 + 22) = (uint8_t)r_v0;
    r_a2 = *(uint32_t *)(uintptr_t)(r_s4 + 92);
    r_a1 = r_s0;
    r_a0 = r_s3;
    r_v0 = (uint32_t)REGCALL_tisp_simple_intp_int8(r_a0, r_a1, r_a2, r_a3);
    *(uint8_t *)(uintptr_t)(r_s2 + 23) = (uint8_t)r_v0;
    r_a2 = *(uint32_t *)(uintptr_t)(r_s4 + 96);
    r_a1 = r_s0;
    r_a0 = r_s3;
    r_v0 = (uint32_t)REGCALL_tisp_simple_intp_int8(r_a0, r_a1, r_a2, r_a3);
    *(uint8_t *)(uintptr_t)(r_s2 + 24) = (uint8_t)r_v0;
    r_a2 = *(uint32_t *)(uintptr_t)(r_s4 + 100);
    r_a1 = r_s0;
    r_a0 = r_s3;
    r_v0 = (uint32_t)REGCALL_tisp_simple_intp_int8(r_a0, r_a1, r_a2, r_a3);
    *(uint8_t *)(uintptr_t)(r_s2 + 25) = (uint8_t)r_v0;
    r_a2 = *(uint32_t *)(uintptr_t)(r_s4 + 104);
    r_a1 = r_s0;
    r_a0 = r_s3;
    r_v0 = (uint32_t)REGCALL_tisp_simple_intp_int8(r_a0, r_a1, r_a2, r_a3);
    *(uint8_t *)(uintptr_t)(r_s2 + 28) = (uint8_t)r_v0;
    r_a2 = *(uint32_t *)(uintptr_t)(r_s4 + 108);
    r_a1 = r_s0;
    r_a0 = r_s3;
    r_v0 = (uint32_t)REGCALL_tisp_simple_intp_int8(r_a0, r_a1, r_a2, r_a3);
    *(uint8_t *)(uintptr_t)(r_s2 + 29) = (uint8_t)r_v0;
    r_a2 = *(uint32_t *)(uintptr_t)(r_s4 + 112);
    r_a1 = r_s0;
    r_a0 = r_s3;
    r_v0 = (uint32_t)REGCALL_tisp_simple_intp_int8(r_a0, r_a1, r_a2, r_a3);
    *(uint8_t *)(uintptr_t)(r_s2 + 30) = (uint8_t)r_v0;
    r_a2 = *(uint32_t *)(uintptr_t)(r_s4 + 116);
    r_a1 = r_s0;
    r_a0 = r_s3;
    r_v0 = (uint32_t)REGCALL_tisp_simple_intp_int8(r_a0, r_a1, r_a2, r_a3);
    *(uint8_t *)(uintptr_t)(r_s2 + 31) = (uint8_t)r_v0;
    r_ra = *(uint32_t *)(uintptr_t)(r_sp + 44);
    r_s5 = *(uint32_t *)(uintptr_t)(r_sp + 40);
    r_s4 = *(uint32_t *)(uintptr_t)(r_sp + 36);
    r_s3 = *(uint32_t *)(uintptr_t)(r_sp + 32);
    r_s2 = *(uint32_t *)(uintptr_t)(r_sp + 28);
    r_s1 = *(uint32_t *)(uintptr_t)(r_sp + 24);
    r_s0 = *(uint32_t *)(uintptr_t)(r_sp + 20);
    r_sp = r_sp + 0x30U;
    goto fn_exit;
L_615e8:
    r_v0 = 0; /* HI16 .bss anchor */
    r_s5 = *(uint32_t *)(uintptr_t)((uintptr_t)&main_cdns);
    r_v0 = 0; /* HI16 .bss anchor */
    r_s2 = *(uint32_t *)(uintptr_t)((uintptr_t)&main_cdns_intp);
    r_v0 = 0; /* HI16 .bss anchor */
    r_s4 = *(uint32_t *)(uintptr_t)((uintptr_t)&main_cdns_comb);
    goto L_61334;
fn_exit:
    return (int32_t)r_v0;
}

static int32_t tisp_cdns_reg_cfg_lit(uint32_t a0_in)
{
    uint32_t r_v0 = 0, r_v1 = 0;
    uint32_t r_t0 = 0, r_t1 = 0, r_t2 = 0, r_t3 = 0, r_t4 = 0, r_t5 = 0;
    uint32_t r_t6 = 0, r_t7 = 0, r_t8 = 0, r_t9 = 0;
    uint32_t r_s0 = 0, r_s1 = 0, r_s2 = 0, r_s3 = 0, r_s4 = 0, r_s5 = 0;
    uint32_t r_s6 = 0, r_s7 = 0, r_s8 = 0, r_ra = 0, r_at = 0;
    uint32_t mips_lo = 0, mips_hi = 0, mips_cond = 0;
    uint32_t r_a0 = a0_in;
    uint32_t r_a1 = 0;
    uint32_t r_a2 = 0;
    uint32_t r_a3 = 0;
    uint8_t mips_stack[448] __attribute__((aligned(8)));
    uint32_t r_sp = (uint32_t)(uintptr_t)(mips_stack + 384);

    (void)r_at; (void)r_ra; (void)r_t8; (void)r_t9; (void)mips_lo;
    (void)mips_hi; (void)r_sp; (void)mips_cond; (void)r_a2; (void)r_a3;
/* literal translation of tisp_cdns_reg_cfg; warnings: 1 */
/* WARN 612b4: jr via t9 (tail call sym=system_reg_write) */
    r_sp = r_sp + -0x28U;
    *(uint32_t *)(uintptr_t)(r_sp + 36) = (uint32_t)r_ra;
    *(uint32_t *)(uintptr_t)(r_sp + 32) = (uint32_t)r_s3;
    *(uint32_t *)(uintptr_t)(r_sp + 28) = (uint32_t)r_s2;
    *(uint32_t *)(uintptr_t)(r_sp + 24) = (uint32_t)r_s1;
    mips_cond = (r_a0 == 0) ? 1U : 0U;
    *(uint32_t *)(uintptr_t)(r_sp + 20) = (uint32_t)r_s0;
    if (mips_cond) goto L_612bc;
    r_v0 = 0; /* HI16 .bss anchor */
    r_v1 = 0; /* HI16 .bss anchor */
    r_v0 = *(uint32_t *)(uintptr_t)((uintptr_t)&sec_cdns);
    r_s0 = *(uint32_t *)(uintptr_t)((uintptr_t)&sec_cdns_intp);
L_6108c:
    r_a0 = r_a0 + 0x380U;
    r_a1 = *(uint8_t *)(uintptr_t)(r_v0 + 0);
    r_s1 = r_a0 << 7;
    r_s2 = (uint32_t)(uintptr_t)&system_reg_write; /* HI16 system_reg_write */
    r_s2 = (uint32_t)(uintptr_t)&system_reg_write;
    r_a0 = r_s1 + 0x8U;
    r_v0 = (uint32_t)REGCALL_system_reg_write(r_a0, r_a1, r_a2, r_a3);
    r_a1 = *(uint8_t *)(uintptr_t)(r_s0 + 0);
    r_a0 = r_s1 + 0x10U;
    r_s3 = 0xff0000U;
    r_a1 = r_a1 & 0xfU;
    r_v0 = (uint32_t)REGCALL_system_reg_write(r_a0, r_a1, r_a2, r_a3);
    r_a1 = (uint32_t)(int8_t)*(uint8_t *)(uintptr_t)(r_s0 + 3);
    r_v0 = *(uint8_t *)(uintptr_t)(r_s0 + 2);
    r_a0 = r_s1 + 0x20U;
    r_a1 = r_a1 << 16;
    r_a1 = r_a1 & r_s3;
    r_v0 = r_v0 << 8;
    r_v0 = r_v0 | r_a1;
    r_a1 = *(uint8_t *)(uintptr_t)(r_s0 + 1);
    r_a1 = r_v0 | r_a1;
    r_v0 = (uint32_t)REGCALL_system_reg_write(r_a0, r_a1, r_a2, r_a3);
    r_a1 = (uint32_t)(int8_t)*(uint8_t *)(uintptr_t)(r_s0 + 6);
    r_v0 = *(uint8_t *)(uintptr_t)(r_s0 + 5);
    r_a0 = r_s1 + 0x24U;
    r_a1 = r_a1 << 16;
    r_a1 = r_a1 & r_s3;
    r_v0 = r_v0 << 8;
    r_v0 = r_v0 | r_a1;
    r_a1 = *(uint8_t *)(uintptr_t)(r_s0 + 4);
    r_a1 = r_v0 | r_a1;
    r_v0 = (uint32_t)REGCALL_system_reg_write(r_a0, r_a1, r_a2, r_a3);
    r_a1 = (uint32_t)(int8_t)*(uint8_t *)(uintptr_t)(r_s0 + 9);
    r_v0 = *(uint8_t *)(uintptr_t)(r_s0 + 8);
    r_a0 = r_s1 + 0x28U;
    r_a1 = r_a1 << 16;
    r_a1 = r_a1 & r_s3;
    r_v0 = r_v0 << 8;
    r_v0 = r_v0 | r_a1;
    r_a1 = *(uint8_t *)(uintptr_t)(r_s0 + 7);
    r_a1 = r_v0 | r_a1;
    r_v0 = (uint32_t)REGCALL_system_reg_write(r_a0, r_a1, r_a2, r_a3);
    r_a1 = (uint32_t)(int8_t)*(uint8_t *)(uintptr_t)(r_s0 + 12);
    r_v0 = *(uint8_t *)(uintptr_t)(r_s0 + 11);
    r_a0 = r_s1 + 0x2cU;
    r_a1 = r_a1 << 16;
    r_a1 = r_a1 & r_s3;
    r_v0 = r_v0 << 8;
    r_v0 = r_v0 | r_a1;
    r_a1 = *(uint8_t *)(uintptr_t)(r_s0 + 10);
    r_a1 = r_v0 | r_a1;
    r_v0 = (uint32_t)REGCALL_system_reg_write(r_a0, r_a1, r_a2, r_a3);
    r_a1 = (uint32_t)(int8_t)*(uint8_t *)(uintptr_t)(r_s0 + 15);
    r_v0 = *(uint8_t *)(uintptr_t)(r_s0 + 14);
    r_a0 = r_s1 + 0x30U;
    r_a1 = r_a1 << 16;
    r_a1 = r_a1 & r_s3;
    r_v0 = r_v0 << 8;
    r_v0 = r_v0 | r_a1;
    r_a1 = *(uint8_t *)(uintptr_t)(r_s0 + 13);
    r_a1 = r_v0 | r_a1;
    r_v0 = (uint32_t)REGCALL_system_reg_write(r_a0, r_a1, r_a2, r_a3);
    r_a1 = (uint32_t)(int8_t)*(uint8_t *)(uintptr_t)(r_s0 + 18);
    r_v0 = *(uint8_t *)(uintptr_t)(r_s0 + 17);
    r_a0 = r_s1 + 0x34U;
    r_a1 = r_a1 << 16;
    r_a1 = r_a1 & r_s3;
    r_v0 = r_v0 << 8;
    r_v0 = r_v0 | r_a1;
    r_a1 = *(uint8_t *)(uintptr_t)(r_s0 + 16);
    r_a1 = r_v0 | r_a1;
    r_v0 = (uint32_t)REGCALL_system_reg_write(r_a0, r_a1, r_a2, r_a3);
    r_a1 = (uint32_t)(int8_t)*(uint8_t *)(uintptr_t)(r_s0 + 21);
    r_v0 = *(uint8_t *)(uintptr_t)(r_s0 + 20);
    r_a0 = r_s1 + 0x38U;
    r_a1 = r_a1 << 16;
    r_a1 = r_a1 & r_s3;
    r_v0 = r_v0 << 8;
    r_v0 = r_v0 | r_a1;
    r_a1 = *(uint8_t *)(uintptr_t)(r_s0 + 19);
    r_a1 = r_v0 | r_a1;
    r_v0 = (uint32_t)REGCALL_system_reg_write(r_a0, r_a1, r_a2, r_a3);
    r_a1 = (uint32_t)(int8_t)*(uint8_t *)(uintptr_t)(r_s0 + 24);
    r_v0 = *(uint8_t *)(uintptr_t)(r_s0 + 23);
    r_a0 = r_s1 + 0x3cU;
    r_a1 = r_a1 << 16;
    r_a1 = r_a1 & r_s3;
    r_v0 = r_v0 << 8;
    r_v0 = r_v0 | r_a1;
    r_a1 = *(uint8_t *)(uintptr_t)(r_s0 + 22);
    r_a1 = r_v0 | r_a1;
    r_v0 = (uint32_t)REGCALL_system_reg_write(r_a0, r_a1, r_a2, r_a3);
    r_a1 = (uint32_t)(int8_t)*(uint8_t *)(uintptr_t)(r_s0 + 27);
    r_v0 = *(uint8_t *)(uintptr_t)(r_s0 + 26);
    r_v1 = 0x3000000U;
    r_a1 = r_a1 << 16;
    r_s3 = r_a1 & r_s3;
    r_a1 = *(uint8_t *)(uintptr_t)(r_s0 + 25);
    r_v0 = r_v0 << 8;
    r_v0 = r_v0 | r_s3;
    r_v0 = r_v0 | r_a1;
    r_a1 = *(uint8_t *)(uintptr_t)(r_s0 + 28);
    r_a0 = r_s1 + 0x40U;
    r_a1 = r_a1 << 24;
    r_a1 = r_a1 & r_v1;
    r_v0 = r_v0 | r_a1;
    r_a1 = *(uint8_t *)(uintptr_t)(r_s0 + 29);
    r_v1 = 0x30000000U;
    r_a1 = r_a1 << 28;
    r_a1 = r_a1 & r_v1;
    r_a1 = r_v0 | r_a1;
    r_v0 = (uint32_t)REGCALL_system_reg_write(r_a0, r_a1, r_a2, r_a3);
    r_a1 = (uint32_t)(int8_t)*(uint8_t *)(uintptr_t)(r_s0 + 30);
    r_v0 = (uint32_t)(int8_t)*(uint8_t *)(uintptr_t)(r_s0 + 31);
    r_v1 = ((int32_t)r_v0 < (int32_t)r_a1) ? 1U : 0U;
    mips_cond = (r_v1 != 0) ? 1U : 0U;
    /* nop */
    if (mips_cond) goto L_612d0;
    mips_cond = (r_a1 == r_v0) ? 1U : 0U;
    r_v1 = 0;
    if (mips_cond) goto L_61278;
    r_v1 = r_v0 - r_a1;
    r_a0 = 4080U;
    if (r_v1) { mips_lo = (uint32_t)((int32_t)r_a0 / (int32_t)r_v1); mips_hi = (uint32_t)((int32_t)r_a0 % (int32_t)r_v1); }
    r_v1 = mips_lo;
L_61278:
    r_a0 = 0xfff0000U;
    r_v0 = r_v0 << 8;
    r_v1 = r_v1 << 16;
    r_v1 = r_v1 & r_a0;
    r_ra = *(uint32_t *)(uintptr_t)(r_sp + 36);
    r_s3 = *(uint32_t *)(uintptr_t)(r_sp + 32);
    r_s0 = *(uint32_t *)(uintptr_t)(r_sp + 20);
    r_v0 = r_v0 & 0xffffU;
    r_a0 = r_s1 + 0x50U;
    r_t9 = r_s2;
    r_s1 = *(uint32_t *)(uintptr_t)(r_sp + 24);
    r_s2 = *(uint32_t *)(uintptr_t)(r_sp + 28);
    r_v0 = r_v0 | r_v1;
    r_a1 = r_a1 & 0xffU;
    r_a1 = r_v0 | r_a1;
    r_sp = r_sp + 0x28U;
    r_v0 = (uint32_t)REGCALL_system_reg_write(r_a0, r_a1, r_a2, r_a3); goto fn_exit;
L_612bc:
    r_v0 = 0; /* HI16 .bss anchor */
    r_v1 = 0; /* HI16 .bss anchor */
    r_v0 = *(uint32_t *)(uintptr_t)((uintptr_t)&main_cdns);
    r_s0 = *(uint32_t *)(uintptr_t)((uintptr_t)&main_cdns_intp);
    goto L_6108c;
L_612d0:
    r_v0 = r_a1;
    r_v1 = 0;
    goto L_61278;
fn_exit:
    return (int32_t)r_v0;
}

static int32_t tisp_cdns_wdr_en_lit(uint32_t a0_in, uint32_t a1_in)
{
    uint32_t r_v0 = 0, r_v1 = 0;
    uint32_t r_t0 = 0, r_t1 = 0, r_t2 = 0, r_t3 = 0, r_t4 = 0, r_t5 = 0;
    uint32_t r_t6 = 0, r_t7 = 0, r_t8 = 0, r_t9 = 0;
    uint32_t r_s0 = 0, r_s1 = 0, r_s2 = 0, r_s3 = 0, r_s4 = 0, r_s5 = 0;
    uint32_t r_s6 = 0, r_s7 = 0, r_s8 = 0, r_ra = 0, r_at = 0;
    uint32_t mips_lo = 0, mips_hi = 0, mips_cond = 0;
    uint32_t r_a0 = a0_in;
    uint32_t r_a1 = a1_in;
    uint32_t r_a2 = 0;
    uint32_t r_a3 = 0;
    uint8_t mips_stack[448] __attribute__((aligned(8)));
    uint32_t r_sp = (uint32_t)(uintptr_t)(mips_stack + 384);

    (void)r_at; (void)r_ra; (void)r_t8; (void)r_t9; (void)mips_lo;
    (void)mips_hi; (void)r_sp; (void)mips_cond; (void)r_a2; (void)r_a3;
/* literal translation of tisp_cdns_wdr_en; warnings: 0 */
    mips_cond = (r_a0 == 0) ? 1U : 0U;
    r_v0 = 0; /* HI16 .bss anchor */
    if (mips_cond) goto L_61728;
    r_v0 = 0; /* HI16 .bss anchor */
    r_v1 = *(uint32_t *)(uintptr_t)((uintptr_t)&sec_cdns_comb);
    r_v0 = 0; /* HI16 .bss anchor */
    r_v0 = *(uint32_t *)(uintptr_t)((uintptr_t)&sec_cdns);
L_6161c:
    r_a2 = 0; /* HI16 .bss anchor */
    r_a0 = r_a0 << 2;
    r_a2 = (uint32_t)(uintptr_t)&regtrace_cdns_wdr_en_flags;
    r_a0 = r_a0 + r_a2;
    mips_cond = (r_a1 != 0) ? 1U : 0U;
    *(uint32_t *)(uintptr_t)(r_a0 + 0) = (uint32_t)r_a1;
    if (mips_cond) goto L_61738;
    r_a0 = r_v0 + 0x1U;
    *(uint32_t *)(uintptr_t)(r_v1 + 0) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0xcU;
    *(uint32_t *)(uintptr_t)(r_v1 + 4) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0x17U;
    *(uint32_t *)(uintptr_t)(r_v1 + 8) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0x22U;
    *(uint32_t *)(uintptr_t)(r_v1 + 12) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0x2dU;
    *(uint32_t *)(uintptr_t)(r_v1 + 16) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0x38U;
    *(uint32_t *)(uintptr_t)(r_v1 + 20) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0x43U;
    *(uint32_t *)(uintptr_t)(r_v1 + 24) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0x4eU;
    *(uint32_t *)(uintptr_t)(r_v1 + 28) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0x59U;
    *(uint32_t *)(uintptr_t)(r_v1 + 32) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0x64U;
    *(uint32_t *)(uintptr_t)(r_v1 + 36) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0x6fU;
    *(uint32_t *)(uintptr_t)(r_v1 + 40) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0x7aU;
    *(uint32_t *)(uintptr_t)(r_v1 + 44) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0x85U;
    *(uint32_t *)(uintptr_t)(r_v1 + 48) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0x90U;
    *(uint32_t *)(uintptr_t)(r_v1 + 52) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0x9bU;
    *(uint32_t *)(uintptr_t)(r_v1 + 56) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0xa6U;
    *(uint32_t *)(uintptr_t)(r_v1 + 60) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0xb1U;
    *(uint32_t *)(uintptr_t)(r_v1 + 64) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0xbcU;
    *(uint32_t *)(uintptr_t)(r_v1 + 68) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0xc7U;
    *(uint32_t *)(uintptr_t)(r_v1 + 72) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0xd2U;
    *(uint32_t *)(uintptr_t)(r_v1 + 76) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0xddU;
    *(uint32_t *)(uintptr_t)(r_v1 + 80) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0xe8U;
    *(uint32_t *)(uintptr_t)(r_v1 + 84) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0xf3U;
    *(uint32_t *)(uintptr_t)(r_v1 + 88) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0xfeU;
    *(uint32_t *)(uintptr_t)(r_v1 + 92) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0x109U;
    *(uint32_t *)(uintptr_t)(r_v1 + 96) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0x114U;
    *(uint32_t *)(uintptr_t)(r_v1 + 100) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0x135U;
    *(uint32_t *)(uintptr_t)(r_v1 + 104) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0x140U;
    *(uint32_t *)(uintptr_t)(r_v1 + 108) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0x14bU;
    *(uint32_t *)(uintptr_t)(r_v1 + 112) = (uint32_t)r_a0;
    r_v0 = r_v0 + 0x156U;
L_61720:
    *(uint32_t *)(uintptr_t)(r_v1 + 116) = (uint32_t)r_v0;
    goto fn_exit;
L_61728:
    r_v1 = *(uint32_t *)(uintptr_t)((uintptr_t)&main_cdns_comb);
    r_v0 = 0; /* HI16 .bss anchor */
    r_v0 = *(uint32_t *)(uintptr_t)((uintptr_t)&main_cdns);
    goto L_6161c;
L_61738:
    r_a0 = r_v0 + 0x161U;
    *(uint32_t *)(uintptr_t)(r_v1 + 0) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0x16cU;
    *(uint32_t *)(uintptr_t)(r_v1 + 4) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0x177U;
    *(uint32_t *)(uintptr_t)(r_v1 + 8) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0x182U;
    *(uint32_t *)(uintptr_t)(r_v1 + 12) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0x18dU;
    *(uint32_t *)(uintptr_t)(r_v1 + 16) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0x198U;
    *(uint32_t *)(uintptr_t)(r_v1 + 20) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0x1a3U;
    *(uint32_t *)(uintptr_t)(r_v1 + 24) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0x1aeU;
    *(uint32_t *)(uintptr_t)(r_v1 + 28) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0x1b9U;
    *(uint32_t *)(uintptr_t)(r_v1 + 32) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0x1c4U;
    *(uint32_t *)(uintptr_t)(r_v1 + 36) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0x1cfU;
    *(uint32_t *)(uintptr_t)(r_v1 + 40) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0x1daU;
    *(uint32_t *)(uintptr_t)(r_v1 + 44) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0x1e5U;
    *(uint32_t *)(uintptr_t)(r_v1 + 48) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0x1f0U;
    *(uint32_t *)(uintptr_t)(r_v1 + 52) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0x1fbU;
    *(uint32_t *)(uintptr_t)(r_v1 + 56) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0x206U;
    *(uint32_t *)(uintptr_t)(r_v1 + 60) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0x211U;
    *(uint32_t *)(uintptr_t)(r_v1 + 64) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0x21cU;
    *(uint32_t *)(uintptr_t)(r_v1 + 68) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0x227U;
    *(uint32_t *)(uintptr_t)(r_v1 + 72) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0x232U;
    *(uint32_t *)(uintptr_t)(r_v1 + 76) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0x23dU;
    *(uint32_t *)(uintptr_t)(r_v1 + 80) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0x248U;
    *(uint32_t *)(uintptr_t)(r_v1 + 84) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0x253U;
    *(uint32_t *)(uintptr_t)(r_v1 + 88) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0x25eU;
    *(uint32_t *)(uintptr_t)(r_v1 + 92) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0x269U;
    *(uint32_t *)(uintptr_t)(r_v1 + 96) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0x274U;
    *(uint32_t *)(uintptr_t)(r_v1 + 100) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0x27fU;
    *(uint32_t *)(uintptr_t)(r_v1 + 104) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0x28aU;
    *(uint32_t *)(uintptr_t)(r_v1 + 108) = (uint32_t)r_a0;
    r_a0 = r_v0 + 0x295U;
    *(uint32_t *)(uintptr_t)(r_v1 + 112) = (uint32_t)r_a0;
    r_v0 = r_v0 + 0x2a0U;
    goto L_61720;
fn_exit:
    return (int32_t)r_v0;
}

static int32_t tisp_cdns_all_reg_refresh_lit(uint32_t ch, uint32_t gain)
{
    /* OEM 0x61828: intp -> reg_cfg -> reg_trig (tail call) */
    (void)tisp_cdns_intp_lit(ch, gain);
    (void)tisp_cdns_reg_cfg_lit(ch);
    regtrace_cdns_refresh_count++;
    regtrace_cdns_gain_now = gain;
    return tisp_cdns_reg_trig_lit(ch);
}

static int32_t tisp_cdns_par_refresh_lit(uint32_t ch, uint32_t gain, uint32_t thresh)
{
    /* OEM 0x61880; unlike SDNS both paths go to all_reg_refresh. */
    uint32_t old = regtrace_cdns_gain_old[ch & 1U];
    uint32_t d;

    if (old != 0xffffffffU) {
        d = (gain < old) ? old - gain : gain - old;
        if (d < thresh) {
            regtrace_cdns_skip_count++;
            return 0;
        }
    }
    regtrace_cdns_gain_old[ch & 1U] = gain;
    return tisp_cdns_all_reg_refresh_lit(ch, gain);
}

/*
 * TOP 0x40 bit 19 = CDNS bypass.  Clear it only after a successful init
 * (and never against t40_force_bypass); set it again when init failed so
 * the block cannot run on stale parameters.  The callers commit
 * regtrace_t40_quality_top40_final to 0x40.
 */
static void regtrace_t40_cdns_top_bit(int init_ret)
{
    regtrace_cdns_dbg_init_ret = init_ret;
    if (!init_ret && !(regtrace_t40_force_bypass & BIT(19)))
        regtrace_t40_quality_top40_final &= ~BIT(19);
    else
        regtrace_t40_quality_top40_final |= BIT(19);
}

static int32_t tisp_cdns_params_refresh_lit(uint32_t ch)
{
    /* OEM 0x618d4: memcpy(ch ? sec_cdns : main_cdns, tparamsN[ch] + 0x11b8b, 683) */
    uintptr_t dst = ch ? sec_cdns : main_cdns;
    uintptr_t bank = tparamsN[ch];

    if (!dst || !bank)
        return -1;
    memcpy((void *)dst, (const void *)(bank + 0x11b8bU), 683);
    return 0;
}

static int32_t regtrace_cdns_main_init_lit(void)
{
    /* OEM tisp_cdns_main_init 0x61974 */
    void *par, *comb, *wrk;

    if (!main_cdns) {
        par = (void *)(uintptr_t)
            ((uintptr_t (*)(uintptr_t))(uintptr_t)private_vmalloc)(683);
        comb = (void *)(uintptr_t)
            ((uintptr_t (*)(uintptr_t))(uintptr_t)private_vmalloc)(120);
        wrk = (void *)(uintptr_t)
            ((uintptr_t (*)(uintptr_t))(uintptr_t)private_vmalloc)(32);
        if (!par || !comb || !wrk)
            return -ENOMEM;
        memset(par, 0, 683);
        memset(comb, 0, 120);
        memset(wrk, 0, 32);
        main_cdns = (uintptr_t)par;
        main_cdns_comb = (uintptr_t)comb;
        main_cdns_intp = (uintptr_t)wrk;
    }
    if (tisp_cdns_params_refresh_lit(0))
        return -ENOENT;
    (void)tisp_cdns_wdr_en_lit(0, regtrace_cdns_wdr_en_flags[0]);
    regtrace_cdns_gain_old[0] = 0xffffffffU;
    (void)tisp_cdns_par_refresh_lit(0, 0x10000U, 0x10000U);
    regtrace_cdns_init_count++;
    printk(KERN_INFO "tx_isp_t40_recovered: cdns-main-init-lit done par=%p comb[0]=%p comb[29]=%p\n",
           (void *)(uintptr_t)main_cdns,
           (void *)(uintptr_t)((uint32_t *)(uintptr_t)main_cdns_comb)[0],
           (void *)(uintptr_t)((uint32_t *)(uintptr_t)main_cdns_comb)[29]);
    return 0;
}
