/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000afb10_lib_tisp_remove.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0xafb10: lib_tisp_remove; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
extern int tisp_lce_remove();
int lib_tisp_remove(void)
{
	return tisp_lce_remove();
}
