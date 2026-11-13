/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000009c8_private_set_fs.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x9c8: private_set_fs; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
void private_set_fs(mm_segment_t val);
void private_set_fs(mm_segment_t val)
{
    set_fs(val);
}
