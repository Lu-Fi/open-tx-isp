/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000010_isp_printf.c@2e2e06bc, unchanged except the include line below. */
/* C wrapper checked against the T32 OEM calling convention. */
#include "../ref_compat.h"
extern int t32_data_ce260;
int isp_printf(int level, const char *fmt, ...)
{
    struct va_format vaf;
    va_list args;
    int ret;
    if ((unsigned int)level < (unsigned int)t32_data_ce260)
        return 0;
    va_start(args, fmt);
    vaf.fmt = fmt;
    vaf.va = &args;
    ret = printk("%pV", &vaf);
    va_end(args);
    if ((unsigned int)level >= 2)
        dump_stack();
    return ret;
}
