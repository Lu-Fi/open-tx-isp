/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000560_private_i2c_put_adapter.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x560: private_i2c_put_adapter; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
void private_i2c_put_adapter(struct i2c_adapter *adap);
void private_i2c_put_adapter(struct i2c_adapter *adap)
{
    i2c_put_adapter(adap);
}
