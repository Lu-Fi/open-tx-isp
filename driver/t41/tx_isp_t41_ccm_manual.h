/* SPDX-License-Identifier: MIT */
#ifndef TX_ISP_T41_CCM_MANUAL_H
#define TX_ISP_T41_CCM_MANUAL_H
#include "tx_isp_t41_ccm.h"
/*
 * IMP_ISP_Tuning_Set/GetCCMAttr (0x08000080).  Kernel wire (40 bytes, stock
 * tisp_ccm_api_set / tisp_bcsh_api_set_ccm): byte 0 = ManualEn, byte 1 =
 * SatEn, bytes 4..39 = nine signed Q16 words (65536 = 1.0).  Stock
 * tisp_ccm_interp_by_ct rounds each word to Q10 (round half up, shift 6)
 * and clips it to [-8192, 8191]; tisp_ccm_matrix_trans_by_sat copies the
 * matrix unchanged when ManualEn is set and SatEn is clear.
 */
#define T41_CCM_ATTR_BYTES 40U

struct t41_ccm_manual {
	unsigned int manual;	/* stock: byte0 == 1 */
	unsigned int sat;	/* byte1 */
	int word[9];		/* Q16 */
};

static inline int t41_ccm_manual_parse(const unsigned char *wire,
		struct t41_ccm_manual *out)
{
	unsigned int i;
	if (!wire || !out || wire[0] > 1 || wire[1] > 1)
		return -1;
	out->manual = wire[0];
	out->sat = wire[1];
	for (i = 0; i < 9; ++i)
		out->word[i] = (int)t41_tmo_le32(wire + 4 + i * 4);
	return 0;
}

static inline void t41_ccm_manual_matrix(const struct t41_ccm_manual *m,
		short *out)
{
	unsigned int i;
	for (i = 0; i < 9; ++i)
		out[i] = (short)t41_ccm_clip(t41_ccm_round(m->word[i], 6));
}
#endif
