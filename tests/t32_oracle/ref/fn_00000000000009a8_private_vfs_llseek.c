/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000009a8_private_vfs_llseek.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x9a8: private_vfs_llseek; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
loff_t private_vfs_llseek(struct file *file, loff_t offset, int whence);
loff_t private_vfs_llseek(struct file *file, loff_t offset, int whence)
{
    return vfs_llseek(file, offset, whence);
}
