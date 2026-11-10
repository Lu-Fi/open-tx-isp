/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000104_private_clk_get_rate.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x104: private_clk_get_rate; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
unsigned long private_clk_get_rate(struct clk *clk);
unsigned long private_clk_get_rate(struct clk *clk)
{
    return clk_get_rate(clk);
}
