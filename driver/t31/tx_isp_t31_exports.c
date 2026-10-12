/*
 * Export table of tx-isp-t31.ko: the whitelist in tx_isp_t31_exports.h.
 * Built with TX_ISP_T31_EXPORTS_C, so EXPORT_SYMBOL is the kernel's here.
 */
#include <linux/module.h>

#define TX_ISP_T31_EXPORT(sym) extern char sym[]; EXPORT_SYMBOL(sym);
#include "tx_isp_t31_exports.h"
