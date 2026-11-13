/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_000000000008d688_tisp_ccm_wake_up.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x8d688: tisp_ccm_wake_up; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
extern int tisp_ccm_write_reg();
int tisp_ccm_wake_up(int arg1)
{
	return tisp_ccm_write_reg(arg1, 0);
}
