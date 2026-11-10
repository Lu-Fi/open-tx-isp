/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000614_private_try_module_get.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x614: private_try_module_get; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
bool private_try_module_get(struct module *module);
bool private_try_module_get(struct module *module)
{
    return try_module_get(module);
}
