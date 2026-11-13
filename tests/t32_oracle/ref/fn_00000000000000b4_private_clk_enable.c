/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000000b4_private_clk_enable.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0xb4: private_clk_enable; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
int private_clk_enable(struct clk *clk);
int private_clk_enable(struct clk *clk)
{
    return clk_enable(clk);
}
