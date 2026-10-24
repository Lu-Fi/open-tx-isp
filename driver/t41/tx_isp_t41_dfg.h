/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T41 defog (t41_defog=1): runtime for the mechanically translated H20250310a
 * defog chain in tx_isp_t41_dfg_gen.inc (tools/t41_vae/gen_defog.sh, same
 * asm2c.py and guard runtime as the vendor AE in tx_isp_t41_vae.h).
 *
 * Replaces the bypass-only tisp_defog_* of the lifted image: the OEM
 * tisp_defog_init allocates the statistics buffers, programs the block from
 * the tuning bin and hooks the statistics IRQ (slot 10) and the per-frame
 * event 3 (tisp_defog_process -> Tiziano_defog_soft -> write_reg_para3);
 * tisp_defog_ev_update follows the AE exposure (event 6) and
 * tisp_s_defog_ratio is the IMP strength path.  Included after tx_isp_t41_vae.h.
 */
#ifndef TX_ISP_T41_DFG_H
#define TX_ISP_T41_DFG_H

/* Default 0 since the 2026-10-24 .32 A/B: in the day bank (gc5603, 2880x1620,
 * 160x162 blocks) the translated chain leaves a bright, desaturated diamond
 * at every block corner (18 x 10 lattice, strength scales it; none with the
 * block bypassed, none in the vendor stack).  Registers programmed by init
 * are identical on host and device and the per-block LUT is smooth, so the
 * fault is not in the parameter loading; cause open.  1 = OEM defog chain
 * (translated), 0 = block kept in bypass (needs a boot-time param). */
static unsigned int t41_defog = 0;
module_param(t41_defog, uint, 0444);
MODULE_PARM_DESC(t41_defog, "defog: 1 = OEM defog chain (translated, leaves a diamond lattice in the gc5603 day picture), 0 = block kept in bypass (default)");

/* OEM statics: .bss+0x3ea0 (deghost_en_flag_tmp and the defog words behind it;
 * +0xf0 is the per-channel info pointer) and the PM callback table */
static u32 dfg_bss[64] __aligned(8);
static u32 dfg_tpm_cb[16] __aligned(8);
static u32 t41_dfg_live;
static u32 t41_dfg_irqs, t41_dfg_frames, t41_dfg_evs;
module_param_named(defog_irqs, t41_dfg_irqs, uint, 0444);
module_param_named(defog_frames, t41_dfg_frames, uint, 0444);
module_param_named(defog_evs, t41_dfg_evs, uint, 0444);
module_param_named(defog_live, t41_dfg_live, uint, 0444);

u64 vae_tisp_defog_process(u32 a0, u32 a1, u32 a2, u32 a3, u32 csp);
u64 vae_tisp_defog_interrupt_static(u32 a0, u32 a1, u32 a2, u32 a3, u32 csp);

static u64 vae_x_private_dma_cache_sync(u32 a0, u32 a1, u32 a2, u32 a3, u32 csp)
{
	if (!vae_ok(a1, 0) || a2 > 0x10000) { vae_fault_note(a1, 5); return 0; }
	return (u32)private_dma_cache_sync((struct device *)(uintptr_t)a0,
					   (void *)(uintptr_t)a1, a2, a3);
}
static u64 vae_x_private_kmalloc(u32 a0, u32 a1, u32 a2, u32 a3, u32 csp)
{
	return (u32)(uintptr_t)private_kmalloc(a0, GFP_KERNEL);
}
static u64 vae_x_private_vmalloc(u32 a0, u32 a1, u32 a2, u32 a3, u32 csp)
{
	return (u32)(uintptr_t)private_vmalloc(a0);
}
static u64 vae_x_private_kfree(u32 a0, u32 a1, u32 a2, u32 a3, u32 csp)
{
	if (vae_ok(a0, 3))
		private_kfree((void *)(uintptr_t)a0);
	return 0;
}
static u64 vae_x_private_vfree(u32 a0, u32 a1, u32 a2, u32 a3, u32 csp)
{
	if (a0 >= 0x80000000U && a0 < 0xfffff000U)
		private_vfree((void *)(uintptr_t)a0);
	return 0;
}
static u64 vae_x_system_irq_func_set(u32 a0, u32 a1, u32 a2, u32 a3, u32 csp)
{
	return (u32)system_irq_func_set(a0, a1, a2);
}
static u64 vae_x_system_reg_read(u32 a0, u32 a1, u32 a2, u32 a3, u32 csp)
{
	return (u32)system_reg_read(a0);
}
static u64 vae_x_tisp_get_tuning(u32 a0, u32 a1, u32 a2, u32 a3, u32 csp)
{
	return (u32)tisp_get_tuning();
}

/* event 3 callback: the event thread passes (channel, ...) */
static int t41_dfg_process_cb(uint32_t channel)
{
	if (!READ_ONCE(t41_dfg_live))
		return 0;
	t41_dfg_frames++;
	return (int)vae_tisp_defog_process(channel, 0, 0, 0, 0);
}
static u64 vae_x_tisp_event_set_cb(u32 a0, u32 a1, u32 a2, u32 a3, u32 csp)
{
	if (a1 == 3 && a2 == (u32)(uintptr_t)vae_tisp_defog_process)
		a2 = (u32)(uintptr_t)t41_dfg_process_cb;
	return (u32)tisp_event_set_cb(a0, a1, a2);
}

#include "tx_isp_t41_dfg_rodata.inc"
#include "tx_isp_t41_dfg_gen.inc"

/* Defog pushes event 3 itself (stats ready); AE event 6 carries the EV. */
static bool t41_dfg_event(u32 ch, u32 id, u32 ev)
{
	if (id == 3)
		return true;
	if (id == 6 && READ_ONCE(t41_dfg_live)) {
		u32 scratch[16] __aligned(8);

		t41_dfg_evs++;
		/* leaf frame set up late in the OEM function: its saves land in
		 * the "caller area" (csp) */
		vae_tisp_defog_ev_update(ch, 0, vae_lw(ev + 16, 6), vae_lw(ev + 20, 6),
					 (u32)(uintptr_t)scratch);
	}
	return false;
}

int64_t t41_dfg_init(uint32_t ch, uintptr_t par)
{
	u32 faults0 = vae_faults;
	u64 ret;

	if (!t41_defog)
		return tisp_defog_init(ch, par);
	if (ch != 0 || !par)
		return -EINVAL;
	if (t41_dfg_live)
		return -EBUSY;
	memset(dfg_bss, 0, sizeof(dfg_bss));
	ret = vae_tisp_defog_init(ch, (u32)par, 0, 0, 0);
	if ((s32)ret == 0)
		WRITE_ONCE(t41_dfg_live, 1);
	printk(KERN_WARNING "t41: defog init ret=%d info=%#x faults=%u\n",
	       (int)ret, dfg_bss[60], vae_faults - faults0);
	return (int)ret;
}

int32_t t41_dfg_deinit(uint32_t ch)
{
	if (!t41_defog)
		return tisp_defog_deinit(ch);
	if (!READ_ONCE(t41_dfg_live))
		return 0;
	WRITE_ONCE(t41_dfg_live, 0);
	return (int)vae_tisp_defog_deinit(ch, 0, 0, 0, 0);
}

int32_t t41_dfg_wdr_en(uint32_t ch, uint32_t en)
{
	if (!READ_ONCE(t41_dfg_live))
		return tisp_defog_wdr_en(ch, en);
	return (int)vae_tisp_defog_wdr_en(ch, en, 0, 0, 0);
}

int32_t t41_dfg_dn_params_refresh(uint32_t ch, uint32_t mode, uint32_t a2, uint32_t a3)
{
	if (!READ_ONCE(t41_dfg_live))
		return 0;
	{
		u32 scratch[64] __aligned(8);	/* caller area, see t41_dfg_event */

		return (int)vae_tisp_defog_dn_params_refresh(ch, mode, a2, a3,
							     (u32)(uintptr_t)scratch);
	}
}

/* IMP_ISP_Tuning_SetDefog_Strength: ratio/enable are read from the tuning
 * attributes (tuning + 184 + 8 * 4) by the OEM function. */
bool t41_dfg_ready(void) { return READ_ONCE(t41_dfg_live); }
int32_t t41_dfg_ratio_apply(void)
{
	if (!READ_ONCE(t41_dfg_live))
		return -EOPNOTSUPP;
	return (int)vae_tisp_s_defog_ratio(0, 0, 0, 0, 0);
}

void t41_dfg_irq(void)
{
	if (!READ_ONCE(t41_dfg_live))
		return;
	t41_dfg_irqs++;
	vae_tisp_defog_interrupt_static(0, 0, 0, 0, 0);
}
#endif
