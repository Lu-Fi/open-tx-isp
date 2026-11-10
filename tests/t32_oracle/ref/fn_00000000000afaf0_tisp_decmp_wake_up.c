/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000afaf0_tisp_decmp_wake_up.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0xafaf0: tisp_decmp_wake_up; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
extern int tisp_decmp_ref_reg_cfg();
int tisp_decmp_wake_up(int arg1)
{
	return tisp_decmp_ref_reg_cfg(arg1);
}
