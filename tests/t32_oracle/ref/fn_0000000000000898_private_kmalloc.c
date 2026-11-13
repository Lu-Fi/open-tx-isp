/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000898_private_kmalloc.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x898: private_kmalloc; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
void *private_kmalloc(size_t size, gfp_t flags);
void *private_kmalloc(size_t size, gfp_t flags)
{
    return kmalloc(size, flags);
}
