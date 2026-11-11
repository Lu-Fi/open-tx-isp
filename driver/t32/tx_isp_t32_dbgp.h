/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32: the vendor's isp_printf (tx-isp-t32.o 0x10, Matt Davis' recovered C, PR #15):
 * below the module's print level nothing is printed, otherwise one printk("%pV")
 * and, from level 2 on, a stack dump.  The level compare is unsigned.
 *
 * Checked by tests/t32_oracle (t32_dbgp_oracle) against the recovered C: result,
 * the printed text and the stack dump on random levels, print levels and arguments.
 */
#ifndef TX_ISP_T32_DBGP_H
#define TX_ISP_T32_DBGP_H

#include "tx_isp_t32_glue.h"
#include <stdarg.h>

#ifdef __KERNEL__
#include <linux/kernel.h>
#define T32_DBG_PRINTK		printk
#define T32_DBG_DUMP_STACK	dump_stack
#endif
#ifndef __maybe_unused
#define __maybe_unused __attribute__((unused))
#endif
/* host oracle: defines struct va_format, T32_DBG_PRINTK and T32_DBG_DUMP_STACK before the include */

/* isp_printf: below the module's print level nothing, else printk("%pV"), and a stack dump from level 2 (0x10) */
static inline int t32_isp_vprintf(int print_level, int level, const char *fmt, va_list *args)
{
	struct va_format vaf;
	int ret;

	if ((unsigned int)level < (unsigned int)print_level)
		return 0;
	vaf.fmt = fmt;
	vaf.va = args;
	ret = T32_DBG_PRINTK("%pV", &vaf);
	if ((unsigned int)level >= 2)
		T32_DBG_DUMP_STACK();
	return ret;
}

static __maybe_unused int t32_isp_printf(int print_level, int level, const char *fmt, ...)
{
	va_list args;
	int ret;

	va_start(args, fmt);
	ret = t32_isp_vprintf(print_level, level, fmt, &args);
	va_end(args);
	return ret;
}

#endif
