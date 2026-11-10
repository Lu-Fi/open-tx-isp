/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_000000000008fba0_tisp_csccr_wake_up.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x8fba0: tisp_csccr_wake_up; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
extern int tisp_csccr_write_reg();


int32_t tisp_csccr_wake_up(uint32_t a0)
{
    uint32_t t9 = 0;

    /* fragment 0: ConstantLoad */
    t9 = 0x0;

    /* fragment 1: IndirectTailCall */
    return tisp_csccr_write_reg(a0 & 255);

    return 0;
}
