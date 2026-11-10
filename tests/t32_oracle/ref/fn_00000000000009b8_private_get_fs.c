/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000009b8_private_get_fs.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x9b8: private_get_fs; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
mm_segment_t private_get_fs(void);
mm_segment_t private_get_fs(void)
{
    return get_fs();
}
