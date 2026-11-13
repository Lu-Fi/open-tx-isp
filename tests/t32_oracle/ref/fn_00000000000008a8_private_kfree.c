/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000008a8_private_kfree.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x8a8: private_kfree; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
void private_kfree(void *p);
void private_kfree(void *p)
{
    kfree(p);
}
