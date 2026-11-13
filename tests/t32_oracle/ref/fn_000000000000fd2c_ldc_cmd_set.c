/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_000000000000fd2c_ldc_cmd_set.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0xfd2c: ldc_cmd_set; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
int32_t ldc_cmd_set(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t ra = 0;
    uintptr_t v0 = 0;

    /* fragment 0: Epilogue */
    /* function epilogue: restore registers and return */

    /* fragment 1: Arithmetic */
    v0 = a2;

    return (int32_t)v0;
}
