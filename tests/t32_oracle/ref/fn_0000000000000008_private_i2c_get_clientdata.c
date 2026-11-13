/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000008_private_i2c_get_clientdata.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x8: private_i2c_get_clientdata; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
void *private_i2c_get_clientdata(const struct i2c_client *dev);
void *private_i2c_get_clientdata(const struct i2c_client *dev)
{
    return i2c_get_clientdata(dev);
}
