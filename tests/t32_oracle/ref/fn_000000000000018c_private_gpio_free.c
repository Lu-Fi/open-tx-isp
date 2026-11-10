/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_000000000000018c_private_gpio_free.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x18c: private_gpio_free; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
void private_gpio_free(unsigned int gpio);
void private_gpio_free(unsigned int gpio)
{
    gpio_free(gpio);
}
