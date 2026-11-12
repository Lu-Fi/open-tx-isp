/* Host test: T30 g_ctrl jump table 1 (tx_isp_t30_gctrl.h, #379).  0x8000020 and 0x800002c are served (they
 * returned -1), the other table-1 entries keep their #375 result; the 0x8000020 block marks three tuning words. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../driver/t30/tx_isp_t30_gctrl.h"

int main(void)
{
	uint8_t t[0x1000], ref[0x1000];
	uint32_t i;
	int bad = 0;

	for (i = 0; i < 67; i++) {
		int want = (i == 0 || i == 3 || i == 0x21 || i == 0x40 || i == 0x41) ? T30_GCTRL_JT_ZERO :
			   i == 0x20 ? T30_GCTRL_JT_AE_ATTR : i == 0x2c ? T30_GCTRL_JT_STAB : T30_GCTRL_JT_ERR;
		bad += t30_g_ctrl_jt(1, i) != want;
	}
	bad += t30_g_ctrl_jt(3, 7) != T30_GCTRL_JT_TUNING_3776 || t30_g_ctrl_jt(3, 0) != T30_GCTRL_JT_ZERO;
	bad += t30_g_ctrl_jt(2, 0) != T30_GCTRL_JT_ERR;

	for (i = 0; i < sizeof(t); i++)
		t[i] = ref[i] = (uint8_t)(i * 7 + 1);
	t30_gctrl_ae_attr_mark(t);
	memset(ref + 0x828, 0xff, 4);		/* u16 +0x828, u16 +0x82a */
	memset(ref + 0x830, 0xff, 4);		/* u32 +0x830 */
	bad += memcmp(t, ref, sizeof(t)) != 0;
	bad += t[0x82c] != ref[0x82c] || T30_GCTRL_AE_ATTR_OFF + T30_GCTRL_AE_ATTR_LEN > sizeof(t) + 0x300;

	printf("t30_gctrl_host_test: %s\n", bad ? "FAIL" : "ok");
	return bad != 0;
}
