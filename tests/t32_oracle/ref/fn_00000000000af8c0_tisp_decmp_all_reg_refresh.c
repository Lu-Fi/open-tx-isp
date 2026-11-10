/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000af8c0_tisp_decmp_all_reg_refresh.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0xaf8c0: tisp_decmp_all_reg_refresh; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
extern int tisp_decmp_ref_reg_cfg();
int32_t tisp_decmp_all_reg_refresh(int32_t arg1)
{
	return tisp_decmp_ref_reg_cfg(arg1);
}
