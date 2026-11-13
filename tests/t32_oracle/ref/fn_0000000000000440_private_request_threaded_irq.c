/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000440_private_request_threaded_irq.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x440: private_request_threaded_irq; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
int private_request_threaded_irq(unsigned int irq, irq_handler_t handler, irq_handler_t thread_fn, unsigned long flags, const char *name, void *dev);
int private_request_threaded_irq(unsigned int irq, irq_handler_t handler, irq_handler_t thread_fn, unsigned long flags, const char *name, void *dev)
{
    return request_threaded_irq(irq, handler, thread_fn, flags, name, dev);
}
