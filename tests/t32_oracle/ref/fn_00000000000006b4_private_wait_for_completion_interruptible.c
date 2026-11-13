/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000006b4_private_wait_for_completion_interruptible.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x6b4: private_wait_for_completion_interruptible; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
int private_wait_for_completion_interruptible(struct completion *x);
int private_wait_for_completion_interruptible(struct completion *x)
{
    return wait_for_completion_interruptible(x);
}
