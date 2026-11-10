/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000580_private_i2c_new_device.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x580: private_i2c_new_device; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
struct i2c_client *private_i2c_new_device(struct i2c_adapter *adap, const struct i2c_board_info *info);
struct i2c_client *private_i2c_new_device(struct i2c_adapter *adap, const struct i2c_board_info *info)
{
    return i2c_new_device(adap, info);
}
