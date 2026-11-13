/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000590_private_i2c_unregister_device.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x590: private_i2c_unregister_device; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
void private_i2c_unregister_device(struct i2c_client *client);
void private_i2c_unregister_device(struct i2c_client *client)
{
    i2c_unregister_device(client);
}
