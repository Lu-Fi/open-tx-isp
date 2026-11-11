/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_000000000008fcd0_tisp_rdma_fra_down_interrupt.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x8fcd0: tisp_rdma_fra_down_interrupt; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
int32_t tisp_rdma_fra_down_interrupt(void)
{
    uint32_t ra = 0;
    uintptr_t v0 = 0;

    /* fragment 0: Epilogue */
    /* function epilogue: restore registers and return */

    /* fragment 1: Arithmetic */
    v0 = 1;

    return (int32_t)v0;
}
