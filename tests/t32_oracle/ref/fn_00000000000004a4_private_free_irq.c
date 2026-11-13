/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000004a4_private_free_irq.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x4a4: private_free_irq; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
void private_free_irq(unsigned int irq, void *dev_id);
void private_free_irq(unsigned int irq, void *dev_id)
{
    free_irq(irq, dev_id);
}
