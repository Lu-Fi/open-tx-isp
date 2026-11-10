/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000bcce0_init_module.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0xbcce0: init_module; recovery status: diff_mismatch. */
#include "../ref_compat.h"
extern int tx_isp_init();
extern int tx_isp_sinfo_init();
int __init init_module(void)
{
	int ret;

	ret = tx_isp_init();
	if (ret == 0)
		tx_isp_sinfo_init();

	return ret;
}
