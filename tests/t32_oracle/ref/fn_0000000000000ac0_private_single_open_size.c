/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000ac0_private_single_open_size.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0xac0: private_single_open_size; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
int private_single_open_size(struct file *file, int (*show)(struct seq_file *, void *), void *data, size_t size);
int private_single_open_size(struct file *file, int (*show)(struct seq_file *, void *), void *data, size_t size)
{
    return single_open_size(file, show, data, size);
}
