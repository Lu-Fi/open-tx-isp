/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_000000000000d014_private_math_exp2.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0xd014: private_math_exp2; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
int32_t private_math_exp2(void)
{
    uint32_t ra = 0;
    uintptr_t v0 = 0;

    /* fragment 0: Epilogue */
    /* function epilogue: restore registers and return */

    /* fragment 1: Arithmetic */
    v0 = 0;

    return (int32_t)v0;
}
