/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000aa0_private_seq_lseek.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0xaa0: private_seq_lseek; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
loff_t private_seq_lseek(struct file *file, loff_t offset, int whence);
loff_t private_seq_lseek(struct file *file, loff_t offset, int whence)
{
    return seq_lseek(file, offset, whence);
}
