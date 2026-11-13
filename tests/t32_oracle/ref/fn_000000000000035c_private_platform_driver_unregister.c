/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_000000000000035c_private_platform_driver_unregister.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x35c: private_platform_driver_unregister; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
void private_platform_driver_unregister(struct platform_driver *drv);
void private_platform_driver_unregister(struct platform_driver *drv)
{
    platform_driver_unregister(drv);
}
