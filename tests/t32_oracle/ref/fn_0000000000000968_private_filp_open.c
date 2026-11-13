/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000968_private_filp_open.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x968: private_filp_open; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
struct file *private_filp_open(const char *filename, int flags, umode_t mode);
struct file *private_filp_open(const char *filename, int flags, umode_t mode)
{
    return filp_open(filename, flags, mode);
}
