/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000282f4_lib_tisp_fw_process_empty.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x282f4: lib_tisp_fw_process_empty; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
extern int tisp_event_work_empty();


int32_t lib_tisp_fw_process_empty(uint32_t a0)
{
    uint32_t t9 = 0;

    /* fragment 0: ConstantLoad */
    t9 = 0x0;

    /* fragment 1: IndirectTailCall */
    return tisp_event_work_empty(a0 & 255);

    return 0;
}
