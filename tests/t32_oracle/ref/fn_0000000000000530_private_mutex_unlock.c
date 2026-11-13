/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000530_private_mutex_unlock.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x530: private_mutex_unlock; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
void private_mutex_unlock(struct mutex *lock);
void private_mutex_unlock(struct mutex *lock)
{
    mutex_unlock(lock);
}
