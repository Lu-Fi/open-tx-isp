/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000b28e4_tisp_s_face_ae_weiget.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0xb28e4: tisp_s_face_ae_weiget; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
extern int tisp_ae_api_set_face_ae_tune_params();
int32_t tisp_s_face_ae_weiget(char arg1, int32_t *arg2)
{
	return tisp_ae_api_set_face_ae_tune_params((unsigned char)arg1,
						   arg2,
						   &arg2[1],
						   &arg2[2]);
}
