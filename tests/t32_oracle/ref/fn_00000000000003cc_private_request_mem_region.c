/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000003cc_private_request_mem_region.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x3cc: private_request_mem_region; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
struct resource *private_request_mem_region(resource_size_t start, resource_size_t n, const char *name);
struct resource *private_request_mem_region(resource_size_t start, resource_size_t n, const char *name)
{
    return request_mem_region(start, n, name);
}
