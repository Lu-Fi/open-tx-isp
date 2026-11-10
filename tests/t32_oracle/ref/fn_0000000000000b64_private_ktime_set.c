/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000b64_private_ktime_set.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0xb64: private_ktime_set; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
ktime_t private_ktime_set(const long secs, const unsigned long nsecs);
ktime_t private_ktime_set(const long secs, const unsigned long nsecs)
{
    return ktime_set(secs, nsecs);
}
