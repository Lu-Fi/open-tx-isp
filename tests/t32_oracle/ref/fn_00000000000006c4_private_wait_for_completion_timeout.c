/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000006c4_private_wait_for_completion_timeout.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x6c4: private_wait_for_completion_timeout; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
unsigned long private_wait_for_completion_timeout(struct completion *x, unsigned long timeout);
unsigned long private_wait_for_completion_timeout(struct completion *x, unsigned long timeout)
{
    return wait_for_completion_timeout(x, timeout);
}
