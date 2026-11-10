/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000518_private_spin_lock_init.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x518: private_spin_lock_init; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
void private_spin_lock_init(spinlock_t *lock);
void private_spin_lock_init(spinlock_t *lock)
{
    spin_lock_init(lock);
}
