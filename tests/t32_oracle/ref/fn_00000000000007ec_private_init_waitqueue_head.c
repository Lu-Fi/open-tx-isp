/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000007ec_private_init_waitqueue_head.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x7ec: private_init_waitqueue_head; recovery status: diff_mismatch. */
#include "../ref_compat.h"
void private_init_waitqueue_head(wait_queue_head_t *q);
void private_init_waitqueue_head(wait_queue_head_t *q)
{
    init_waitqueue_head(q);
}
