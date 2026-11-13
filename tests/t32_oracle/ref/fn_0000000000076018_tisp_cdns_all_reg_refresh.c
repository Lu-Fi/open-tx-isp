/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000076018_tisp_cdns_all_reg_refresh.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x76018: tisp_cdns_all_reg_refresh; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
extern int tisp_cdns_intp_reg_refresh();
int tisp_cdns_all_reg_refresh(int32_t arg1, int32_t arg2)
{
	return tisp_cdns_intp_reg_refresh(arg1, arg2);
}
