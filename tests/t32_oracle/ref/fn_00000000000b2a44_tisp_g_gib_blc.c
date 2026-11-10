/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000b2a44_tisp_g_gib_blc.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0xb2a44: tisp_g_gib_blc; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
extern int tisp_gib_api_get_blc();
int tisp_g_gib_blc(int arg1, int arg2)
{
	return tisp_gib_api_get_blc(arg1, arg2);
}
