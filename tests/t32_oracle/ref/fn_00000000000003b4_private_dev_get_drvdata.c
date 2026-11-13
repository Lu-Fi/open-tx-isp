/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000003b4_private_dev_get_drvdata.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x3b4: private_dev_get_drvdata; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
void *private_dev_get_drvdata(const struct device *dev);
void *private_dev_get_drvdata(const struct device *dev)
{
    return dev_get_drvdata(dev);
}
