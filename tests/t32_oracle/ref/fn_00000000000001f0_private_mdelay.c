/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000001f0_private_mdelay.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x1f0: private_mdelay; recovery status: diff_mismatch. */
#include "../ref_compat.h"
void __udelay(unsigned long count);

void private_mdelay(int32_t arg1)
{
	void (*delay_p)(unsigned long) = __udelay;
	int32_t i = arg1;

	while (i != 0) {
		i -= 1;
		delay_p(1000);
	}
}
