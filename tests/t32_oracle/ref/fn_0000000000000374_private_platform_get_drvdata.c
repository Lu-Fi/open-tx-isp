/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000374_private_platform_get_drvdata.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x374: private_platform_get_drvdata; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
void *private_platform_get_drvdata(const struct platform_device *pdev);
void *private_platform_get_drvdata(const struct platform_device *pdev)
{
    return platform_get_drvdata(pdev);
}
