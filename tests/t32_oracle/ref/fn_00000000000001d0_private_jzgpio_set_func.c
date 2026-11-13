/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000001d0_private_jzgpio_set_func.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x1d0: private_jzgpio_set_func; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
extern int jzgpio_set_func(int, int, unsigned long);
int private_jzgpio_set_func(int port, int func, unsigned long pins)
{
    return jzgpio_set_func(port, func, pins);
}
