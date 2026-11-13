/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_000000000000019c_private_gpio_direction_output.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x19c: private_gpio_direction_output; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
int private_gpio_direction_output(unsigned int gpio, int value);
int private_gpio_direction_output(unsigned int gpio, int value)
{
    return gpio_direction_output(gpio, value);
}
