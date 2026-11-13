/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000a20_private_kthread_should_stop.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0xa20: private_kthread_should_stop; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
bool private_kthread_should_stop(void);
bool private_kthread_should_stop(void)
{
    return kthread_should_stop();
}
