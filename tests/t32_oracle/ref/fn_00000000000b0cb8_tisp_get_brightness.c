/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000b0cb8_tisp_get_brightness.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0xb0cb8: tisp_get_brightness; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
extern int tisp_bcsh_api_get_brightness();
int tisp_get_brightness(int arg1, char *arg2)
{
	return tisp_bcsh_api_get_brightness(arg1, arg2);
}
