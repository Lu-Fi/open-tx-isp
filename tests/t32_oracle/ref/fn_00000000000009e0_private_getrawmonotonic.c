/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000009e0_private_getrawmonotonic.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x9e0: private_getrawmonotonic; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
void private_getrawmonotonic(struct timespec *ts);
void private_getrawmonotonic(struct timespec *ts)
{
    getrawmonotonic(ts);
}
