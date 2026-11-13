/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000b94_private_set_current_state.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0xb94: private_set_current_state; recovery status: diff_mismatch. */
#include "../ref_compat.h"
void private_set_current_state(unsigned int state);
void private_set_current_state(unsigned int state)
{
    set_current_state(state);
}
