/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000b3380_lib_tisp_api_get_csccr_output.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0xb3380: lib_tisp_api_get_csccr_output; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
extern int lib_tisp_csccr_dn_status();


int32_t lib_tisp_api_get_csccr_output(uint32_t a0)
{
    uint32_t t9 = 0;

    /* fragment 0: ConstantLoad */
    t9 = 0x0;

    /* fragment 1: IndirectTailCall */
    return lib_tisp_csccr_dn_status(a0 & 255);

    return 0;
}
