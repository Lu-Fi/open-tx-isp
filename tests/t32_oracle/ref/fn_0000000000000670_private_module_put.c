/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000670_private_module_put.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x670: private_module_put; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
void private_module_put(struct module *module);
void private_module_put(struct module *module)
{
    module_put(module);
}
