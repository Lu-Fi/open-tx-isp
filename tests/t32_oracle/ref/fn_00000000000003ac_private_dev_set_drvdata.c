/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000003ac_private_dev_set_drvdata.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x3ac: private_dev_set_drvdata; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
void private_dev_set_drvdata(struct device *dev, void *data);
void private_dev_set_drvdata(struct device *dev, void *data)
{
    *(void **)((char *)dev + 0x4c) = data;
}
