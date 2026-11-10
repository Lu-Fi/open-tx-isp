/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_000000000000036c_private_platform_set_drvdata.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x36c: private_platform_set_drvdata; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
void private_platform_set_drvdata(struct platform_device *pdev, void *data);
void private_platform_set_drvdata(struct platform_device *pdev, void *data)
{
    platform_set_drvdata(pdev, data);
}
