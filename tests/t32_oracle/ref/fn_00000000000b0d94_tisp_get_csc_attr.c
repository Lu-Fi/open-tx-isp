/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000b0d94_tisp_get_csc_attr.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0xb0d94: tisp_get_csc_attr; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
extern int tisp_csc_api_get();


int32_t tisp_get_csc_attr(uint32_t a0, uintptr_t a1)
{
    uint32_t t9 = 0;

    /* fragment 0: ConstantLoad */
    t9 = 0x0;

    /* fragment 1: IndirectTailCall */
    return tisp_csc_api_get(a0 & 255);

    return 0;
}
