/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T40 vendor AWB (t40_vawb=1): runtime for the mechanically translated OEM AWB
 * chain in tx_isp_t40_vawb_gen.inc (tools/t40_vawb/gen.sh, same asm2c.py as the
 * T41 vendor AE).  Source module: Eufy T8416 OEM tx-isp-t40.ko (with symbols).
 *
 * Translated per frame: tisp_awb_get_statistics (IRQ), tisp_awb_main_process
 * (long_par_update, spec_calculate, long_alogrithm -> ct_detect, sat_weight,
 * set_gain), and for init / day-night / EV: params_transmit, params_refresh,
 * set_hardware_param, dn_params_refresh, main_ev_update, tgain_update.
 * Every OEM instruction is one C statement on 32-bit registers, relocations are
 * resolved by symbol name against the driver's own objects (pstMainAwbOri,
 * stMainAwbOuter/Inter, tparamsN, ...), so the lifter defect classes of the
 * fragment lift (lost args, wrong .bss anchors, 64-bit pairs) cannot occur.
 * Every load/store/call is guarded: a bad address is counted and skipped.
 * Included at the end of tx_isp_t40_recovered.c.
 */
#ifndef TX_ISP_T40_VAWB_H
#define TX_ISP_T40_VAWB_H

typedef u64 (*vawb_fn_t)(u32, u32, u32, u32, u32);

static u32 vawb_faults;
static u32 vawb_calls, vawb_frames, vawb_irqs;
static const char vawb_str[] = "t40-vawb";
module_param_named(t40_vawb_faults, vawb_faults, uint, 0444);
module_param_named(t40_vawb_frames, vawb_frames, uint, 0444);

/* OEM private state of the chain (not shared with other driver code) */
static u32 vawb_tawb_custom_en[2];
static u32 vawb_awb_num;
static u32 vawb_peter_test_count[2];
static u32 vawb_converge_tele_gain = 0x00010001U;              /* OEM .data 0x2bf14 */
static u32 vawb_wide_tele_awb_gain[3] = { 0x04000400U, 0x04000400U, 0x04000400U }; /* .data 0x2c0c8 */
/* awb_wide_tele_para: OEM main_init copies awb_wide_tele_para_t8416 (module
 * param a_awb_params default "T8416", .data 0x2c038).  [0] = enable, [1] = the
 * channel that gets the tele gain, [6] = 0 -> event 13 takes the CT of the
 * second (tele) sensor.  The open stack runs one sensor, so [0] is cleared in
 * t40_vawb_main_init: no tele gain, event 13 carries the main channel CT. */
static const u8 vawb_wide_tele_para_t8416[144] = {
	0x01, 0x01, 0x80, 0x00, 0x80, 0x00, 0x00, 0x04, 0x0a, 0x03, 0x09, 0x00, 0x0e, 0x00, 0x0e, 0x09,
	0x02, 0x02, 0x2c, 0x02, 0x4d, 0x02, 0x72, 0x02, 0xb9, 0x02, 0x87, 0x03, 0xef, 0x03, 0xc6, 0x04,
	0x0f, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0xe2, 0x02, 0xc1, 0x02, 0x8b, 0x02, 0x5b, 0x02, 0xfd, 0x01, 0x9c, 0x01, 0x78, 0x01, 0x4a, 0x01,
	0xa0, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0xf5, 0x01, 0x1f, 0x02, 0x3e, 0x02, 0x5f, 0x02, 0xa6, 0x02, 0x74, 0x03, 0xdf, 0x03, 0xb8, 0x04,
	0xdb, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x58, 0x03, 0x31, 0x03, 0xe8, 0x02, 0xa5, 0x02, 0x36, 0x02, 0xc2, 0x01, 0x8a, 0x01, 0x52, 0x01,
	0xad, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
static u8 vawb_wide_tele_para[144] __aligned(4);

static noinline void vawb_fault_note(u32 addr, u32 pc)
{
	if (vawb_faults++ < 8)
		printk(KERN_WARNING "t40: vendor AWB guard addr=%#x off=%u from=%pS\n",
		       addr, pc, __builtin_return_address(0));
}

static inline bool vawb_ok(u32 a, u32 align)
{
	return a >= 0x80000000U && a < 0xfffff000U && !(a & align);
}

#define VAWB_LD(name, type, align) \
static inline u32 vawb_##name(u32 a, u32 pc) \
{ \
	if (unlikely(!vawb_ok(a, align))) { vawb_fault_note(a, pc); return 0; } \
	return (u32)*(volatile type *)(uintptr_t)a; \
}
VAWB_LD(lw, u32, 3)
VAWB_LD(lhu, u16, 1)
VAWB_LD(lbu, u8, 0)
static inline u32 vawb_lh(u32 a, u32 pc) { return (u32)(s32)(s16)vawb_lhu(a, pc); }
static inline u32 vawb_lb(u32 a, u32 pc) { return (u32)(s32)(signed char)vawb_lbu(a, pc); }

#define VAWB_ST(name, type, align) \
static inline void vawb_##name(u32 a, u32 v, u32 pc) \
{ \
	if (unlikely(!vawb_ok(a, align))) { vawb_fault_note(a, pc); return; } \
	*(volatile type *)(uintptr_t)a = (type)v; \
}
VAWB_ST(sw, u32, 3)
VAWB_ST(sh, u16, 1)
VAWB_ST(sb, u8, 0)

/* accesses to the function's own frame array vframe[] (generated "sp + N"): always valid and aligned */
#define vf_lw(a)  (*(volatile u32 *)(uintptr_t)(a))
#define vf_lhu(a) ((u32)*(volatile u16 *)(uintptr_t)(a))
#define vf_lbu(a) ((u32)*(volatile u8 *)(uintptr_t)(a))
#define vf_lh(a)  ((u32)(s32)*(volatile s16 *)(uintptr_t)(a))
#define vf_lb(a)  ((u32)(s32)*(volatile s8 *)(uintptr_t)(a))
#define vf_sw(a, v) (*(volatile u32 *)(uintptr_t)(a) = (u32)(v))
#define vf_sh(a, v) (*(volatile u16 *)(uintptr_t)(a) = (u16)(v))
#define vf_sb(a, v) (*(volatile u8 *)(uintptr_t)(a) = (u8)(v))

/* little-endian unaligned word access (lwl/lwr, swl/swr pairs) */
static inline u32 vawb_lwl(u32 a, u32 rt, u32 pc)
{
	u32 k = a & 3, w = vawb_lw(a & ~3U, pc);
	return (rt & (0x00ffffffU >> (8 * k))) | (w << (8 * (3 - k)));
}
static inline u32 vawb_lwr(u32 a, u32 rt, u32 pc)
{
	u32 k = a & 3, w = vawb_lw(a & ~3U, pc);
	return k ? (rt & (0xffffffffU << (8 * (4 - k)))) | (w >> (8 * k)) : w;
}
static inline void vawb_swl(u32 a, u32 rt, u32 pc)
{
	u32 k = a & 3, b = a & ~3U, i;
	for (i = 0; i <= k; i++)
		vawb_sb(b + i, rt >> (8 * (3 - k + i)), pc);
}
static inline void vawb_swr(u32 a, u32 rt, u32 pc)
{
	u32 k = a & 3, i;
	for (i = 0; i < 4 - k; i++)
		vawb_sb(a + i, rt >> (8 * i), pc);
}

/* hi:lo as one u64 (hi in the upper word); x/0 leaves 0 instead of trapping */
static inline u64 vawb_divu(u32 x, u32 y, u32 pc)
{
	if (unlikely(!y)) { vawb_fault_note(0, pc); return 0; }
	return ((u64)(x % y) << 32) | (x / y);
}
static inline u64 vawb_divs(u32 x, u32 y, u32 pc)
{
	s32 a = (s32)x, b = (s32)y;
	if (unlikely(!b || (a == INT_MIN && b == -1))) { vawb_fault_note(0, pc); return 0; }
	return ((u64)(u32)(a % b) << 32) | (u32)(a / b);
}

static inline void vawb_fn_enter(void) { vawb_calls++; }

static inline u64 vawb_call(u32 tg, u32 a0, u32 a1, u32 a2, u32 a3, u32 sp, u32 pc)
{
	if (unlikely(tg < 0x80000000U || (tg & 3))) {
		vawb_fault_note(tg, pc);
		return 0;
	}
	return ((vawb_fn_t)(uintptr_t)tg)(a0, a1, a2, a3, sp);
}

/* ---- external leaves the OEM code calls (same ABI) ---- */
static u64 vawb_x_memcpy(u32 a0, u32 a1, u32 a2, u32 a3, u32 csp)
{
	if (!vawb_ok(a0, 0) || !vawb_ok(a1, 0) || a2 > 0x10000) { vawb_fault_note(a0, 1); return a0; }
	memcpy((void *)(uintptr_t)a0, (const void *)(uintptr_t)a1, a2);
	return a0;
}
static u64 vawb_x_memset(u32 a0, u32 a1, u32 a2, u32 a3, u32 csp)
{
	if (!vawb_ok(a0, 0) || a2 > 0x10000) { vawb_fault_note(a0, 2); return a0; }
	memset((void *)(uintptr_t)a0, (int)a1, a2);
	return a0;
}
static u64 vawb_x_private_complete(u32 a0, u32 a1, u32 a2, u32 a3, u32 csp)
{
	if (vawb_ok(a0, 3))
		private_complete(a0);
	return 0;
}
static u64 vawb_x_system_reg_write(u32 a0, u32 a1, u32 a2, u32 a3, u32 csp)
{
	return (u32)system_reg_write(a0, a1);
}
static u64 vawb_x_system_reg_read(u32 a0, u32 a1, u32 a2, u32 a3, u32 csp)
{
	return (u32)system_reg_read(a0);
}
static u64 vawb_x_isp_printf(u32 a0, u32 a1, u32 a2, u32 a3, u32 csp)
{
	return 0;
}
/* event 13 (CT -> CCM/LSC/BCSH/CLM) goes to the driver's dispatcher; the CT
 * consumers stay gated by t40_ct_update there */
static u32 vawb_events_seen;
static u64 vawb_x_tisp_event_push(u32 a0, u32 a1, u32 a2, u32 a3, u32 csp)
{
	u32 id = vawb_lw(a1 + 8, 5);

	vawb_events_seen |= id < 32 ? BIT(id) : BIT(31);
	return (u32)tisp_event_push(a0, a1);
}
static u64 vawb_x___ashldi3(u32 a0, u32 a1, u32 a2, u32 a3, u32 csp)
{
	return (((u64)a1 << 32) | a0) << (a2 & 63);
}
static u64 vawb_x___lshrdi3(u32 a0, u32 a1, u32 a2, u32 a3, u32 csp)
{
	return (((u64)a1 << 32) | a0) >> (a2 & 63);
}
static u64 vawb_x_div64_u64(u32 a0, u32 a1, u32 a2, u32 a3, u32 csp)
{
	u64 b = ((u64)a3 << 32) | a2;

	if (!b) { vawb_fault_note(0, 4); return 0; }
	return div64_u64(((u64)a1 << 32) | a0, b);
}

#include "tx_isp_t40_vawb_gen.inc"

static u32 vawb_argarea[16] __aligned(8);
#define VAWB_CSP ((u32)(uintptr_t)vawb_argarea)

/* OEM tisp_awb_main_init 0x31530 (frame of the open driver: inter seed =
 * stMainAwbInterOri, byte-identical with the Eufy OEM .data 0x1d36c). */
static int t40_vawb_main_init(uint32_t a0, uint32_t a1)
{
	uint32_t *ctx = pstMainAwbOri;
	void *outer;

	memcpy(vawb_wide_tele_para, vawb_wide_tele_para_t8416, sizeof(vawb_wide_tele_para));
	vawb_wide_tele_para[0] = 0;	/* single sensor: see vawb_wide_tele_para */
	if (!regtrace_awb_main_inter_alloc())
		return -ENOMEM;
	if (!stMainAwbOuter) {
		outer = (void *)(uintptr_t)((uintptr_t (*)(uintptr_t))(uintptr_t)private_vmalloc)(4752);
		if (!outer)
			return -ENOMEM;
		stMainAwbOuter = (uintptr_t)outer;
	}
	memset((void *)(uintptr_t)stMainAwbOuter, 0, 4752);
	vawb_tisp_awb_params_transmit((u32)(uintptr_t)ctx, 0, 0, 0, VAWB_CSP);
	if (!vawb_ok(ctx[49], 1) || !tparamsN[0])
		return -EFAULT;
	*(uint16_t *)(uintptr_t)ctx[49] = (uint16_t)a0;
	*(uint16_t *)((uintptr_t)ctx[49] + 2) = (uint16_t)a1;
	vawb_tisp_awb_params_refresh((u32)(uintptr_t)ctx, 0, (u32)tparamsN[0] + 4288, 0, VAWB_CSP);
	if (vawb_ok(ctx[119], 0) && !*(uint8_t *)(uintptr_t)ctx[119]) {
		vawb_tisp_awb_set_hardware_param((u32)(uintptr_t)ctx, 0, 0, 0, VAWB_CSP);
		vawb_tisp_awb_set_gain((u32)(uintptr_t)ctx, 0, 0, 0, VAWB_CSP);
	}
	(void)system_irq_func_set(3, (int32_t)(uintptr_t)&tisp_awb_main_interrupt_static);
	(void)tisp_event_set_cb(0, 14, (int32_t)(uintptr_t)&tisp_awb_main_process);
	printk(KERN_INFO "t40: vendor AWB init pi=%#x outer=%p inter=%p ctx7=%#x faults=%u\n",
	       a0, (void *)(uintptr_t)stMainAwbOuter, (void *)(uintptr_t)stMainAwbInter,
	       ctx[7], vawb_faults);
	return 0;
}

static void t40_vawb_get_statistics(uintptr_t addr)
{
	vawb_irqs++;
	vawb_tisp_awb_get_statistics((u32)addr, (u32)(uintptr_t)pstMainAwbOri, 0, 0, VAWB_CSP);
}

static int64_t t40_vawb_process(void)
{
	u32 argarea[16] __aligned(8) = { 0 };
	u32 f0 = vawb_faults;
	u64 ret = vawb_tisp_awb_main_process(0, 0, 0, 0, (u32)(uintptr_t)argarea);

	vawb_frames++;
	if (vawb_frames <= 4 || (vawb_faults != f0 && vawb_faults <= 8))
		printk(KERN_INFO "t40: vendor AWB f=%u ret=%d faults=%u ev=%#x\n",
		       vawb_frames, (int)ret, vawb_faults, vawb_events_seen);
	return (int64_t)ret;
}

/* CT consumers (OEM tisp_ct_update, event 13: ccm/lsc/bcsh main_ct_update) driven from the
 * frame-done work with the literal CCM/LSC/BCSH chains of this driver.  CT = *ctx[7]. */
static uint32_t t40_vawb_ct, t40_vawb_lsc_ct;
module_param_named(t40_vawb_ct, t40_vawb_ct, uint, 0444);
static uint t40_vawb_lsc_step = 200;
module_param(t40_vawb_lsc_step, uint, 0644);
MODULE_PARM_DESC(t40_vawb_lsc_step, "vendor AWB: LSC LUT re-interpolation CT step in K (default 200)");
static void t40_vawb_ct_follow(void)
{
	u32 ct = vawb_ok(pstMainAwbOri[7], 3) ? *(u32 *)(uintptr_t)pstMainAwbOri[7] : 0;
	u32 *b = pstMainBcshOri_lit;

	if (ct < 1500U || ct > 15000U || READ_ONCE(regtrace_running_mode))
		return;
	t40_vawb_ct = ct;
	if (regtrace_enable_ccm && regtrace_ccm_ct_track)
		regtrace_ccm_main_ct_update_lit(ct, false);	/* own threshold ctx[8] like OEM */
	if (regtrace_enable_lsc_lit && regtrace_lsc_lit_ct_track &&
	    (ct > t40_vawb_lsc_ct + t40_vawb_lsc_step || ct + t40_vawb_lsc_step < t40_vawb_lsc_ct)) {
		regtrace_lsc_lit_ct_update(0, ct);
		regtrace_lsc_lit_ct_now = ct;
		t40_vawb_lsc_ct = ct;
	}
	/* OEM tisp_bcsh_main_ct_update 0x6ab68: [41] cur, [42] last, [43] threshold, [6] dirty */
	if (stMainBcshOuter_lit && vawb_ok(b[41], 1) && vawb_ok(b[42], 1) && vawb_ok(b[43], 1) && b[6]) {
		u16 last = *(u16 *)(uintptr_t)b[42];

		*(u16 *)(uintptr_t)b[41] = (u16)ct;
		if (*(u8 *)(uintptr_t)b[6] == 1 ||
		    (last > ct ? last - ct : ct - last) > *(u16 *)(uintptr_t)b[43]) {
			(void)tisp_bcsh_ct_update_lit((u32)(uintptr_t)b, 0);
			*(u8 *)(uintptr_t)b[6] = 0;
			*(u16 *)(uintptr_t)b[42] = (u16)ct;
		}
	}
}

static uint32_t t40_vawb_dn_params_refresh(uint32_t a0)
{
	return (uint32_t)vawb_tisp_awb_dn_params_refresh(a0, 0, 0, 0, VAWB_CSP);
}
static int32_t t40_vawb_main_ev_update(uint32_t a0)
{
	return (int32_t)vawb_tisp_awb_main_ev_update(a0, 0, 0, 0, VAWB_CSP);
}
static int32_t t40_vawb_tgain_update(uint32_t a0, uint32_t a1, uint32_t a2)
{
	return (int32_t)vawb_tisp_awb_tgain_update(a0, a1, a2, 0, VAWB_CSP);
}
#endif
