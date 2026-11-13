/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000b2928_tisp_g_ev_list.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0xb2928: tisp_g_ev_list; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
extern int tisp_ae_get_ev_list();


int32_t tisp_g_ev_list(uint32_t a0, uintptr_t a1)
{
    uint32_t t9 = 0;

    /* fragment 0: ConstantLoad */
    t9 = 0x0;

    /* fragment 1: IndirectTailCall */
    return tisp_ae_get_ev_list(a0 & 255);

    return 0;
}
