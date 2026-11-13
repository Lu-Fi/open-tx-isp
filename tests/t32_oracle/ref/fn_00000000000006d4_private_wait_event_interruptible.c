/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000006d4_private_wait_event_interruptible.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x6d4: private_wait_event_interruptible; recovery status: diff_mismatch. */
#include "../ref_compat.h"
int private_wait_event_interruptible(wait_queue_head_t *q, int (*state)(void *), void *data);
int private_wait_event_interruptible(wait_queue_head_t *q, int (*state)(void *), void *data)
{
    return wait_event_interruptible(*q, state(data));
}
