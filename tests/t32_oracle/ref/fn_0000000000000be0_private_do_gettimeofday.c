/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000be0_private_do_gettimeofday.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0xbe0: private_do_gettimeofday; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
void private_do_gettimeofday(struct timeval *tv);
void private_do_gettimeofday(struct timeval *tv)
{
    do_gettimeofday(tv);
}
