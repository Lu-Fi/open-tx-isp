/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000ab0_private_single_release.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0xab0: private_single_release; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
int private_single_release(struct inode *inode, struct file *file);
int private_single_release(struct inode *inode, struct file *file)
{
    return single_release(inode, file);
}
