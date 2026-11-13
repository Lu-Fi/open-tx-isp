/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000604_private_sched_clock.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x604: private_sched_clock; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
unsigned long long private_sched_clock(void);
unsigned long long private_sched_clock(void)
{
    return sched_clock();
}
