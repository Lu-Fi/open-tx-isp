/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_000000000000038c_private_platform_device_unregister.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x38c: private_platform_device_unregister; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
void private_platform_device_unregister(struct platform_device *pdev);
void private_platform_device_unregister(struct platform_device *pdev)
{
    platform_device_unregister(pdev);
}
