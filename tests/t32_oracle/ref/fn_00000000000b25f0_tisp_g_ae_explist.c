/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000b25f0_tisp_g_ae_explist.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0xb25f0: tisp_g_ae_explist; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
extern int tisp_ae_api_get_exp_list();
int32_t tisp_g_ae_explist(int32_t arg1, int32_t *arg2)
{
	return tisp_ae_api_get_exp_list(arg1, (int32_t)arg2, (int32_t)&arg2[1]);
}
