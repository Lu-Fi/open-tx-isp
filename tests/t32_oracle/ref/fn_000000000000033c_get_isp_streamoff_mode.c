/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_000000000000033c_get_isp_streamoff_mode.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x33c: get_isp_streamoff_mode; recovery status: diff_mismatch. */
#include "../ref_compat.h"
extern uint32_t t32_data_d9000;

uint32_t get_isp_streamoff_mode(void)
{
    uint32_t ra = 0;
    uintptr_t v0 = 0;

    /* fragment 0: Arithmetic */
    v0 = (uintptr_t)&t32_data_d9000;

    /* fragment 1: Epilogue */
    /* function epilogue: restore registers and return */

    /* fragment 2: MemoryAccess */
    v0 = *(uint32_t *)((char *)&t32_data_d9000 + 24);

    return (uint32_t)v0;
}
