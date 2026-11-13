/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000004d4_private_spin_unlock_irqrestore.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x4d4: private_spin_unlock_irqrestore; recovery status: diff_mismatch. */
#include "../ref_compat.h"
void private_spin_unlock_irqrestore(spinlock_t *lock, unsigned long flags);
void private_spin_unlock_irqrestore(spinlock_t *lock, unsigned long flags)
{
    spin_unlock_irqrestore(lock, flags);
}
