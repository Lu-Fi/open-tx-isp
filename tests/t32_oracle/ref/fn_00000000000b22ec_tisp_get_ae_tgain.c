/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000b22ec_tisp_get_ae_tgain.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0xb22ec: tisp_get_ae_tgain; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
extern int api_ae_get_tgain();
int tisp_get_ae_tgain(int arg1, int *arg2)
{
	return api_ae_get_tgain(arg1, arg2);
}
