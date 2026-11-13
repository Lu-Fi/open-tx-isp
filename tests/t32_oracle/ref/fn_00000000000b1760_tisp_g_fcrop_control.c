/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000b1760_tisp_g_fcrop_control.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0xb1760: tisp_g_fcrop_control; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
extern int tisp_msca_api_get_fcrop();


int32_t tisp_g_fcrop_control(uint32_t a0, uintptr_t a1)
{
    uint32_t t9 = 0;

    /* fragment 0: ConstantLoad */
    t9 = 0x0;

    /* fragment 1: IndirectTailCall */
    return tisp_msca_api_get_fcrop(a0 & 255);

    return 0;
}
