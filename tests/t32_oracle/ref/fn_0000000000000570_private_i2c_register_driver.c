/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000570_private_i2c_register_driver.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x570: private_i2c_register_driver; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
int private_i2c_register_driver(struct module *owner, struct i2c_driver *driver);
int private_i2c_register_driver(struct module *owner, struct i2c_driver *driver)
{
    return i2c_register_driver(owner, driver);
}
