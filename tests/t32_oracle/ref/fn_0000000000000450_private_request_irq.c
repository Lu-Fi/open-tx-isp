/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000450_private_request_irq.c@2e2e06bc, unchanged except the include line below. */
/* C wrapper checked against the T32 OEM calling convention. */
#include "../ref_compat.h"
int private_request_irq(unsigned int irq, irq_handler_t handler,
                        unsigned long flags, const char *name, void *dev)
{
    return request_threaded_irq(irq, handler, NULL, flags, name, dev);
}
