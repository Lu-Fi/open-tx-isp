/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000bd0_private_flush_work.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0xbd0: private_flush_work; recovery status: diff_mismatch. */
#include "../ref_compat.h"
int private_flush_work(struct work_struct *work)
{
	return flush_work(work);
}
