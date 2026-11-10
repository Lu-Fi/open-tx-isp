/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_000000000009abb0_tisp_tmo_adr_params_refresh.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x9abb0: tisp_tmo_adr_params_refresh; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
extern int tisp_tmo_adr_gamma_refresh();
int tisp_tmo_adr_params_refresh(int32_t arg1)
{
	return tisp_tmo_adr_gamma_refresh(arg1);
}
