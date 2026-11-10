/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_000000000000016c_private_i2c_del_driver.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x16c: private_i2c_del_driver; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
void private_i2c_del_driver(struct i2c_driver *driver);
void private_i2c_del_driver(struct i2c_driver *driver)
{
    i2c_del_driver(driver);
}
