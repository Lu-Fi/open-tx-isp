/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000007d4_private_wake_up.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x7d4: private_wake_up; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
void private_wake_up(wait_queue_head_t *q);
void private_wake_up(wait_queue_head_t *q)
{
    wake_up(q);
}
