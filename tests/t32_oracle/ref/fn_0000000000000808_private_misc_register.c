/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000808_private_misc_register.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x808: private_misc_register; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
int private_misc_register(struct miscdevice *mdev);
int private_misc_register(struct miscdevice *mdev)
{
    return misc_register(mdev);
}
