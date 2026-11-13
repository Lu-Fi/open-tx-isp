/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000015434_sensor_start_changes.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x15434: sensor_start_changes; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
/*
 * sensor_start_changes - sensor start notification hook.
 *
 * This ISP function is an empty stub in the original binary: it contains
 * only a return instruction (jr ra / nop) and performs no work.
 */
void sensor_start_changes(void)
{
	/* no-op: nothing to do on sensor start change */
}
