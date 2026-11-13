/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000114_private_clk_put.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x114: private_clk_put; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
void private_clk_put(struct clk *clk);
void private_clk_put(struct clk *clk)
{
    clk_put(clk);
}
