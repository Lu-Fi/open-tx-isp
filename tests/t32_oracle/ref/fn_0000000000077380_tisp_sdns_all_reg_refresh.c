/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000077380_tisp_sdns_all_reg_refresh.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x77380: tisp_sdns_all_reg_refresh; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
extern int tisp_sdns_intp_reg_refresh();
int tisp_sdns_all_reg_refresh(int arg1, int arg2)
{
	return tisp_sdns_intp_reg_refresh(arg1, arg2);
}
