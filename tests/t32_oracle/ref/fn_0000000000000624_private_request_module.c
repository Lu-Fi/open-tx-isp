/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000624_private_request_module.c@2e2e06bc, unchanged except the include line below. */
/* C wrapper checked against the T32 OEM calling convention. */
#include "../ref_compat.h"
int private_request_module(bool wait, const char *fmt, ...)
{
    struct va_format vaf;
    va_list args;
    int ret;
    /* The OEM always requests synchronously, regardless of the first argument. */
    (void)wait;
    va_start(args, fmt);
    vaf.fmt = fmt;
    vaf.va = &args;
    ret = __request_module(true, "%pV", &vaf);
    va_end(args);
    return ret;
}
