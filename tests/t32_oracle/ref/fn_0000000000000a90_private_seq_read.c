/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000a90_private_seq_read.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0xa90: private_seq_read; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
ssize_t private_seq_read(struct file *file, char __user *buf, size_t size, loff_t *ppos);
ssize_t private_seq_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
    return seq_read(file, buf, size, ppos);
}
