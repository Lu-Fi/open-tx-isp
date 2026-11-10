/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 system register access and the interrupt callback table of the vendor
 * module (Matt Davis' recovered C, PR #15): system_reg_write (0xb9744),
 * system_reg_read (0xb9760), system_reg_ldc_write (0x1054c),
 * system_reg_ldc_read (0x10568) and system_irq_func_set (0xac130).
 *
 * The ISP core registers are reached through the register base of the ISP
 * subdev (t32_data_f3310), the LDC registers through the base of the LDC
 * subdev (t32_data_d9944); both pointers are read at every call, as the vendor
 * does.  The vendor checks neither the pointer nor the offset.
 *
 * system_irq_func_set(unused, index, fn) stores fn in the 96 entry table
 * t32_data_e3b50; the index is not checked by the vendor (an index >= 96
 * writes past the table: vendor UB, kept, never done by the oracle).
 * Return values: system_reg_write returns 0; system_irq_func_set and
 * system_reg_ldc_write return leftover registers in the vendor code (the
 * table address / the register base) and are void here.
 *
 * Checked bit-exact against the vendor flow by tests/t32_oracle
 * (t32_sysreg_oracle).
 */
#ifndef TX_ISP_T32_SYSREG_H
#define TX_ISP_T32_SYSREG_H

#include "tx_isp_t32_glue.h"

#define T32_IRQ_FUNC_N	96

struct t32_sysreg {
	u8 *isp_sd;			/* t32_data_f3310: ISP subdev */
	u8 *ldc_sd;			/* t32_data_d9944: LDC subdev */
	u32 irq_func[T32_IRQ_FUNC_N];	/* t32_data_e3b50: interrupt callbacks */
};

static inline volatile u32 *t32_sysreg_addr(const u8 *sd, u32 reg)
{
	return (volatile u32 *)(T32_PTR(t32_g_rd32(sd, T32_G_SD_BASE)) + reg);
}

static inline int t32_system_reg_write(const struct t32_sysreg *s, u32 reg, u32 val)
{
	*t32_sysreg_addr(s->isp_sd, reg) = val;
	return 0;
}

static inline u32 t32_system_reg_read(const struct t32_sysreg *s, u32 reg)
{
	return *t32_sysreg_addr(s->isp_sd, reg);
}

static inline void t32_system_reg_ldc_write(const struct t32_sysreg *s, u32 reg, u32 val)
{
	*t32_sysreg_addr(s->ldc_sd, reg) = val;
}

static inline u32 t32_system_reg_ldc_read(const struct t32_sysreg *s, u32 reg)
{
	return *t32_sysreg_addr(s->ldc_sd, reg);
}

static inline void t32_system_irq_func_set(struct t32_sysreg *s, u32 index, u32 fn)
{
	s->irq_func[index] = fn;
}

#endif
