/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000238_private_capable.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x238: private_capable; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
bool private_capable(int cap);
bool private_capable(int cap)
{
    return capable(cap);
}
