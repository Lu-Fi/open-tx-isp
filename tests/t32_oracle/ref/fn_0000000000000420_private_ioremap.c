/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000420_private_ioremap.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x420: private_ioremap; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
void __iomem *private_ioremap(phys_addr_t offset, unsigned long size);
void __iomem *private_ioremap(phys_addr_t offset, unsigned long size)
{
    return ioremap(offset, size);
}
