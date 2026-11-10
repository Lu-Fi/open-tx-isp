/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000ad0_private_jz_proc_mkdir.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0xad0: private_jz_proc_mkdir; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
struct proc_dir_entry *private_jz_proc_mkdir(char *s);
struct proc_dir_entry *private_jz_proc_mkdir(char *s)
{
    return jz_proc_mkdir(s);
}
