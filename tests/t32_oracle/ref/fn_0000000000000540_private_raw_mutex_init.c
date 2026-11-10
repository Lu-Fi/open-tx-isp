/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000540_private_raw_mutex_init.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x540: private_raw_mutex_init; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
void private_raw_mutex_init(struct mutex *lock, const char *name, struct lock_class_key *key);
void private_raw_mutex_init(struct mutex *lock, const char *name, struct lock_class_key *key)
{
    __mutex_init(lock, name, key);
}
