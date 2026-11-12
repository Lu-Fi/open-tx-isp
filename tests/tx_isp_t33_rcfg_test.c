/* Host test: T33 per-block register lists (driver/t32/tx_isp_t32_rcfg33.h,
 * T33 CDNS in tx_isp_t32_cdns.h, PRJ008 IQ bin walk in tx_isp_t32_iq.h).
 * Without arguments: list builder checks.  With T33_IQ_BIN (a cv2005
 * PRJ008 bin) and T33_LIST_REF (lines "list reg val" of the vendor module's
 * first run of each list, from a local QEMU trace) the top, CSC, CSCCR and
 * CDNS lists must give the vendor's writes (1920x1080, RGGB10). */
#define TX_ISP_T33
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../driver/t32/tx_isp_t32_rcfg33.h"
#include "../driver/t32/tx_isp_t32_iq.h"
#include "../driver/t32/tx_isp_t32_cdns.h"

static int bad;
#define CHECK(c) do { if (!(c)) { bad++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

/* decode a list into its register writes */
static unsigned int decode(const u32 *l, u32 *reg, u32 *val, unsigned int max)
{
	unsigned int i = 1, n = 0, k, c;

	while (i <= l[0]) {
		u32 cmd = l[i++];

		c = t32_rcfg_cmd_count(cmd);
		for (k = 0; k < c && i <= l[0] && n < max; k++, n++) {
			reg[n] = t32_rcfg_cmd_offset(cmd) + ((cmd & T32_RCFG_CMD) ? 4 * k : 0);
			val[n] = l[i++];
		}
	}
	return n;
}

static u32 ref_reg[64][1024], ref_val[64][1024];
static unsigned int ref_n[64];

static void vs_ref(const char *what, unsigned int id, const struct t32_front_write *w, unsigned int n)
{
	static u32 buf[T33_SLOT_WORDS], reg[1024], val[1024];
	struct t32_rcfg_list l;
	unsigned int m, i;

	CHECK(t33_rcfg_build(&l, buf, T33_SLOT_WORDS, w, n) > 0);
	m = decode(buf, reg, val, 1024);
	if (!ref_n[id]) {
		printf("  %s: list %u, %u writes (no reference)\n", what, id, m);
		return;
	}
	CHECK(m == ref_n[id]);
	for (i = 0; i < m && i < ref_n[id]; i++)
		if (reg[i] != ref_reg[id][i] || val[i] != ref_val[id][i]) {
			bad++;
			printf("FAIL %s #%u: %05x=%08x vendor %05x=%08x\n", what, i, reg[i], val[i],
			       ref_reg[id][i], ref_val[id][i]);
		}
	printf("  %s: list %u, %u writes vs vendor %u\n", what, id, m, ref_n[id]);
}

int main(void)
{
	static struct t32_front_write w[256];
	static u32 buf[64];
	struct t32_rcfg_list l;
	const char *bp = getenv("T33_IQ_BIN"), *rp = getenv("T33_LIST_REF");

	/* builder: ascending, block id dropped, runs merged */
	{
		struct t32_front_write in[] = { { 0xb048, 3 }, { 0xb010, 1 }, { 0xb000, 0x1234 }, { 0xb040, 2 },
						{ 0xb044, 5 } };

		CHECK(t33_rcfg_build(&l, buf, 64, in, 5) == 6);
		CHECK(buf[1] == (T32_RCFG_CMD | 0xb010 >> 2) && buf[2] == 1);
		CHECK(buf[3] == (T32_RCFG_CMD | 2 << 18 | 0xb040 >> 2));
		CHECK(buf[4] == 2 && buf[5] == 5 && buf[6] == 3);
		CHECK(t33_rcfg_build(&l, buf, 4, in, 5) == -1);
	}
	if (bp && rp) {
		static u8 bin[0x40000];
		FILE *f = fopen(bp, "rb"), *r = fopen(rp, "r");
		u32 bytes, off, len, id, reg, val;
		struct t32_front_write dw[T33_TOP_DIRECT_WRITES];
		int k;

		CHECK(f && r);
		if (!f || !r)
			return 1;
		bytes = fread(bin, 1, sizeof(bin), f);
		while (fscanf(r, "%u %x %x", &id, &reg, &val) == 3)
			if (id < 64 && ref_n[id] < 1024) {
				ref_reg[id][ref_n[id]] = reg;
				ref_val[id][ref_n[id]++] = val;
			}
		/* every section of the walk starts with a section id */
		for (id = 0; id < T32_IQ_SECTIONS; id++) {
			off = t32_iq_section(bin, bytes, id, &len);
			if (off && len)
				CHECK(bin[off + 2] == 0x35 && bin[off + 3] == 0x01);
		}
		off = t32_iq_section(bin, bytes, T32_IQ_SEC_TOP, &len);
		CHECK(off && len == T33_TOP_BYTES);
		t33_top_init(bin + off, 1920, 1080, 0x10000, 0, w, dw);
		vs_ref("top", T33_MOD_TOP, w, T33_TOP_LIST_WRITES);
		off = t32_iq_section(bin, bytes, T32_IQ_SEC_CSC, &len);
		CHECK(off && len == T32_CSC_BYTES);
		vs_ref("csc", T33_MOD_CSC, w, t32_csc_init(bin + off, w));
		off = t32_iq_section(bin, bytes, T32_IQ_SEC_CSCCR, &len);
		CHECK(off && len == T32_CSCCR_BYTES);
		k = t32_csccr_init(bin + off, w);
		CHECK(k > 0);
		for (id = 0; k > 0 && id < (u32)k; id++)
			if (w[id].reg == 0x14010)
				w[id].val = T33_CSCCR_EN;
		vs_ref("csccr", T33_MOD_CSCCR, w, k > 0 ? k : 0);
		off = t32_iq_section(bin, bytes, T32_SEC_CDNS, &len);
		CHECK(off && len == T33_CDNS_SEC_BYTES);
		vs_ref("cdns", T33_MOD_CDNS, w, t33_cdns_init(bin + off, w));
	}
	printf("tx_isp_t33_rcfg_test: %s\n", bad ? "FAIL" : "PASS");
	return bad != 0;
}
