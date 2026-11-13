/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000002f4_get_isp_clks.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x2f4: get_isp_clks; recovery status: diff_mismatch. */
#include "../ref_compat.h"
extern uint32_t t32_data_ce220;

uint32_t get_isp_clks(void)
{
    uint32_t ra = 0;
    uintptr_t v0 = 0;

    /* fragment 0: Arithmetic */
    v0 = (uintptr_t)&t32_data_ce220;

    /* fragment 1: Epilogue */
    /* function epilogue: restore registers and return */

    /* fragment 2: MemoryAccess */
    v0 = *(uint32_t *)((char *)&t32_data_ce220 + 36);

    return (uint32_t)v0;
}
