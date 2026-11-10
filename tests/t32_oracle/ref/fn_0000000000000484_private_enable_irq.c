/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000484_private_enable_irq.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x484: private_enable_irq; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
void private_enable_irq(unsigned int irq);
void private_enable_irq(unsigned int irq)
{
    enable_irq(irq);
}
