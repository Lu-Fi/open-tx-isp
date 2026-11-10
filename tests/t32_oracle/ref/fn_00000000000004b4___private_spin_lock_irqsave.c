/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000004b4___private_spin_lock_irqsave.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x4b4: __private_spin_lock_irqsave; recovery status: diff_mismatch. */
#include "../ref_compat.h"
void __private_spin_lock_irqsave(spinlock_t *lock, unsigned long *flags);
void __private_spin_lock_irqsave(spinlock_t *lock, unsigned long *flags)
{
    unsigned long local_flags = 0;
    spin_lock_irqsave(lock, local_flags);
    if (flags) {
        *flags = local_flags;
    }
}
