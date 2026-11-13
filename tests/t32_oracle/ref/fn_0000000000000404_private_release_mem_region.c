/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000404_private_release_mem_region.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x404: private_release_mem_region; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
void private_release_mem_region(resource_size_t start, resource_size_t n);
void private_release_mem_region(resource_size_t start, resource_size_t n)
{
    release_mem_region(start, n);
}
