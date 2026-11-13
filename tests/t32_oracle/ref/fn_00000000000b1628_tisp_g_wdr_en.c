/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000b1628_tisp_g_wdr_en.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0xb1628: tisp_g_wdr_en; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
extern uint32_t t32_data_dc284;

int32_t tisp_g_wdr_en(uintptr_t a0)
{
    uint32_t ra = 0;
    uintptr_t v0 = 0;

    /* fragment 0: Arithmetic */
    v0 = (uintptr_t)&t32_data_dc284;

    /* fragment 1: MemoryAccess */
    v0 = *(uint8_t *)((char *)&t32_data_dc284 + 36);
    *(uint32_t *)((char *)a0 + 0) = v0;

    /* fragment 2: Epilogue */
    /* function epilogue: restore registers and return */

    /* fragment 3: Arithmetic */
    v0 = 0;

    return (int32_t)v0;
}
