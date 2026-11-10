/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000680_private_init_completion.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x680: private_init_completion; recovery status: diff_mismatch. */
#include "../ref_compat.h"
void private_init_completion(struct completion *x);
void private_init_completion(struct completion *x)
{
    init_completion(x);
}
