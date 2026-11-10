/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000053d44_lce_self_light_correct_global.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x53d44: lce_self_light_correct_global; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
extern int lce_self_light_correct_local();


int32_t lce_self_light_correct_global(uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t arg4)
{
    uint32_t t9 = 0;

    /* fragment 0: ConstantLoad */
    t9 = 0x0;

    /* fragment 1: IndirectTailCall */
    return lce_self_light_correct_local(a0, a1, a2, a3);

    return 0;
}
