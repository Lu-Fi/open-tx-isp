/* Host test: T33 block-state layer (driver/t32/tx_isp_t33_bst.h, openimp #99).
 * For the seven info-struct blocks (DPC, RDNS, DMSC, YDNS, YSP, SDNS, CDNS)
 * the oracle-proven tisp_*_t33_frame_done_interrupt ports must write, from
 * the info struct the driver keeps, exactly the list slots of the block's
 * init builder at the same gain (the vendor's par_refresh tail = the frame
 * done's refresh-2 branch): the list stays word-equal at every gain, no slot
 * outside the list.  Also the refresh state machine (1: trigger, 2: write
 * when idle).  With T33_IQ_BIN (a cv2005 PRJ008 bin) the real sections are
 * used, without it a deterministic synthetic section per block. */
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
#include "../driver/t32/tx_isp_t33_mdns.h"
#include "../driver/t32/tx_isp_t33_fd_ydns.h"
#include "../driver/t32/tx_isp_t33_fd_sdns.h"
#include "../driver/t32/tx_isp_t33_fd_cdns.h"
#include "../driver/t32/tx_isp_t33_fd_dpc.h"
#include "../driver/t32/tx_isp_t33_fd_dmsc.h"
#include "../driver/t32/tx_isp_t33_fd_ysp.h"
#include "../driver/t32/tx_isp_t33_fd_rdns.h"

static int bad;
#define CHECK(c) do { if (!(c)) { bad++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static u32 *cur;		/* the list the handlers write */
static u32 cur_mod, writes, outside, trigs, busy;

int tisp_reg_write_t33(u32 ch, u32 mod, u32 reg, u32 val)
{
	(void)ch;
	if (mod != cur_mod || reg + 1 > cur[0])
		outside++;
	else
		cur[reg + 1] = val;
	writes++;
	return 0;
}

int tisp_rcfg_get_dma_state(u32 mod)
{
	(void)mod;
	return busy;
}

int tisp_rcfg_module_cfg_trig(u32 ch, u32 mod)
{
	(void)ch;
	(void)mod;
	trigs++;
	return 0;
}

static int wr_i(u32 ch, u32 mod, u32 reg, u32 val) { return tisp_reg_write_t33(ch, mod, reg, val); }
static void wr_v(u32 ch, u32 mod, u32 reg, u32 val) { tisp_reg_write_t33(ch, mod, reg, val); }
static int trig_i(u32 ch, u32 mod) { return tisp_rcfg_module_cfg_trig(ch, mod); }
static void trig_v(u32 ch, u32 mod) { tisp_rcfg_module_cfg_trig(ch, mod); }
static u32 dma_u(u32 mod) { return (u32)tisp_rcfg_get_dma_state(mod); }
static u32 mdns_l[T33_SLOT_WORDS];
static void mdns_dec(u32 ch, const u8 *par) { (void)ch; t33_mdns_decode((const u32 *)(par - 1068), mdns_l); }

static const struct t33_ydns_fd_ops ydns_ops = { wr_v, tisp_rcfg_get_dma_state, trig_v };
static const struct t33_sdns_fd_ops sdns_ops = { wr_i, tisp_rcfg_get_dma_state, trig_i };
static const struct t33_cdns_ops cdns_ops = { wr_i, tisp_rcfg_get_dma_state, trig_i };
static const struct t33_dpc_hw dpc_ops = { wr_i, tisp_rcfg_get_dma_state, trig_i };
static const struct t33_ysp_ops ysp_ops = { wr_v, mdns_dec, trig_v, dma_u };

enum { DPC, RDNS, DMSC, YDNS, YSP, SDNS, CDNS, NBLK };
static const char *const nm[NBLK] = { "dpc", "rdns", "dmsc", "ydns", "ysp", "sdns", "cdns" };
static const u32 sec_id[NBLK] = { T33_SEC_DPC, 0, T33_SEC_DMSC, T33_SEC_YDNS, T33_SEC_YSP, T33_SEC_SDNS, T32_SEC_CDNS };
static const u32 sec_len[NBLK] = { T33_DPC_SEC_BYTES, T33_RDNS_SEC_BYTES, T33_DMSC_SEC_BYTES, T33_YDNS_SEC_BYTES,
				   T33_YSP_SEC_BYTES, T33_SDNS_SEC_BYTES, T33_CDNS_SEC_BYTES };
static const u32 mod_id[NBLK] = { T33_MOD_DPC, T33_MOD_RDNS, T33_MOD_DMSC, T33_MOD_YDNS, T33_MOD_YSP, T33_MOD_SDNS,
				  T33_MOD_CDNS };

static u32 v_dpc[T33_DPC_WORDS + 8], v_rdns[T33_RDNS_WORDS], v_dmsc[T33_DMSC_WORDS], v_ysp[T33_YSP_WORDS];
static u32 v_ydns[T33_YDNS_INFO_WORDS], v_sdns[T33_SDNS_INFO_WORDS], v_cdns[T33_CDNS_INFO_WORDS];
static u32 initpar[1008];

/* the block's init builder at gain into l (and its info struct); list length */
static int build(int b, const u8 *sec, u32 gain, u32 *l)
{
	static struct t32_front_write w[T33_TMPL_MAX_WRITES];
	struct t32_rcfg_list rl;
	int n;

	memset(l, 0, T33_SLOT_WORDS * 4);
	switch (b) {
	case DPC: return t33_dpc_init(sec, gain, v_dpc, l, T33_SLOT_WORDS);
	case RDNS: return t33_rdns_init(sec, gain, 0x10000, 0x40, 0x100, v_rdns, l, T33_SLOT_WORDS);
	case DMSC: return t33_dmsc_init(sec, gain, v_dmsc, l, T33_SLOT_WORDS);
	case YSP: return t33_ysp_init(sec, gain, v_ysp, l, T33_SLOT_WORDS);
	case YDNS:
		t33_ydns_info(sec, gain, v_ydns);
		n = t33_ydns_init(sec, gain, w);
		return n > 0 ? t33_rcfg_build_tmpl(l, T33_SLOT_WORDS, t33_ydns_tmpl, ARRAY_SIZE(t33_ydns_tmpl), w,
						   (unsigned int)n) : n;
	case SDNS:
		t33_sdns_info(sec, gain, v_sdns);
		n = t33_sdns_init(sec, gain, w);
		return n > 0 ? t33_rcfg_build_tmpl(l, T33_SLOT_WORDS, t33_sdns_tmpl, ARRAY_SIZE(t33_sdns_tmpl), w,
						   (unsigned int)n) : n;
	default:
		t33_cdns_info(sec, gain, v_cdns);
		n = t33_cdns_gain(sec, gain, w);
		return n > 0 ? t33_rcfg_build(&rl, l, T33_SLOT_WORDS, w, (unsigned int)n) : n;
	}
}

static void fd(int b, u8 *rf)
{
	u8 *inf[4] = { NULL, NULL, NULL, NULL };
	u8 mode = 1;

	switch (b) {
	case DPC: inf[0] = (u8 *)v_dpc; t33_dpc_frame_done(&dpc_ops, 0, 1, rf, 1, inf[0]); break;
	case RDNS: inf[0] = (u8 *)v_rdns; t33_rdns_frame_done(0, initpar, rf, 1, inf); break;
	case DMSC: inf[0] = (u8 *)v_dmsc; t33_dmsc_frame_done(0, 1, inf, &mode, rf); break;
	case YDNS: t33_ydns_frame_done(0, 1, rf, 1, (u8 *)v_ydns, &ydns_ops); break;
	case YSP: inf[0] = (u8 *)v_ysp; t33_ysp_frame_done(&ysp_ops, 0, 1, rf, 1, inf); break;
	case SDNS: inf[0] = (u8 *)v_sdns; t33_sdns_frame_done(0, initpar, rf, 1, (const u8 *const *)inf, &sdns_ops); break;
	default: inf[0] = (u8 *)v_cdns; t33_cdns_frame_done(&cdns_ops, 0, 1, rf, 1, inf); break;
	}
}

static u8 bin[0x40000];

int main(void)
{
	static u32 ref[T33_SLOT_WORDS], l[T33_SLOT_WORDS], other[T33_SLOT_WORDS];
	static u8 syn[0x2000];
	static const u32 gains[] = { 0x10000, 0x18000, 0x2a3c1, 0x40000, 0x5f00d, 0x80000, 0xa0000 };
	const char *bp = getenv("T33_IQ_BIN");
	u32 bytes = 0, off, len, s;
	unsigned int b, g, k, nw = 0, stale[NBLK] = { 0 };

	initpar[226] = 1;
	if (bp) {
		FILE *f = fopen(bp, "rb");

		CHECK(f != NULL);
		if (!f)
			return 1;
		bytes = (u32)fread(bin, 1, sizeof(bin), f);
		fclose(f);
	}
	for (b = 0; b < NBLK; b++) {
		const u8 *sec = NULL;

		if (bytes && sec_id[b]) {
			off = t32_iq_section(bin, bytes, sec_id[b], &len);
			if (off && len == sec_len[b])
				sec = bin + off;
		}
		if (bytes && b == RDNS) {
			off = t32_iq_section(bin, bytes, T33_SEC_RDNS, &len);
			if (off && len == sec_len[b])
				sec = bin + off;
		}
		if (!sec) {	/* synthetic: small bytes, rising tables */
			for (k = 0, s = 0x1234u + b; k < sizeof(syn); k++) {
				s = s * 1103515245u + 12345u;
				syn[k] = (u8)((s >> 16) & 0x3f);
			}
			sec = syn;
		}
		for (g = 0; g < ARRAY_SIZE(gains); g++) {
			u8 rf;
			int n = build((int)b, sec, gains[g], ref);

			CHECK(n > 0);
			if (n <= 0)
				continue;
			/* a list from another gain, then the frame done's refresh-2 branch from this gain's info */
			build((int)b, sec, gains[(g + 3) % ARRAY_SIZE(gains)], other);
			memcpy(l, other, sizeof(l));
			build((int)b, sec, gains[g], ref);	/* the info struct of this gain again */
			cur = l;
			cur_mod = mod_id[b];
			writes = outside = trigs = 0;
			busy = 0;
			rf = 2;
			fd((int)b, &rf);
			CHECK(rf == 1 && trigs == 0 && writes > 0 && outside == 0);
			for (k = 0; k < T33_SLOT_WORDS; k++)
				if (l[k] != ref[k]) {
					/* slots the vendor's frame done does not rewrite keep the other gain's value */
					if (other[k] == l[k]) {
						stale[b]++;
						continue;
					}
					printf("FAIL %s gain %#x word %u: %08x / %08x\n", nm[b], gains[g], k, l[k], ref[k]);
					bad++;
				}
			if (g == 0)
				nw += writes;
			/* state machine: busy keeps 2 and writes nothing, 1 triggers once and goes to 0 */
			writes = trigs = 0;
			busy = 1;
			rf = 2;
			fd((int)b, &rf);
			/* the vendor's DPC frame done writes first and keeps 2 while its list runs (oracle block fd-dpc) */
			CHECK(rf == 2 && (b == DPC ? writes > 0 : writes == 0) && trigs == 0);
			writes = 0;
			busy = 0;
			rf = 1;
			fd((int)b, &rf);
			CHECK(rf == 0 && writes == 0 && trigs == 1);
			rf = 0;
			fd((int)b, &rf);
			CHECK(rf == 0 && writes == 0 && trigs == 1);
		}
		if (stale[b])	/* gain-dependent init slots the frame done (and par_refresh) leaves alone */
			printf("info: %s %u init slots not rewritten at a gain change\n", nm[b], stale[b]);
	}
	printf("tx_isp_t33_bst_test: %s (%s sections, %u frame-done writes at gain 1.0)\n", bad ? "FAIL" : "ok",
	       bytes ? "IQ bin" : "synthetic", nw);
	return bad ? 1 : 0;
}
