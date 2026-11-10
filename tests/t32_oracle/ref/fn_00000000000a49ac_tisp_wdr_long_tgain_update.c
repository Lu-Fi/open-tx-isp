/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000a49ac_tisp_wdr_long_tgain_update.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0xa49ac: tisp_wdr_long_tgain_update; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
int32_t tisp_wdr_long_tgain_update(void)
{
    uint32_t ra = 0;
    uintptr_t v0 = 0;

    /* fragment 0: Epilogue */
    /* function epilogue: restore registers and return */

    /* fragment 1: Arithmetic */
    v0 = 0;

    return (int32_t)v0;
}
