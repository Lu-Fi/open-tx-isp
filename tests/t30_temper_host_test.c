/* Host test: T30 Temper attribute system-table items (Lu-Fi/openimp#202).
 *
 * driver/t30/tx_isp_t30_temper.h against the SDK apical_isp_temper_dns_s_attr /
 * g_attr semantics (tx-isp-core-tuning.c; system_tab offsets 11/46/47/48 of apical.h,
 * checked by check_t30_temper.sh against the SDK header when T30_SDK_DIR is set):
 * MANUAL widens max/min to the strength and sets the target, AUTO restores the
 * calibration temper_max/min (u8 stores) and clears the manual flag, DISABLE keeps
 * the table; the getter reports DISABLE while the temper bypass bit (0x40 bit 10) is set. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef TEMPER_HDR
#define TEMPER_HDR "../driver/t30/tx_isp_t30_temper.h"
#endif
#include TEMPER_HDR

#define CK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); bad++; } } while (0)

int main(void)
{
	uint8_t st[60], ref[60];
	int bad = 0, mode, i;
	uint8_t s;

	/* AUTO: max/min from the calibration (u8), manual flag 0, rest untouched */
	memset(st, 0x5a, sizeof(st)); st[11] = 1;
	memcpy(ref, st, sizeof(st)); ref[11] = 0; ref[47] = 0x34; ref[48] = 0x12;
	t30_temper_s_attr_stab(st, T30_TEMPER_AUTO, 200, 0x1234, 0x5612);
	CK(memcmp(st, ref, sizeof(st)) == 0);
	/* MANUAL inside [min, max]: only flag + target */
	memset(st, 0, sizeof(st)); st[47] = 200; st[48] = 20;
	t30_temper_s_attr_stab(st, T30_TEMPER_MANUAL, 100, 1, 1);
	CK(st[11] == 1 && st[46] == 100 && st[47] == 200 && st[48] == 20);
	/* MANUAL above max / below min widens the range */
	t30_temper_s_attr_stab(st, T30_TEMPER_MANUAL, 250, 1, 1);
	CK(st[47] == 250 && st[48] == 20 && st[46] == 250);
	t30_temper_s_attr_stab(st, T30_TEMPER_MANUAL, 5, 1, 1);
	CK(st[47] == 250 && st[48] == 5 && st[46] == 5);
	/* DISABLE leaves the table */
	memcpy(ref, st, sizeof(st));
	t30_temper_s_attr_stab(st, T30_TEMPER_DISABLE, 77, 9, 9);
	CK(memcmp(st, ref, sizeof(st)) == 0);
	/* getter */
	t30_temper_g_attr(st, 0x33242193 | 0x400, &mode, &s);
	CK(mode == T30_TEMPER_DISABLE && s == 5);
	t30_temper_g_attr(st, 0x33242193 & ~0x400u, &mode, &s);
	CK(mode == T30_TEMPER_MANUAL && s == 5);
	st[11] = 0;
	t30_temper_g_attr(st, 0, &mode, &s);
	CK(mode == T30_TEMPER_AUTO);
	/* round trip: AUTO set then get (bypass cleared by the arm) */
	for (i = 0; i < 256; i += 51) {
		memset(st, 0, sizeof(st));
		t30_temper_s_attr_stab(st, T30_TEMPER_MANUAL, (uint8_t)i, 0, 0);
		t30_temper_g_attr(st, 0, &mode, &s);
		CK(mode == T30_TEMPER_MANUAL && s == i);
	}
	printf("t30_temper: s_attr/g_attr system-table items: %s\n", bad ? "FAIL" : "ok");
	return bad ? 1 : 0;
}
