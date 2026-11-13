/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000b1994_tisp_api_ev_alloc_calc.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0xb1994: tisp_api_ev_alloc_calc; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
extern int tisp_ae_api_ev_alloc_calc();


int32_t tisp_api_ev_alloc_calc(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    uint32_t t9 = 0;

    /* fragment 0: ConstantLoad */
    t9 = 0x0;

    /* fragment 1: IndirectTailCall */
    return tisp_ae_api_ev_alloc_calc(a0 & 255);

    return 0;
}
