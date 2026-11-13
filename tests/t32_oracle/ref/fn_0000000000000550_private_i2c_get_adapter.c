/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000550_private_i2c_get_adapter.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x550: private_i2c_get_adapter; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
struct i2c_adapter *private_i2c_get_adapter(int nr);
struct i2c_adapter *private_i2c_get_adapter(int nr)
{
    return i2c_get_adapter(nr);
}
