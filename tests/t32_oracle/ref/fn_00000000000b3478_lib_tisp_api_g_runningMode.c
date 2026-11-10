/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000b3478_lib_tisp_api_g_runningMode.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0xb3478: lib_tisp_api_g_runningMode; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
extern uint32_t t32_data_dc284;

uint32_t lib_tisp_api_g_runningMode(void)
{
    uint32_t ra = 0;
    uintptr_t v0 = 0;

    /* fragment 0: Arithmetic */
    v0 = (uintptr_t)&t32_data_dc284;

    /* fragment 1: Epilogue */
    /* function epilogue: restore registers and return */

    /* fragment 2: MemoryAccess */
    v0 = *(uint8_t *)((char *)&t32_data_dc284 + 44);

    return (uint32_t)v0;
}
