/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000b25bc_tisp_g_csccr_mode.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0xb25bc: tisp_g_csccr_mode; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
extern int tisp_csccr_api_get();


int32_t tisp_g_csccr_mode(uint32_t a0, uint32_t a1)
{
    uint32_t t9 = 0;

    /* fragment 0: ConstantLoad */
    t9 = 0x0;

    /* fragment 1: IndirectTailCall */
    return tisp_csccr_api_get(a0 & 255);

    return 0;
}
