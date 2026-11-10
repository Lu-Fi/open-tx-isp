/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000003bc_private_platform_get_irq.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x3bc: private_platform_get_irq; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
int private_platform_get_irq(struct platform_device *pdev, unsigned int num);
int private_platform_get_irq(struct platform_device *pdev, unsigned int num)
{
    return platform_get_irq(pdev, num);
}
