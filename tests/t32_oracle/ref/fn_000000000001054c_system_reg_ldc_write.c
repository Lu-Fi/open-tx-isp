/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_000000000001054c_system_reg_ldc_write.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x1054c: system_reg_ldc_write; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
extern void *t32_data_d9944;

void *system_reg_ldc_write(int32_t arg1, int32_t arg2)
{
	void *result;

	result = *(volatile uint32_t *)((char *)t32_data_d9944 + 0xf4);
	*(volatile uint32_t *)((char *)result + arg1) = arg2;
	return result;
}
