/* Oracle replacement for the vendor tisp_reg_write (fn_1c790), NOT driver code.
 * Vendor: mode d03a0 != 1 is a tail call of system_reg_write(a1, a2); mode 1
 * stores a2 at image[a1 * 4] of channel a0 (the register shadow).  Here mode 1
 * is logged as reg = 0x80000000 | (a0 & 7) << 24 | a1 (value a2), in order, so an
 * oracle can prove that the d03a0 = 1 run differs from the direct one only in
 * the address (t32o_reg_eq in t32_oracle_env.h). */
#include "../ref_compat.h"
extern unsigned char ref_t32_data_d03a0[] __asm__("t32_data_d03a0");
extern struct { unsigned int n, printfs; unsigned int irq; struct { unsigned int reg, val; } w[4096]; } t32o_log;
u64 system_reg_write(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_reg_write(u32 a0, u32 a1, u32 a2, u32 a3)
{
	if (ref_t32_data_d03a0[0] != 1)
		return system_reg_write(a1, a2, 0, 0);
	if (t32o_log.n < 4096) {
		t32o_log.w[t32o_log.n].reg = 0x80000000u | (a0 & 7) << 24 | (a1 & 0xffffff);
		t32o_log.w[t32o_log.n].val = a2;
	}
	t32o_log.n++;
	(void)a3;
	return 0;
}
