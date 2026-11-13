/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000bb4_private_schedule_work.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0xbb4: private_schedule_work; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
bool private_schedule_work(struct work_struct *work);
bool private_schedule_work(struct work_struct *work)
{
    return schedule_work(work);
}
