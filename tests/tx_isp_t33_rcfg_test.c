/* Host test: T33 per-block register lists (driver/t32/tx_isp_t32_rcfg33.h,
 * T33 CDNS in tx_isp_t32_cdns.h, PRJ008 IQ bin walk in tx_isp_t32_iq.h).
 * Without arguments: list builder checks.  With T33_IQ_BIN (a cv2005
 * PRJ008 bin) and T33_LIST_REF (lines "list reg val" of the vendor module's
 * first run of each list, from a local QEMU trace) the top, CSC, CSCCR and
 * CDNS lists, and BLC / YDNS / SDNS in their module layouts, must give the
 * vendor's writes (1920x1080, RGGB10). */
#define TX_ISP_T33
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../driver/t32/tx_isp_t32_rcfg33.h"
#include "../driver/t32/tx_isp_t32_iq.h"
#include "../driver/t32/tx_isp_t32_cdns.h"
#include "../driver/t32/tx_isp_t33_blk.h"
#include "../driver/t32/tx_isp_t33_dmsc.h"
#include "../driver/t32/tx_isp_t33_dpc.h"
#include "../driver/t32/tx_isp_t33_ysp.h"
#include "../driver/t32/tx_isp_t33_rdns.h"
#include "../driver/t32/tx_isp_t33_ccm.h"
#include "../driver/t32/tx_isp_t33_lsc.h"
#include "../driver/t32/tx_isp_t33_gamma.h"
#include "../driver/t32/tx_isp_t33_defog.h"
#include "../driver/t32/tx_isp_t33_lce.h"
#include "../driver/t32/tx_isp_t33_af.h"
#include "../driver/t32/tx_isp_t33_bcsh.h"
#include "../driver/t32/tx_isp_t33_tmo.h"
#include "../driver/t32/tx_isp_t33_clm.h"
#include "../driver/t32/tx_isp_t33_msca.h"
#include "../driver/t32/tx_isp_t33_mdns.h"
#include "../driver/t32/tx_isp_t33_ae.h"

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

/* a module layout filled with the block's writes against the vendor's list */
static void vs_tmpl(const char *what, unsigned int id, const struct t33_tcmd *t, unsigned int nt,
		    const struct t32_front_write *w, int n)
{
	static u32 buf[T33_SLOT_WORDS], reg[1024], val[1024];
	unsigned int m, i;
	int len;

	CHECK(n > 0);
	len = t33_rcfg_build_tmpl(buf, T33_SLOT_WORDS, t, nt, w, n > 0 ? (unsigned int)n : 0);
	CHECK(len > 0);
	if (len <= 0)
		return;
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
	printf("  %s: list %u, %u words, %u writes vs vendor %u\n", what, id, len, m, ref_n[id]);
}

/* a list filled the vendor's way (layout + slot stores) against the vendor's list;
 * skip: a register whose vendor value is not defined (0: none) */
static void vs_list_skip(const char *what, unsigned int id, const u32 *buf, int len, u32 skip)
{
	static u32 reg[1024], val[1024];
	unsigned int m, i;

	CHECK(len > 0);
	if (len <= 0)
		return;
	m = decode(buf, reg, val, 1024);
	CHECK(m == ref_n[id]);
	for (i = 0; i < m && i < ref_n[id]; i++)
		if (reg[i] != ref_reg[id][i] || (val[i] != ref_val[id][i] && (!skip || reg[i] != skip))) {
			bad++;
			printf("FAIL %s #%u: %05x=%08x vendor %05x=%08x\n", what, i, reg[i], val[i],
			       ref_reg[id][i], ref_val[id][i]);
		}
	printf("  %s: list %u, %d words, %u writes vs vendor %u\n", what, id, len, m, ref_n[id]);
}

static void vs_list(const char *what, unsigned int id, const u32 *buf, int len)
{
	vs_list_skip(what, id, buf, len, 0);
}

#define TMPL(x) x, sizeof(x) / sizeof(x[0])

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
		/* tparamsP offsets of the vendor's block pointers */
		CHECK(t33_tp_offset(bin, T33_SEC_YDNS) == 0x88ac);
		CHECK(t33_tp_offset(bin, T33_SEC_SDNS) == 0x9b20);
		CHECK(t33_tp_offset(bin, T33_SEC_BLC) == 0x44e8);
		off = t32_iq_section(bin, bytes, T33_SEC_YDNS, &len);
		CHECK(off && len == T33_YDNS_SEC_BYTES);
		vs_tmpl("ydns", T33_MOD_YDNS, TMPL(t33_ydns_tmpl), w, t33_ydns_init(bin + off, 0x10000, w));
		off = t32_iq_section(bin, bytes, T33_SEC_SDNS, &len);
		CHECK(off && len == T33_SDNS_SEC_BYTES);
		vs_tmpl("sdns", T33_MOD_SDNS, TMPL(t33_sdns_tmpl), w, t33_sdns_init(bin + off, 0x10000, w));
		off = t32_iq_section(bin, bytes, T33_SEC_BLC, &len);
		CHECK(off && len == T33_BLC_SEC_BYTES);
		vs_tmpl("blc", T33_MOD_BLC, TMPL(t33_blc_tmpl), w, t33_blc_init(bin + off, 0, 0x10000, w));
		{
			static u32 v[0x1000], lb[T33_SLOT_WORDS];

			off = t32_iq_section(bin, bytes, T33_SEC_DMSC, &len);
			CHECK(off && len == T33_DMSC_SEC_BYTES);
			vs_list("dmsc", T33_MOD_DMSC, lb, t33_dmsc_init(bin + off, 0x10000, v, lb, T33_SLOT_WORDS));
			CHECK(t33_tp_offset(bin, T33_SEC_DPC) == 0x3e2c);
			off = t32_iq_section(bin, bytes, T33_SEC_DPC, &len);
			CHECK(off && len == T33_DPC_SEC_BYTES);
			vs_list("dpc", T33_MOD_DPC, lb, t33_dpc_init(bin + off, 0x10000, v, lb, T33_SLOT_WORDS));
#define VB(blk, BLK, name)								\
			off = t32_iq_section(bin, bytes, T33_SEC_##BLK, &len);		\
			CHECK(off && len == T33_##BLK##_SEC_BYTES);			\
			vs_list(name, T33_MOD_##BLK, lb, t33_##blk##_init(bin + off, 0x10000, v, lb, T33_SLOT_WORDS))
			VB(ysp, YSP, "ysp");
			/* RDNS reads the BLC (0x3044) and DG (0x4060) list values: the vendor's */
			off = t32_iq_section(bin, bytes, T33_SEC_RDNS, &len);
			CHECK(off && len == T33_RDNS_SEC_BYTES);
			vs_list("rdns", T33_MOD_RDNS, lb, t33_rdns_init(bin + off, 0x10000, 0xffffffffU, ref_val[4][1],
								  ref_val[5][1], v, lb, T33_SLOT_WORDS));
			off = t32_iq_section(bin, bytes, T33_SEC_CCM, &len);
			CHECK(off && len == T33_CCM_SEC_BYTES);
			vs_list("ccm", T33_MOD_CCM, lb, t33_ccm_init(bin + off, bin + off + len,
								 t32_iq_payload(bin + off, bytes - off, T33_SEC_CCM),
								 T33_CCM_INIT_CT, 0, 0, v, lb, T33_SLOT_WORDS));
			off = t32_iq_section(bin, bytes, T33_SEC_GAMMA, &len);
			CHECK(off && len == T33_GAMMA_SEC_BYTES);
			vs_list("gamma", T33_MOD_GAMMA, lb, t33_gamma_init(bin + off, bin + off + len,
								     t32_iq_payload(bin + off, bytes - off, T33_SEC_GAMMA),
								     0, 0, lb, T33_SLOT_WORDS));
			{
				/* defog: the statistics DMA address (0xa210) as the vendor's */
				u32 phys = 0;

				for (id = 0; id < ref_n[T33_MOD_DEFOG]; id++)
					if (ref_reg[T33_MOD_DEFOG][id] == 0xa210)
						phys = ref_val[T33_MOD_DEFOG][id];
				static u8 dsec[T33_DEFOG_SEC_BYTES];

				off = t32_iq_section(bin, bytes, T33_SEC_DEFOG, &len);
				CHECK(off && len == T33_DEFOG_SEC_BYTES);
				memcpy(dsec, bin + off, sizeof(dsec));
				/* weights for another size / zoom: recomputed (defog_count_weight35abc) */
				if (t33_defog_weights_needed(dsec, 1920, 1080)) {
					u8 *m = calloc(1, t33_defog_scratch_bytes(1920, 1080));

					CHECK(m != NULL);
					if (m)
						t33_defog_count_weight(dsec, 1920, 1080, m);
					free(m);
					printf("  defog: weights recomputed (zoom 0x%02x)\n", dsec[0x1b1]);
				}
				vs_list("defog", T33_MOD_DEFOG, lb, t33_defog_init(dsec, 1920, 1080, phys, lb,
										   T33_SLOT_WORDS));
			}
			{
				/* LCE: the statistics (0xc090) and curve (0xc0a4) addresses as the vendor's */
				static u8 lsec[T33_LCE_SEC_BYTES], cbuf[T33_LCE_CURVE_BYTES];
				u32 st = 0, cv = 0;

				for (id = 0; id < ref_n[T33_MOD_LCE]; id++) {
					if (ref_reg[T33_MOD_LCE][id] == 0xc090)
						st = ref_val[T33_MOD_LCE][id];
					if (ref_reg[T33_MOD_LCE][id] == 0xc0a4)
						cv = ref_val[T33_MOD_LCE][id];
				}
				off = t32_iq_section(bin, bytes, T33_SEC_LCE, &len);
				CHECK(off && len == T33_LCE_SEC_BYTES);
				CHECK(t33_tp_offset(bin, T33_SEC_LCE) == 0x5e5c);
				memcpy(lsec, bin + off, sizeof(lsec));
				vs_list("lce", T33_MOD_LCE, lb, t33_lce_init(lsec, 1920, 1080, 0, 0, cv, st, lb, T33_SLOT_WORDS));
				/* the size calculation gives the vendor's 1080p defaults */
				CHECK(!memcmp(lsec + 6, bin + off + 6, 0x1c + 1 - 6));
				CHECK(t32_tp_le16(lsec + 0x2ea) == 7200 && t32_tp_le16(lsec + 0x2f8) == 4660);
				t33_lce_curve_default(cbuf);
				CHECK(cbuf[0] == 0 && cbuf[19] == 0xf0 && cbuf[0x1680] == 0 && cbuf[0x2cff] == 0xf0);
				/* another size: grid 2 * (360 / 12) x 2 * (640 / 24) with remainders */
				CHECK(t33_lce_init(lsec, 1280, 720, 0, 0, cv, st, lb, T33_SLOT_WORDS) > 0);
				CHECK(lsec[6] == 60 && lsec[7] == 52 && lsec[8] == 0 && lsec[9] == 16);
			}
			{
				/* AF: the statistics address (0x18108) as the vendor's */
				static u8 asec[T33_AF_SEC_BYTES];
				u32 dma = 0;

				for (id = 0; id < ref_n[T33_MOD_AF]; id++)
					if (ref_reg[T33_MOD_AF][id] == 0x18108)
						dma = ref_val[T33_MOD_AF][id];
				off = t32_iq_section(bin, bytes, T33_SEC_AF, &len);
				CHECK(off && len == T33_AF_SEC_BYTES);
				CHECK(t33_tp_offset(bin, T33_SEC_AF) == 0x9fec);
				memcpy(asec, bin + off, sizeof(asec));
				/* 0x1812c: no store in the vendor code, its list word is stale memory */
				vs_list_skip("af", T33_MOD_AF, lb, t33_af_init(asec, 1920, 1080, dma, lb, T33_SLOT_WORDS),
					     0x1812c);
			}
			off = t32_iq_section(bin, bytes, T33_SEC_MSCA, &len);
			CHECK(off && len > 0x30);
			vs_list("msca", T33_MOD_MSCA, lb, t33_msca_init(1920, 1080, bin + off, lb, T33_SLOT_WORDS));
			{
				/* MDNS: gain 1.0, zip ratio 15, the ISP memory address (0xd040) as the vendor's;
				 * then the decode words from the YSP block state */
				static u32 yv[T33_YSP_WORDS], mv[T33_MDNS_WORDS], yl[T33_SLOT_WORDS];
				u32 pa = 0, d[2] = { 0, 0 };
				int n;

				for (id = 0; id < ref_n[T33_MOD_MDNS]; id++)
					if (ref_reg[T33_MOD_MDNS][id] == 0xd040)
						pa = ref_val[T33_MOD_MDNS][id];
				off = t32_iq_section(bin, bytes, T33_SEC_YSP, &len);
				t33_ysp_init(bin + off, 0x10000, yv, yl, T33_SLOT_WORDS);
				off = t32_iq_section(bin, bytes, T33_SEC_MDNS, &len);
				CHECK(off && len == T33_MDNS_SEC_BYTES);
				CHECK(t33_tp_offset(bin, T33_SEC_MDNS) == 0x6238);
				n = t33_mdns_init(bin + off, 0x10000, 1920, 1080, 1500, pa, mv, lb, T33_SLOT_WORDS, d);
				if (n > 0)
					t33_mdns_decode(yv, lb);
				vs_list("mdns", T33_MOD_MDNS, lb, n);
				CHECK(d[0] == 8102 && d[1] == 2);
			}
			{
				/* AE (list 30): buffer addresses as the vendor's; 0x1708c is not in the
				 * QEMU trace (dropped before the compare); 0x170b8: no store in the
				 * vendor code, its list word is stale memory.  DG (list 5) at 1.0. */
				static u32 al[T33_SLOT_WORDS], reg[64], val[64];
				u32 st = 0, hi = 0;
				unsigned int m, i, j;

				for (id = 0; id < ref_n[T33_MOD_AE]; id++) {
					if (ref_reg[T33_MOD_AE][id] == 0x17068)
						st = ref_val[T33_MOD_AE][id];
					if (ref_reg[T33_MOD_AE][id] == 0x17078)
						hi = ref_val[T33_MOD_AE][id];
				}
				off = t32_iq_section(bin, bytes, T33_SEC_AE, &len);
				CHECK(off && len >= T33_AE_SEC_MIN && t33_tp_offset(bin, T33_SEC_AE) == 0x70);
				CHECK(t33_ae_init(bin + off, 1920, 1080, st, hi, al, T33_SLOT_WORDS) > 0);
				m = decode(al, reg, val, 64);
				for (i = j = 0; i < m; i++)
					if (reg[i] != 0x1708c) {
						reg[j] = reg[i];
						val[j++] = val[i];
					}
				CHECK(j == ref_n[T33_MOD_AE]);
				for (i = 0; i < j && i < ref_n[T33_MOD_AE]; i++)
					if (reg[i] != ref_reg[T33_MOD_AE][i] ||
					    (val[i] != ref_val[T33_MOD_AE][i] && reg[i] != 0x170b8)) {
						bad++;
						printf("FAIL ae #%u: %05x=%08x vendor %05x=%08x\n", i, reg[i], val[i],
						       ref_reg[T33_MOD_AE][i], ref_val[T33_MOD_AE][i]);
					}
				vs_list("dg", T33_MOD_DG, al, t33_dg_init(T33_AE_DG_INIT, al, T33_SLOT_WORDS));
			}
			{
				/* CLM: CT 5000 K, the csc section */
				u32 coff, clen;

				off = t32_iq_section(bin, bytes, T33_SEC_CLM, &len);
				CHECK(off && len == T33_CLM_SEC_BYTES);
				CHECK(t33_tp_offset(bin, T33_SEC_CLM) == 0x903c);
				coff = t32_iq_section(bin, bytes, T32_IQ_SEC_CSC, &clen);
				vs_list("clm", T33_MOD_CLM, lb,
					t33_clm_init(bin + off, bin + off + len, t32_iq_payload(bin + off, bytes - off, T33_SEC_CLM),
						     bin + coff, 5000, lb, T33_SLOT_WORDS));
			}
			{
				/* TMO: EV 0, the V table DMA address (0x70e0) as the vendor's */
				static u32 tv[T33_TMO_WORDS], vt[T33_TMO_V_WORDS];
				u32 dma = 0;

				for (id = 0; id < ref_n[T33_MOD_TMO]; id++)
					if (ref_reg[T33_MOD_TMO][id] == 0x70e0)
						dma = ref_val[T33_MOD_TMO][id];
				off = t32_iq_section(bin, bytes, T33_SEC_TMO, &len);
				CHECK(off && len == T33_TMO_SEC_BYTES);
				CHECK(t33_tp_offset(bin, T33_SEC_TMO) == 0x279c);
				vs_list("tmo", T33_MOD_TMO, lb, t33_tmo_init(bin + off, 1920, 1080, 0, dma, tv, lb, T33_SLOT_WORDS));
				t33_tmo_vtable(vt);
				CHECK(vt[0] == 0x3319a000 && vt[1] == 0x06664cd3 && vt[1499] == vt[3]);
			}
			{
				/* BCSH: CT 5000 K, EV 0 (T33_BCSH_CT / T33_BCSH_EV override) */
				static u8 bsec[T33_BCSH_SEC_BYTES], bst[T33_BCSH_ST_BYTES];
				u32 coff, clen;
				const char *ce = getenv("T33_BCSH_CT"), *ee = getenv("T33_BCSH_EV");

				off = t32_iq_section(bin, bytes, T33_SEC_BCSH, &len);
				CHECK(off && len == T33_BCSH_SEC_BYTES);
				CHECK(t33_tp_offset(bin, T33_SEC_BCSH) == 0x8b78);
				coff = t32_iq_section(bin, bytes, T32_IQ_SEC_CSC, &clen);
				memcpy(bsec, bin + off, sizeof(bsec));
				vs_list("bcsh", T33_MOD_BCSH, lb,
					t33_bcsh_init(bsec, bin + off + len, t32_iq_payload(bin + off, bytes - off, T33_SEC_BCSH),
						      bin + coff, ce ? atoi(ce) : 5000, ee ? strtoull(ee, NULL, 0) : 0, bst, lb,
						      T33_SLOT_WORDS));
			}
			{
				/* LSC: the DMA table address (slot 16) as the vendor's; 0x507c is slab slack there */
				static struct t33_lsc m;
				static u8 lut[T33_LSC_LUT_BYTES];
				u32 phys = 0;

				for (id = 0; id < ref_n[T33_MOD_LSC]; id++)
					if (ref_reg[T33_MOD_LSC][id] == 0x5078)
						phys = ref_val[T33_MOD_LSC][id];
				off = t32_iq_section(bin, bytes, T33_SEC_LSC, &len);
				CHECK(off && len == T33_LSC_SEC_BYTES);
				vs_list_skip("lsc", T33_MOD_LSC, lb,
					     t33_lsc_init(bin + off, bin + off + len,
							  t32_iq_payload(bin + off, bytes - off, T33_SEC_LSC), 1080, 1920,
							  0x10000, 5000, v, &m, lut, phys, lb, T33_SLOT_WORDS), 0x507c);
				CHECK(m.rows == 36 && m.cols == 62 && m.cells == 2232);
				/* the table: row 0 cells 0/1 of the blend of the 3850 / 6300 K tables */
				{
					/* 5000 K: 3950..6200 K, f = 1911 / 4096; identity distance gains */
					static const u8 c01[9] = { 0xd7, 0x88, 0x88, 0x97, 0x78, 0x8b, 0x6b, 0x98, 0x87 };

					CHECK(!memcmp(lut, c01, 9) && !lut[279]);
				}
			}
		}
	}
	printf("tx_isp_t33_rcfg_test: %s\n", bad ? "FAIL" : "PASS");
	return bad != 0;
}
