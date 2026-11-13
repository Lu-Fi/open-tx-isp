/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000978_private_filp_close.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x978: private_filp_close; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
int private_filp_close(struct file *filp, fl_owner_t id);
int private_filp_close(struct file *filp, fl_owner_t id)
{
    return filp_close(filp, id);
}
