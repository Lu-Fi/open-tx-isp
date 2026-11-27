/*
 * t32_sdns_ratio_host: SDK 2.1.0 tisp_sdns_intp ends with tisp_ratio(ctx+16, p[20], 16) and
 * tisp_ratio(-ctx+16, p[31], 108) (PRJ007 2.1.0 drv .dis tisp_sdns_intp 0x75314..0x75370; 2.0.0 had none)
 * (openimp #928).  Ratio 128 (tisp_sdns_init value) must leave the 2.0 result unchanged.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_sdns.h"

int isp_printf(unsigned int level, const char *fmt, ...) { (void)level; (void)fmt; return 0; }

static unsigned int bad;
#define CK(c, m) do { if (!(c)) { printf("FAIL: %s\n", m); bad++; } } while (0)

int main(void)
{
	static u8 sec[T32_SDNS_SEC_BYTES], base[55], p[55];
	struct t32_sdns_ctx c = { .sec = sec, .param = p, .gain = (u32)-1, .ratio = 128 };
	unsigned int k, t;
	u32 gain, r;

	for (t = 0; t < 200; t++) {
		for (k = 0; k < sizeof(sec); k++)
			sec[k] = (u8)(rand() >> 5);
		gain = ((rand() % 6) << 16) | (rand() & 0xffff);
		for (k = 0; k < 55; k++)
			base[t32_sdns_map[k][1]] = t32_tp_intp_u8(gain >> 16, gain & 0xffff, sec + t32_sdns_map[k][0]);
		r = t % 4 == 0 ? 128 : (u32)(rand() & 0xff);
		c.ratio = r;
		t32_sdns_intp(&c, 0, gain);
		for (k = 0; k < 55; k++) {
			if (k == 20)
				CK(p[k] == (u8)t32_dmsc_ratio(r, base[k], 16), "byte 20");
			else if (k == 31)
				CK(p[k] == (u8)t32_dmsc_ratio(-r, base[k], 108), "byte 31");
			else
				CK(p[k] == base[k], "other byte changed");
		}
		if (r == 128)
			CK(p[20] == base[20] && p[31] == base[31], "ratio 128 is not neutral");
	}
	/* fixed vendor-formula points: r=64 -> 64*v>>7 = v/2; -64 & 0xff = 192 -> (64 * (108 - v) >> 7) + v */
	CK(t32_dmsc_ratio(64, 10, 16) == 5, "ratio 64");
	CK(t32_dmsc_ratio(-64, 40, 108) == 40 + ((64 * 68) >> 7), "ratio 192");
	if (bad)
		return 1;
	printf("t32_sdns_ratio_host: OK\n");
	return 0;
}
