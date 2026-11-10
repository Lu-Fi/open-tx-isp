/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_000000000000017c_private_gpio_request.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x17c: private_gpio_request; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
int private_gpio_request(unsigned int gpio, const char *label);
int private_gpio_request(unsigned int gpio, const char *label)
{
    return gpio_request(gpio, label);
}
