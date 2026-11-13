/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000b28fc_tisp_g_face_ae_weiget.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0xb28fc: tisp_g_face_ae_weiget; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
extern int tisp_ae_api_get_face_ae_tune_params();
int32_t tisp_g_face_ae_weiget(char arg1, int32_t *arg2)
{
	return tisp_ae_api_get_face_ae_tune_params(arg1 & 0xff, arg2,
		&arg2[1], &arg2[2]);
}
