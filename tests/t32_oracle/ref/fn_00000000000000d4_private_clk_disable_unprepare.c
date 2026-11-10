/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000000d4_private_clk_disable_unprepare.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0xd4: private_clk_disable_unprepare; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
void private_clk_disable_unprepare(struct clk *clk)
{
    clk_disable(clk);
    clk_unprepare(clk);
}
