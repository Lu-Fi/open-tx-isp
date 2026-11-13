/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000b2a34_tisp_s_gib_blc.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0xb2a34: tisp_s_gib_blc; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
extern int tisp_gib_api_set_blc();
int tisp_s_gib_blc(int arg1, int arg2)
{
	return tisp_gib_api_set_blc(arg1, arg2);
}
