/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000ae0_private_proc_remove.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0xae0: private_proc_remove; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
void private_proc_remove(struct proc_dir_entry *de);
void private_proc_remove(struct proc_dir_entry *de)
{
    proc_remove(de);
}
