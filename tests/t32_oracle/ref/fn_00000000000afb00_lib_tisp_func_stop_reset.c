/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000afb00_lib_tisp_func_stop_reset.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0xafb00: lib_tisp_func_stop_reset; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
extern int tisp_func_stop_reset();
int lib_tisp_func_stop_reset(void)
{
	return tisp_func_stop_reset();
}
