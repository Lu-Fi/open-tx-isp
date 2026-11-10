/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_000000000000039c_private_platform_get_resource.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x39c: private_platform_get_resource; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
struct resource *private_platform_get_resource(struct platform_device *pdev, unsigned int type, unsigned int num);
struct resource *private_platform_get_resource(struct platform_device *pdev, unsigned int type, unsigned int num)
{
    return platform_get_resource(pdev, type, num);
}
