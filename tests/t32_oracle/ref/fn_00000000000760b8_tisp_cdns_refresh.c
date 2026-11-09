/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000760b8_tisp_cdns_refresh.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x760b8: tisp_cdns_refresh; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
extern int tisp_cdns_par_refresh();
int tisp_cdns_refresh(int arg1, int arg2)
{
	return tisp_cdns_par_refresh(arg1, arg2, 256);
}
