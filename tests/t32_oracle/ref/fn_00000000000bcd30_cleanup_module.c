/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000bcd30_cleanup_module.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0xbcd30: cleanup_module; recovery status: diff_mismatch. */
#include "../ref_compat.h"
extern int tx_isp_exit();
extern int tx_isp_sinfo_exit();
void __exit cleanup_module(void)
{
	tx_isp_sinfo_exit();
	tx_isp_exit();
}
