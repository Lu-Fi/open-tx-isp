/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000b38_private_simple_strtoull.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0xb38: private_simple_strtoull; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
unsigned long long private_simple_strtoull(const char *cp, char **endp, unsigned int base);
unsigned long long private_simple_strtoull(const char *cp, char **endp, unsigned int base)
{
    return simple_strtoull(cp, endp, base);
}
