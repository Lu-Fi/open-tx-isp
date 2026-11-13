/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000b28d4_tisp_g_coefft_wb.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0xb28d4: tisp_g_coefft_wb; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
extern int tisp_bcsh_api_get_offset_rgb();
int tisp_g_coefft_wb(int arg1, int arg2)
{
	return tisp_bcsh_api_get_offset_rgb(arg1, arg2);
}
