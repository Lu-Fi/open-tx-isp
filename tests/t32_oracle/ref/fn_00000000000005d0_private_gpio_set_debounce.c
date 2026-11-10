/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000005d0_private_gpio_set_debounce.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x5d0: private_gpio_set_debounce; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
int private_gpio_set_debounce(unsigned int gpio, unsigned int debounce);
int private_gpio_set_debounce(unsigned int gpio, unsigned int debounce)
{
    return gpio_set_debounce(gpio, debounce);
}
