/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The T33 LCE / LSF per-frame path (driver/t32/tx_isp_t33_lcepf.h) built for mipsel as a relocatable object,
 * for tools/ko_emu/diff_t33_lcepf.py: Unicorn runs these entry points and the vendor tisp_lce_* functions of
 * tx-isp-t33.ko on the same state (lceInfo block, section, buffers, lists) and compares all of it.
 * system_reg_read, tisp_rcfg_module_cfg_trig, tisp_event_push, memcpy and memset stay undefined (the script
 * hooks them; the vendor's own system_reg_read / cfg_trig / event_push entries are hooked the same way).
 * Build: tests/t33_lcepf/build_emu.sh <cc> <out.o> [header-dir]
 */
#include <stdint.h>
#include <string.h>

extern uint32_t system_reg_read(uint32_t reg);
extern uint32_t tisp_rcfg_module_cfg_trig(uint32_t vin, uint32_t id);
extern int tisp_event_push(uint32_t vin, void *ev);

#include "tx_isp_t33_lcepf.h"

/* the vendor's event record: type at +8 */
struct emu_ev { u32 r[2]; u32 type; u32 more[9]; };

/* tisp_lce_init's lceInfo part; s = the section (sized here like the vendor's tisp_lce_size_para_calc) */
void emu_init(struct t33_lce_pf *c, u8 *s, u32 w, u32 h)
{
	t33_lce_size(s, h, w);
	t33_lcepf_init(c, s, w, h);
}

/* tisp_lce_interrupt_static */
u32 emu_irq8(struct t33_lce_pf *c, const u8 *s, const u8 *stat, u32 *bufs, u32 *l16, u32 *l37, u32 nsens)
{
	u32 idx = system_reg_read(T33_LCE_IDX_REG);

	/* bufs: curve va, stat pa, hist pa, curve pa */
	t33_lcepf_irq8(c, s, idx, stat, bufs[1], bufs[2], (u8 *)bufs[0], bufs[3], l16, l37);
	if (nsens == 1)
		tisp_rcfg_module_cfg_trig(0, T33_MOD_LCE);
	t33_lcepf_irq8_lsf(l37);
	if (!system_reg_read(T33_LSF_BUSY_REG))
		tisp_rcfg_module_cfg_trig(0, T33_MOD_LSF);
	return 1;
}

/* tisp_lsf_interrupt_static */
u32 emu_irq30(struct t33_lce_pf *c, const u8 *s, const u8 *hist)
{
	struct emu_ev ev;

	u32 d = system_reg_read(T33_LSF_DATAOUT_REG);

	memset(&ev, 0, sizeof(ev));
	t33_lcepf_irq30(c, s, hist, d, system_reg_read(T33_LSF_DATAOUT_REG));
	ev.type = T33_LCEPF_EV_LCE;
	tisp_event_push(0, &ev);
	return 1;
}

/* tisp_lce_soft_process */
u32 emu_process(struct t33_lce_pf *c, u8 *s, u32 *half2)
{
	t33_lcepf_process(c, s, system_reg_read(T33_LSF_DATAOUT_REG), half2);
	return 0;
}

/* leaves */
void emu_hist_method(u16 *h, const u16 *clip, const u8 *par, u32 str, u32 clip2)
{
	t33_lcepf_hist_method(h, h + 32, clip, par, str, clip2 == 1);
}

void emu_thres(const u16 *h, const u16 *thr, u8 *out)
{
	t32_lce_thres_search(h, thr, out);
}

void emu_adjust(u8 *a, const u32 *up, const u32 *dn)
{
	t33_lcepf_hist_adjust(a, up, dn);
}

void emu_converge(u32 g, u32 lo, u32 hi, const u8 *tgt, u8 *cur)
{
	t33_lcepf_converge(g, lo, hi, tgt, cur);
}

void emu_lsf_curve(u32 *buf)
{
	t33_lsf_write_curve(buf);
}
