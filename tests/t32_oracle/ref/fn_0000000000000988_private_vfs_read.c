/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000988_private_vfs_read.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x988: private_vfs_read; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
ssize_t private_vfs_read(struct file *file, char __user *buf, size_t count, loff_t *pos);
ssize_t private_vfs_read(struct file *file, char __user *buf, size_t count, loff_t *pos)
{
    return vfs_read(file, buf, count, pos);
}
