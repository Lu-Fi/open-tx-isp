/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_000000000001ecec_tisp_get_ae_info.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x1ecec: tisp_get_ae_info; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
int32_t tisp_get_ae_info(uintptr_t a0)
{
    uint32_t ra = 0;
    uintptr_t v0 = 0;

    /* fragment 0: Arithmetic */
    v0 = 164;

    /* fragment 1: MemoryAccess */
    *(uint32_t *)((char *)a0 + 4) = v0;

    /* fragment 2: Epilogue */
    /* function epilogue: restore registers and return */

    /* fragment 3: Arithmetic */
    v0 = 0;

    return (int32_t)v0;
}
