/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T41 vendor AE (t41_vendor_ae=1): runtime for the mechanically translated
 * H20250310a AE chain in tx_isp_t41_vae_gen.inc.
 *
 * tools/t41_vae/gen.sh (asm2c.py) turns every OEM instruction of tisp_ae_calc_process
 * and its callees into one C statement on 32-bit register variables, so the
 * lifter defect classes (64-bit pairs, lhu/lw, pointer scaling, lost tail
 * calls, unbound args, wrong .bss/.rodata anchors) cannot occur: relocations
 * are resolved by symbol name against the driver's own objects.
 *
 * Every load/store/call goes through a guard: an address outside the kernel
 * map (NULL + offset, unrelocated table entries) is recorded as a fault and
 * skipped instead of oopsing.  Included at the end of tx_isp_t41_recovered.c.
 */
#ifndef TX_ISP_T41_VAE_H
#define TX_ISP_T41_VAE_H

typedef u64 (*vae_fn_t)(u32, u32, u32, u32, u32);

static u32 vae_faults;
static u32 vae_fault_addr, vae_fault_pc;
static u32 vae_calls;
static u32 vae_tisp_ae_hist[2104 / 4];
static u32 vae_ta_custom_en[2];
static u32 vae_ae_ceguang[320 / 4];
static u32 vae_slock[4];
static const char vae_str[] = "t41-vae";

static noinline void vae_fault_note(u32 addr, u32 pc)
{
	if (!vae_faults++) {
		vae_fault_addr = addr;
		vae_fault_pc = (u32)(uintptr_t)__builtin_return_address(0);
	}
	if (vae_faults <= 8)
		printk(KERN_WARNING "t41: vendor AE guard addr=%#x off=%#x from=%pS\n",
		       addr, pc, __builtin_return_address(0));
}

static inline bool vae_ok(u32 a, u32 align)
{
	return a >= 0x80000000U && a < 0xfffff000U && !(a & align);
}

#define VAE_LD(name, type, align) \
static noinline u32 vae_##name(u32 a, u32 pc) \
{ \
	if (unlikely(!vae_ok(a, align))) { vae_fault_note(a, pc); return 0; } \
	return (u32)*(volatile type *)(uintptr_t)a; \
}
VAE_LD(lw, u32, 3)
VAE_LD(lhu, u16, 1)
VAE_LD(lbu, u8, 0)
static inline u32 vae_lh(u32 a, u32 pc) { return (u32)(s32)(s16)vae_lhu(a, pc); }
static inline u32 vae_lb(u32 a, u32 pc) { return (u32)(s32)(signed char)vae_lbu(a, pc); }

#define VAE_ST(name, type, align) \
static noinline void vae_##name(u32 a, u32 v, u32 pc) \
{ \
	if (unlikely(!vae_ok(a, align))) { vae_fault_note(a, pc); return; } \
	*(volatile type *)(uintptr_t)a = (type)v; \
}
VAE_ST(sw, u32, 3)
VAE_ST(sh, u16, 1)
VAE_ST(sb, u8, 0)

/* Accesses to the function's own frame array vframe[] (generated "sp + N"):
 * always a valid, aligned kernel stack address, so no guard (same result as
 * the guarded access, which could never fault there). */
#define vf_lw(a)  (*(volatile u32 *)(uintptr_t)(a))
#define vf_lhu(a) ((u32)*(volatile u16 *)(uintptr_t)(a))
#define vf_lbu(a) ((u32)*(volatile u8 *)(uintptr_t)(a))
#define vf_lh(a)  ((u32)(s32)*(volatile s16 *)(uintptr_t)(a))
#define vf_lb(a)  ((u32)(s32)*(volatile s8 *)(uintptr_t)(a))
#define vf_sw(a, v) (*(volatile u32 *)(uintptr_t)(a) = (u32)(v))
#define vf_sh(a, v) (*(volatile u16 *)(uintptr_t)(a) = (u16)(v))
#define vf_sb(a, v) (*(volatile u8 *)(uintptr_t)(a) = (u8)(v))

/* hi:lo as one u64 (hi in the upper word); x/0 leaves 0 instead of trapping */
static inline u64 vae_divu(u32 x, u32 y, u32 pc)
{
	if (unlikely(!y)) { vae_fault_note(0, pc); return 0; }
	return ((u64)(x % y) << 32) | (x / y);
}
static inline u64 vae_divs(u32 x, u32 y, u32 pc)
{
	s32 a = (s32)x, b = (s32)y;
	if (unlikely(!b || (a == INT_MIN && b == -1))) { vae_fault_note(0, pc); return 0; }
	return ((u64)(u32)(a % b) << 32) | (u32)(a / b);
}

static inline void vae_fn_enter(void) { vae_calls++; }

static inline u64 vae_call(u32 tg, u32 a0, u32 a1, u32 a2, u32 a3, u32 sp, u32 pc)
{
	if (unlikely(tg < 0x80000000U || (tg & 3))) {
		vae_fault_note(tg, pc);
		return 0;
	}
	return ((vae_fn_t)(uintptr_t)tg)(a0, a1, a2, a3, sp);
}

/* ---- external leaves the OEM code calls (same ABI) ---- */
static u64 vae_x_memcpy(u32 a0, u32 a1, u32 a2, u32 a3, u32 csp)
{
	if (!vae_ok(a0, 0) || !vae_ok(a1, 0) || a2 > 0x10000) { vae_fault_note(a0, 1); return a0; }
	memcpy((void *)(uintptr_t)a0, (const void *)(uintptr_t)a1, a2);
	return a0;
}
static u64 vae_x_memset(u32 a0, u32 a1, u32 a2, u32 a3, u32 csp)
{
	if (!vae_ok(a0, 0) || a2 > 0x10000) { vae_fault_note(a0, 2); return a0; }
	memset((void *)(uintptr_t)a0, (int)a1, a2);
	return a0;
}
static u64 vae_x_private_complete(u32 a0, u32 a1, u32 a2, u32 a3, u32 csp)
{
	if (vae_ok(a0, 3))
		private_complete((struct completion *)(uintptr_t)a0);
	return 0;
}
/* T41 is single core: the OEM wrappers reduce to irq save/restore. */
static u64 vae_x___private_spin_lock_irqsave(u32 a0, u32 a1, u32 a2, u32 a3, u32 csp)
{
	unsigned long flags;

	local_irq_save(flags);
	vae_sw(a1, (u32)flags, 3);
	return 0;
}
static u64 vae_x_private_spin_unlock_irqrestore(u32 a0, u32 a1, u32 a2, u32 a3, u32 csp)
{
	local_irq_restore((unsigned long)a1);
	return 0;
}
static u64 vae_x_system_reg_write(u32 a0, u32 a1, u32 a2, u32 a3, u32 csp)
{
	return (u32)system_reg_write(a0, a1);
}
static u64 vae_x_isp_printf(u32 a0, u32 a1, u32 a2, u32 a3, u32 csp)
{
	return 0;
}
/* Events the OEM AE pushes (6: EV update for the gain-indexed modules).
 * Their lifted consumers are not trusted yet: dropped unless enabled in
 * t41_vae_events (bit n = event n); the driver's own gain fanout runs
 * instead (t41_vae_calc_process). */
static unsigned int t41_vae_events;
module_param(t41_vae_events, uint, 0644);
MODULE_PARM_DESC(t41_vae_events, "vendor AE: forward OEM AE event ids (bitmask, default 0)");
static u32 vae_events_seen, vae_events_dropped;
static bool t41_dfg_event(u32 ch, u32 id, u32 ev);
static u64 vae_x_tisp_event_push(u32 a0, u32 a1, u32 a2, u32 a3, u32 csp)
{
	u32 id = vae_lw(a1 + 8, 5);

	if (t41_dfg_event(a0, id, a1))
		return (u32)tisp_event_push(a0, a1);

	vae_events_seen |= id < 32 ? BIT(id) : BIT(31);
	if (id < 32 && (t41_vae_events & BIT(id)))
		return (u32)tisp_event_push(a0, a1);
	vae_events_dropped++;
	return 0;
}
static u64 vae_x___ashldi3(u32 a0, u32 a1, u32 a2, u32 a3, u32 csp)
{
	return (((u64)a1 << 32) | a0) << (a2 & 63);
}
static u64 vae_x___lshrdi3(u32 a0, u32 a1, u32 a2, u32 a3, u32 csp)
{
	return (((u64)a1 << 32) | a0) >> (a2 & 63);
}
static u64 vae_x___ashrdi3(u32 a0, u32 a1, u32 a2, u32 a3, u32 csp)
{
	return (u64)((s64)(((u64)a1 << 32) | a0) >> (a2 & 63));
}
static u64 vae_x___div64_32(u32 a0, u32 a1, u32 a2, u32 a3, u32 csp)
{
	u64 n;
	u32 rem;

	if (!vae_ok(a0, 3) || !a1) { vae_fault_note(a0, 3); return 0; }
	n = *(u64 *)(uintptr_t)a0;
	rem = do_div(n, a1);
	*(u64 *)(uintptr_t)a0 = n;
	return rem;
}
static u64 vae_x_div64_u64(u32 a0, u32 a1, u32 a2, u32 a3, u32 csp)
{
	u64 b = ((u64)a3 << 32) | a2;

	if (!b) { vae_fault_note(0, 4); return 0; }
	return div64_u64(((u64)a1 << 32) | a0, b);
}

#include "tx_isp_t41_vae_gen.inc"

/* Sensor control callbacks (sensor_ctrl_main+324..) the OEM AE reaches. */
static void t41_vae_install_sensor_ctrl(void)
{
	unsigned char *s = sensor_ctrl_main_storage;

	*(void **)(s + 332) = vae_sensor_alloc_analog_gain;
	*(void **)(s + 336) = vae_sensor_alloc_analog_gain_short;
	*(void **)(s + 340) = vae_sensor_alloc_digital_gain;
	*(void **)(s + 348) = vae_sensor_alloc_integration_time;
	*(void **)(s + 352) = vae_sensor_alloc_integration_time_short;
	*(void **)(s + 356) = vae_sensor_set_integration_time;
	*(void **)(s + 360) = vae_sensor_set_integration_time_short;
	*(void **)(s + 364) = vae_sensor_start_changes;
	*(void **)(s + 368) = vae_sensor_end_changes;
	*(void **)(s + 372) = vae_sensor_set_analog_gain;
	*(void **)(s + 376) = vae_sensor_set_analog_gain_short;
	*(void **)(s + 380) = vae_sensor_set_digital_gain;
}

static u32 vae_frames;

int32_t t41_vae_calc_process(uint32_t channel)
{
	u32 argarea[8] __aligned(8) = { 0 };
	u32 *info, faults0;
	unsigned char *cache;
	u64 ret;

	if (channel >= ARRAY_SIZE(ae_info))
		return -EINVAL;
	info = (u32 *)(uintptr_t)ae_info[channel];
	if (!t41_kernel_data_ptr(info) || !t41_kernel_data_ptr((void *)(uintptr_t)info[0]) ||
	    !t41_kernel_data_ptr((void *)(uintptr_t)info[1]))
		return -ENODEV;
	if (!*(u32 *)(void *)(ae_cache + channel * 4))
		*(u32 *)(void *)(ae_cache + channel * 4) =
			(u32)(uintptr_t)(ae_cache_init + channel * 0x688);
	cache = (unsigned char *)(uintptr_t)*(u32 *)(void *)(ae_cache + channel * 4);
	if (*(void **)(sensor_ctrl_main_storage + 356) != vae_sensor_set_integration_time)
		t41_vae_install_sensor_ctrl();
	if (channel == 0 && READ_ONCE(t41_ratio_pending))
		t41_module_ratio_apply(0, xchg(&t41_ratio_pending, 0));
	/* zone bank copy in process context (OEM: hist/static IRQ pair) */
	tisp_ae_interrupt_static(channel);
	{
		/* EV fixed-point precision of the calibration (AE params +0x6c0):
		 * the tone consumers need it to strip the shift again. */
		const unsigned char *ap = (const unsigned char *)(uintptr_t)info[0];
		unsigned int shift = ap[0x6c0] | (ap[0x6c1] << 8);

		if (shift && shift <= 16)
			WRITE_ONCE(t41_ae_ev_shift, shift);
	}
	faults0 = vae_faults;
	ret = vae_tisp_ae_calc_process(channel, 0, 0, 0, (u32)(uintptr_t)argarea);
	vae_frames++;
	{
		/* realized total gain (again * dgain, Q10 linear) -> log2 Q16,
		 * with the OEM's own tisp_log2_fixed_to_fixed */
		u32 it = *(u32 *)(cache + 576), ag = *(u32 *)(cache + 580);
		u32 dg = *(u32 *)(cache + 588), gain_q16;
		u64 total = (u64)ag * (dg ? dg : 1024) >> 10;

		if (total > 0x3ffffff)
			total = 0x3ffffff;
		gain_q16 = (u32)vae_tisp_log2_fixed_to_fixed((u32)total << 6, 16, 16, 0,
							(u32)(uintptr_t)argarea);
		/* proc/IMP getters report the safe AE state: mirror the
		 * realized vendor exposure there */
		t41_safe_ae[channel].integration = it;
		t41_safe_ae[channel].again = ag;
		t41_safe_ae[channel].frames++;
		t41_safe_ae[channel].last_mean_q8 = *(u32 *)(cache + 1520) << 8;
		t41_safe_ae_sensor[channel].log2_q16 = gain_q16;
		t41_safe_ae_integration = it;
		t41_safe_ae_gain_q16 = gain_q16;
		t41_tmo_exposure(it, gain_q16);
		if ((t41_safe_gain_fanout_mask & 0x3fU) &&
		    gain_q16 != t41_safe_gain_last_q16)
			t41_apply_safe_gain_fanout(channel, gain_q16,
						   t41_safe_gain_fanout_mask & 0x3fU);
	}
	if (vae_frames <= 30 || (t41_ae_diag == 1 && !(vae_frames % 25)) || t41_ae_diag >= 2 ||
	    (vae_faults != faults0 && vae_faults <= 8))
		printk(KERN_WARNING
		       "t41: vendor AE f=%u ret=%d it=%u ag=%u dg=%u ev=%u mean=%u g16=%u faults=%u ev-ids=%#x\n",
		       vae_frames, (int)ret, *(u32 *)(cache + 576), *(u32 *)(cache + 580),
		       *(u32 *)(cache + 588), *(u32 *)(cache + 1524),
		       *(u32 *)(cache + 1520), t41_safe_ae_gain_q16, vae_faults,
		       vae_events_seen);
	return (int32_t)ret;
}
#endif
