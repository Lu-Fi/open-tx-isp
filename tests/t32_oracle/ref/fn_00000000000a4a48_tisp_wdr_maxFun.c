/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000a4a48_tisp_wdr_maxFun.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0xa4a48: tisp_wdr_maxFun; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
int32_t tisp_wdr_maxFun(int32_t arg1, int32_t arg2)
{
	return (arg1 >= arg2) ? arg1 : arg2;
}
