/* Host test: T33 LSC on the vendor lsc_para layout (driver/t32/tx_isp_t33_lscv.h, openimp #99 group 13b).
 * tisp_lsc_init's flow on the 0xc4-byte block (t33_lsc_vinit: lut_valid_check, interp_by_tgain, interp_by_ct,
 * func_lsc_para_update) + tisp_lsc_write_reg (t33_lsc_write_regs) + the two init stores must give the list 6
 * words and the mesh table of the init builder t33_lsc_init (the QEMU-proven reference); tisp_lsc_switch_para
 * (tx_isp_t33_swmid.h) on that block with a new section must give t33_lsc_init's words / table of the new
 * section at the block's own gain / CT.  Synthetic sections (all four gain modes, several sensor and cell
 * sizes, CTs) and, with T33_IQ_BIN / T33_IQ_BIN2 (cv2005 PRJ008 day / night bins), the real ones.
 * Built -no-pie: the block keeps 32-bit pointers, all buffers are static (below 4 GB). */
#define TX_ISP_T33
#define T33_SW_GLUE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../driver/t32/tx_isp_t32_rcfg33.h"
#include "../driver/t32/tx_isp_t32_iq.h"
#include "../driver/t32/tx_isp_t33_blk.h"
#include "../driver/t32/tx_isp_t33_lscv.h"

static int bad;
#define CHECK(c) do { if (!(c)) { bad++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

#define CAP 64
static u32 lref[CAP], lnew[CAP], vref[T33_LSC_WORDS], outside, cfg = 1;
static u8 lutA[T33_LSC_LUT_BYTES], lutB[T33_LSC_LUT_BYTES];
static struct t33_lsc mA, mB;
static struct t33_lsc_vblk blk;
static u32 *lsc_para[4];
static u8 tparams[17488 + T33_LSC_SEC_BYTES];	/* tparamsP: the LSC section at +17488 */
#define tp_sec (tparams + 17488)
static const u8 *cur_tab;
static u8 bin[2][1 << 20], tabs[8 * T33_LSC_TABLE_BYTES], secs[2][T33_LSC_SEC_BYTES];

int tisp_reg_write_t33(u32 ch, u32 mod, u32 reg, u32 val)
{
	(void)ch;
	if (mod != 6 || reg + 1 > lnew[0] || reg + 1 >= CAP)
		outside++;
	else
		lnew[reg + 1] = val;
	return 0;
}

int isp_printf(int lvl, const char *fmt, ...)
{
	(void)lvl; (void)fmt;
	return 0;
}

static void wr(u32 ch, u32 blk_, u32 sel, u32 val)
{
	tisp_reg_write_t33(ch, blk_, sel, val);
}

static int idle(u32 mod)
{
	(void)mod;
	return 0;
}

/* the driver's callee mapping of tisp_lsc_switch_para (tx_isp_t32_hw.c) */
static int tisp_lsc_lut_valid_check(u32 h, u32 w, u32 mh, u32 mw, u8 *p0, u8 *p1, u8 *p2, u8 *p3, u8 *p4, u8 *p5,
				    u8 *p6)
{
	t33_lsc_mesh_check((int)h, (int)w, (int)mh, (int)mw, p0, p1, p2, p3, (u32 *)p4, p5, (u32 *)p6);
	return 0;
}

static int tisp_lsc_interp_by_tgain(u32 idx, u32 gain, u32 force)
{
	(void)force;
	t33_lsc_interp_by_tgain_v(lsc_para[idx & 3], gain);
	return 0;
}

static int tisp_lsc_interp_by_ct(u32 idx, u32 ct, u32 force)
{
	(void)force;
	t33_lsc_ct_info(lsc_para[idx & 3], &mB, cur_tab, ct);
	return 0;
}

static int func_lsc_para_update(u32 idx)
{
	t33_lsc_para_update_info(lsc_para[idx & 3]);
	return 0;
}

static int tisp_lsc_write_reg(u32 idx, u32 mode)
{
	(void)mode;
	return t33_lsc_write_regs(lsc_para[idx & 3], idx, (u8)cfg, wr, idle);
}

/* unused callees of the other switch_para functions of swmid.h */
static int tisp_ccm_interp_by_ct(u32 a, u32 b, u32 c) { (void)a; (void)b; (void)c; return 0; }
static int tisp_ccm_interp_by_ev(u32 a, u32 b, u32 c, u32 d, u32 e) { (void)a; (void)b; (void)c; (void)d; (void)e; return 0; }
static int tisp_ccm_no_interp(u32 a) { (void)a; return 0; }
static int tisp_ccm_calc_para_refresh(u32 a) { (void)a; return 0; }
static int tisp_ccm_write_reg(u32 a, u32 b) { (void)a; (void)b; return 0; }
static int tisp_csccr_para_refresh_by_mode(u32 a, u32 b) { (void)a; (void)b; return 0; }
static int tisp_csccr_sync_api_para(u32 a, u32 b) { (void)a; (void)b; return 0; }
static int tisp_csccr_bin_par_update(u32 a) { (void)a; return 0; }
static int defog_count_weight35abc(u32 a, u32 b, u32 c, u32 d, u8 *p0, u8 *p1, u8 *p2, u8 *p3, u8 *p4, u8 *p5, u8 *p6)
{
	(void)a; (void)b; (void)c; (void)d; (void)p0; (void)p1; (void)p2; (void)p3; (void)p4; (void)p5; (void)p6;
	return 0;
}
static int tisp_defog_write_reg_para1(u32 a) { (void)a; return 0; }
static int tisp_defog_interp_trans_k(u32 a, u32 k, u8 *p0, u8 *p1, u8 *p2, u8 *p3, u8 *p4, u8 *p5, u8 *p6)
{
	(void)a; (void)k; (void)p0; (void)p1; (void)p2; (void)p3; (void)p4; (void)p5; (void)p6;
	return 0;
}
static int tisp_defog_update_reg_para2(u32 a) { (void)a; return 0; }
static int tisp_defog_write_reg_para2(u32 a) { (void)a; return 0; }
static int tisp_defog_write_reg_para3(u32 a, u32 b) { (void)a; (void)b; return 0; }
#include "../driver/t32/tx_isp_t33_swmid.h"

static u32 rnd = 12345;
static u32 rn(void)
{
	rnd = rnd * 1103515245U + 12345U;
	return rnd >> 8;
}

/* a synthetic section 7 with n tables (CTs ascending), mode, cell size, distance table on / off */
static void synth(u8 *s, u8 *t, u32 n, u32 mode, u32 mh, u32 mw)
{
	u32 i, k;

	for (i = 0; i < T33_LSC_SEC_BYTES; i++)
		s[i] = (u8)rn();
	s[4] = (u8)mode; s[5] = s[6] = s[7] = 0;
	s[0x58] = (u8)mh; s[0x59] = (u8)mw;
	s[0x5b] = (u8)(rn() % 4);
	s[0x57] = (u8)(rn() & 1);
	for (k = 0; k < 8; k++) {	/* ascending distance knots */
		s[0x18 + 2 * k] = (u8)(400 * (k + 1));
		s[0x19 + 2 * k] = (u8)((400 * (k + 1)) >> 8);
	}
	for (k = 0; k < 8; k++) {	/* small CT hysteresis */
		s[0x28 + 2 * k] = (u8)(rn() % 200);
		s[0x29 + 2 * k] = 0;
	}
	s[0x94] = (u8)n;
	for (i = 0; i < n; i++) {
		u8 *p = t + i * T33_LSC_TABLE_BYTES;
		u32 ct = 2500 + 1500 * i;

		p[0] = (u8)ct; p[1] = (u8)(ct >> 8);
		for (k = 2; k < T33_LSC_TABLE_BYTES; k += 2) {
			u32 v = 0x800 + rn() % 0x800;

			p[k] = (u8)v; p[k + 1] = (u8)(v >> 8);
		}
	}
}

/* reference: the init builder; new: the vendor-layout block + write_reg + the init stores */
static int one(u8 *sec, const u8 *tab, u32 tb, u32 h, u32 w, u32 gain, u32 ct)
{
	int len;

	memset(lref, 0, sizeof(lref));
	memset(lnew, 0, sizeof(lnew));
	memset(lutA, 0, sizeof(lutA));
	memset(lutB, 0, sizeof(lutB));
	len = t33_lsc_init(sec, tab, tb, h, w, gain, ct, vref, &mA, lutA, 0x1234000, lref, CAP);
	if (len <= 0)
		return 0;
	memcpy(tp_sec, sec, T33_LSC_SEC_BYTES);
	t33_rcfg_tmpl_init(lnew, CAP, t33_lsc_tmpl, ARRAY_SIZE(t33_lsc_tmpl));
	CHECK(!t33_lsc_vinit(&blk, &mB, tp_sec, tab, tb, h, w, gain, ct, lutB, 0x1234000));
	lsc_para[0] = blk.blk;
	cur_tab = tab;
	CHECK(t33_lsc_write_regs(blk.blk, 0, 1, wr, idle) == 1);
	tisp_reg_write_t33(0, 6, 6, 1);
	tisp_reg_write_t33(0, 6, 1, 1);
	CHECK(!memcmp(lref, lnew, sizeof(lref)));
	CHECK(!memcmp(lutA, lutB, sizeof(lutA)));
	CHECK(!memcmp(vref + 7, blk.blk + 7, (T33_LSC_WORDS - 7) * 4));
	return 1;
}

/* the switch to sec2 (tables tab2) at the block's gain / CT vs the init builder on sec2 */
static void sw(const u8 *sec2, const u8 *tab2, u32 tb2, u32 h, u32 w, u32 gain, u32 ct)
{
	static u8 *tp[4];
	u8 s2[T33_LSC_SEC_BYTES];

	memcpy(s2, sec2, sizeof(s2));
	memset(lref, 0, sizeof(lref));
	memset(lutA, 0, sizeof(lutA));
	if (t33_lsc_init(s2, tab2, tb2, h, w, gain, ct, vref, &mA, lutA, 0x1234000, lref, CAP) <= 0)
		return;
	memcpy(tp_sec, sec2, T33_LSC_SEC_BYTES);
	cur_tab = tab2;
	tp[0] = tparams;
	t33_lsc_switch_para(0, tp, lsc_para);
	CHECK(!outside);
	CHECK(!memcmp(lref, lnew, sizeof(lref)));
	CHECK(!memcmp(lutA, lutB, sizeof(lutA)));
}

static u32 load(const char *p, u8 *b)
{
	FILE *f = p ? fopen(p, "rb") : NULL;
	u32 n;

	if (!f)
		return 0;
	n = (u32)fread(b, 1, sizeof(bin[0]), f);
	fclose(f);
	return n;
}

int main(void)
{
	static const u32 sz[][2] = { { 1080, 1920 }, { 1440, 2560 }, { 1520, 2688 }, { 720, 1280 } };
	static const u32 cells[][2] = { { 64, 64 }, { 80, 80 }, { 32, 48 } };
	static const u32 gains[] = { 0x10000, 0x18000, 0x40000, 0x9a3c1 };
	static const u32 cts[] = { 2000, 3100, 4174, 5000, 6500, 9000 };
	const char *bp[2] = { getenv("T33_IQ_BIN"), getenv("T33_IQ_BIN2") };
	u32 m, s, c, g, k, n = 0, nb = 0, len, off, bytes[2];

	for (m = 0; m < 4; m++)
		for (s = 0; s < 4; s++)
			for (c = 0; c < 3; c++)
				for (g = 0; g < 4; g++) {
					u32 nt = 2 + (rn() % 4), ct = cts[rn() % 6];

					synth(secs[0], tabs, nt, m, cells[c][0], cells[c][1]);
					if (!one(secs[0], tabs, nt * T33_LSC_TABLE_BYTES, sz[s][0], sz[s][1], gains[g], ct))
						continue;
					n++;
					/* a second section with the same tables: the day / night switch */
					synth(secs[1], tabs + T33_LSC_TABLE_BYTES * 8 - T33_LSC_TABLE_BYTES, 0, (m + 1) & 3,
					      cells[c][0], cells[c][1]);
					memcpy(secs[1] + 0x94, secs[0] + 0x94, 1);
					sw(secs[1], tabs, nt * T33_LSC_TABLE_BYTES, sz[s][0], sz[s][1], gains[g], ct);
				}
	for (k = 0; k < 2; k++)
		bytes[k] = load(bp[k], bin[k]);
	if (bytes[0]) {
		for (k = 0; k < 2; k++) {
			if (!bytes[k] || !(off = t32_iq_section(bin[k], bytes[k], T33_SEC_LSC, &len)) || len != T33_LSC_SEC_BYTES)
				continue;
			memcpy(secs[k], bin[k] + off, len);
		}
		off = t32_iq_section(bin[0], bytes[0], T33_SEC_LSC, &len);
		CHECK(off && len == T33_LSC_SEC_BYTES);
		if (off && one(secs[0], bin[0] + off + len, t32_iq_payload(bin[0] + off, bytes[0] - off, T33_SEC_LSC),
			       1080, 1920, 0x10000, 5000))
			nb++;
		if (bytes[1] && (off = t32_iq_section(bin[1], bytes[1], T33_SEC_LSC, &len)) && len == T33_LSC_SEC_BYTES) {
			sw(secs[1], bin[1] + off + len, t32_iq_payload(bin[1] + off, bytes[1] - off, T33_SEC_LSC), 1080,
			   1920, 0x10000, 5000);
			nb++;
		}
	}
	CHECK(n > 50);
	printf("tx_isp_t33_lscv_test: %u synthetic init + switch cases, %u bin cases: %s\n", n, nb, bad ? "FAIL" : "PASS");
	return bad != 0;
}
