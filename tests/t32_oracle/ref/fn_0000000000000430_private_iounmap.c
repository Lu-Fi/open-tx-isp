/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000430_private_iounmap.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x430: private_iounmap; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
void private_iounmap(const volatile void __iomem *addr);
void private_iounmap(const volatile void __iomem *addr)
{
    iounmap((volatile void __iomem *)addr);
}
