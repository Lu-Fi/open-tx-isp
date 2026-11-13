/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000828_private_proc_create_data.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x828: private_proc_create_data; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
struct proc_dir_entry *private_proc_create_data(const char *name, umode_t mode, struct proc_dir_entry *parent, const struct file_operations *proc_fops, void *data);
struct proc_dir_entry *private_proc_create_data(const char *name, umode_t mode, struct proc_dir_entry *parent, const struct file_operations *proc_fops, void *data)
{
    return proc_create_data(name, mode, parent, proc_fops, data);
}
