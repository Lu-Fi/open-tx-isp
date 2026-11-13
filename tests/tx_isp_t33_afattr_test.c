/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Host test of the T33 AF attribute path (tisp_af_api_set_attr / get_attr, driver/t32/tx_isp_t33_af.h):
 * with T33_IQ_BIN (a cv2005 PRJ008 bin) the section 24 is patched by an attribute and the list 31 rebuilt
 * (tisp_af_par_init + tisp_af_t33_all_reg_write on the patched section): the window words follow the
 * attribute, everything not touched by the attribute stays, the get answers what was set, and the
 * same attribute twice gives the same list.  Without the bin only the get / patch round trip on a zero
 * section runs.
 */
#define TX_ISP_T33
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../driver/t32/tx_isp_t32_rcfg33.h"
#include "../driver/t32/tx_isp_t32_iq.h"
#include "../driver/t32/tx_isp_t33_blk.h"
#include "../driver/t32/tx_isp_t33_af.h"

static int bad;
#define CHECK(c) do { if (!(c)) { bad++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

int main(void)
{
	static u8 sec[T33_AF_SEC_BYTES], par[24], a[24], o[24], bin[0x40000];
	static u32 l1[4096], l2[4096], l3[4096];
	const char *bp = getenv("T33_IQ_BIN");
	u32 off, len, bytes = 0, k;
	int n1, n2, n3;

	/* patch / read round trip on a plain section */
	memset(a, 0, sizeof(a));
	a[8] = 3; a[9] = 2;
	a[10] = 0x11; a[11] = 0x01; a[12] = 0x22; a[13] = 0x02; a[14] = 0x33; a[16] = 0x44; a[17] = 0x04;
	a[0x12] = 5; a[0x13] = 6; a[0x14] = 7; a[0x15] = 8; a[0x16] = 0x55; a[0x17] = 0x05;
	memset(sec, 0, sizeof(sec));
	sec[0x40] = 0x40; sec[0x44] = 0x80;
	t33_af_attr_patch(sec, a);
	memcpy(par, a, 24);
	t33_af_attr_read(sec, par, o);
	CHECK(o[0] == 0x20 && o[4] == 0x10);			/* 0x80 >> 2, 0x40 >> 2 */
	CHECK(o[8] == 3 && o[9] == 2);
	CHECK(!memcmp(o + 10, a + 10, 6) && !memcmp(o + 0x12, a + 0x12, 6));
	CHECK(sec[0x54] == 5 && sec[0x50] == 6 && sec[0x56] == 7 && sec[0x52] == 8 && sec[0x213] == 0x33);

	if (bp) {
		FILE *f = fopen(bp, "rb");

		CHECK(f != NULL);
		if (f) {
			bytes = (u32)fread(bin, 1, sizeof(bin), f);
			fclose(f);
		}
		off = t32_iq_section(bin, bytes, T33_SEC_AF, &len);
		CHECK(off && len == T33_AF_SEC_BYTES);
		if (off && len == T33_AF_SEC_BYTES) {
			memcpy(sec, bin + off, sizeof(sec));
			n1 = t33_af_init(sec, 1920, 1080, 0x1000000, l1, 4096);
			CHECK(n1 > 0);
			/* the get on the untouched section: the bin's values */
			memset(par, 0, sizeof(par));
			t33_af_attr_read(sec, par, o);
			memcpy(a, o, 24);
			/* the vendor's set with exactly what the get answered: no change in the list */
			t33_af_attr_patch(sec, a);
			n2 = t33_af_init(sec, 1920, 1080, 0x1000000, l2, 4096);
			CHECK(n2 == n1 && !memcmp(l1, l2, (size_t)n1 * 4));
			/* a different window count: the list changes in the window words only */
			a[0x14] = (u8)(a[0x14] + 1);
			a[0x15] = (u8)(a[0x15] + 1);
			t33_af_attr_patch(sec, a);
			n3 = t33_af_init(sec, 1920, 1080, 0x1000000, l3, 4096);
			CHECK(n3 == n1);
			for (k = 0; k < (u32)n1; k++)
				if (l1[k] != l3[k])
					break;
			CHECK(k < (u32)n1);
			t33_af_attr_read(sec, par, o);
			CHECK(o[0x14] == a[0x14] && o[0x15] == a[0x15]);
			/* the same attribute again: same list */
			t33_af_attr_patch(sec, a);
			n2 = t33_af_init(sec, 1920, 1080, 0x1000000, l2, 4096);
			CHECK(!memcmp(l2, l3, (size_t)n1 * 4));
		}
	}
	printf("tx_isp_t33_afattr_test: %s\n", bad ? "FAIL" : "PASS");
	return bad != 0;
}
