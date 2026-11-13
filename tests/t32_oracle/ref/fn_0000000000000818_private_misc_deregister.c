/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000818_private_misc_deregister.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x818: private_misc_deregister; recovery status: diff_mismatch. */
#include "../ref_compat.h"
int private_misc_deregister(struct miscdevice *mdev);
int private_misc_deregister(struct miscdevice *mdev)
{
    misc_deregister(mdev);
    return 0;
}
