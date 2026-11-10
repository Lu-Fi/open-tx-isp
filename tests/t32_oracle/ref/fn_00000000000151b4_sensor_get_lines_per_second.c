/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000151b4_sensor_get_lines_per_second.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x151b4: sensor_get_lines_per_second; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
int32_t sensor_get_lines_per_second(void)
{
    uint32_t ra = 0;
    uintptr_t v0 = 0;

    /* fragment 0: Epilogue */
    /* function epilogue: restore registers and return */

    /* fragment 1: Arithmetic */
    v0 = 0;

    return (int32_t)v0;
}
