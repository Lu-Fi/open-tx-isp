/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000000a4_private_devm_clk_get.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0xa4: private_devm_clk_get; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
struct clk *private_devm_clk_get(struct device *dev, const char *id);
struct clk *private_devm_clk_get(struct device *dev, const char *id)
{
    return devm_clk_get(dev, id);
}
