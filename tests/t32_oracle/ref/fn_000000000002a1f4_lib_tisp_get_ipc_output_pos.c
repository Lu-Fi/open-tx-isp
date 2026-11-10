/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_000000000002a1f4_lib_tisp_get_ipc_output_pos.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x2a1f4: lib_tisp_get_ipc_output_pos; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
extern int system_reg_read();


int32_t lib_tisp_get_ipc_output_pos(void)
{
    uint32_t a0 = 0;
    uint32_t t9 = 0;

    /* fragment 0: Arithmetic */
    a0 = 196608;
    t9 = (uintptr_t)&system_reg_read;
    t9 = t9;

    /* fragment 1: IndirectTailCall */
    return system_reg_read(a0 + 512);

    return 0;
}
