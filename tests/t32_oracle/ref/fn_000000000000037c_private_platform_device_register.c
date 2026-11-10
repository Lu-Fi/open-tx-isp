/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_000000000000037c_private_platform_device_register.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x37c: private_platform_device_register; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
int private_platform_device_register(struct platform_device *pdev);
int private_platform_device_register(struct platform_device *pdev)
{
    return platform_device_register(pdev);
}
