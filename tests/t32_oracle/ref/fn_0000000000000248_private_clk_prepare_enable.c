/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000248_private_clk_prepare_enable.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x248: private_clk_prepare_enable; recovery status: diff_mismatch. */
#include "../ref_compat.h"
int private_clk_prepare_enable(struct clk *clk)
{
    int ret;

    ret = clk_prepare(clk);
    if (ret)
        return ret;

    ret = clk_enable(clk);
    if (ret)
        clk_unprepare(clk);

    return ret;
}
