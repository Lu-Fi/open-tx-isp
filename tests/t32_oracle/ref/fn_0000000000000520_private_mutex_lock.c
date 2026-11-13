/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000520_private_mutex_lock.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x520: private_mutex_lock; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
void private_mutex_lock(struct mutex *lock);
void private_mutex_lock(struct mutex *lock)
{
    mutex_lock(lock);
}
