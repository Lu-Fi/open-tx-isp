/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000002d0_get_clkv_name.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x2d0: get_clkv_name; recovery status: diff_mismatch. */
#include "../ref_compat.h"
char *get_clkv_name(void)
{
    extern char *t32_data_ce250;
    return t32_data_ce250;
}
