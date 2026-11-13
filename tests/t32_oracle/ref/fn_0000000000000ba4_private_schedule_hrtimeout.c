/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000ba4_private_schedule_hrtimeout.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0xba4: private_schedule_hrtimeout; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
int private_schedule_hrtimeout(ktime_t *expires, const enum hrtimer_mode mode);
int private_schedule_hrtimeout(ktime_t *expires, const enum hrtimer_mode mode)
{
    return schedule_hrtimeout(expires, mode);
}
