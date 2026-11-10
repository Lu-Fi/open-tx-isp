/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000005a0_private_gpio_direction_input.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x5a0: private_gpio_direction_input; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
int private_gpio_direction_input(unsigned int gpio);
int private_gpio_direction_input(unsigned int gpio)
{
    return gpio_direction_input(gpio);
}
