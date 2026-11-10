/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000134_private_clk_set_rate.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x134: private_clk_set_rate; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
int private_clk_set_rate(struct clk *clk, unsigned long rate);
int private_clk_set_rate(struct clk *clk, unsigned long rate)
{
    return clk_set_rate(clk, rate);
}
