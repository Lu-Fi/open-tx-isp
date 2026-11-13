// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: CLM per-frame paths.  Runs the unmodified vendor functions
 * (Matt Davis' recovered C, PR #15 @ 2e2e06bc) tisp_clm_dn_params_refresh and
 * tisp_clm_wake_up against our readable t32_clm_dn_params_refresh /
 * t32_clm_wake_up (driver/t32/tx_isp_t32_clm.h) and compares every register
 * write (address, value, order) and the section pointer of the block state.
 *
 * Section 20 sits at tuning base + 0xd608; its payload is reached through the
 * pointer the block state caches (0x60 into the vendor struct).  To be
 * independent of that pointer's exact reading, the payload is placed both
 * behind state+0x60 and directly after the section, and every zone carries the
 * same 1050 words and the same 1050 bytes with the colour temperature 0, the
 * hysteresis 0 and the first byte-table byte 0.  Then each path of
 * tisp_clm_ct_interp (verbatim zone, blend of equal tables) yields the same
 * tables for any colour temperature; the zone selection and the blend range
 * are proven by t32_clm_oracle instead.  The CSC section 9 (mode byte 0x5b) is
 * run through the vendor's tisp_csc_init first, as tisp_csc_api_get in the
 * framing path reads that state.
 *
 * Inputs: >= 3000 fixed-seed random sections (2..5 zones, random coefficients,
 * ratios around the tisp_clm_calc_para_refresh buckets, random tuning bytes)
 * for the CSC modes 0..5.  Cases ours refuses (-1) are counted, not compared.
 *
 * Exit 0 = all compared cases equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_front.h"
#include "tx_isp_t32_clm.h"

#define SECB		T32_CLM_SEC_BYTES
#define SEC_OFF		0xd608u
#define CSC_OFF		27488u
#define ZONE		T32_CLM_ZONE_BYTES
#define LUT_MAX		(5 * (ZONE + T32_CLM_TABLE) + 64)
#define MAXW		2048

/*
 * The vendor's per-channel block state pointers.  env.c provides the object
 * where it defines it; this weak copy only keeps the link intact otherwise.
 */
u32 t32_data_e28d0[4] __attribute__((weak));

extern u64 tisp_clm_dn_params_refresh(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_clm_wake_up(u32 a0, u32 a1, u32 a2, u32 a3);

static u32 rs = 0x1f4a27u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static void put16(u8 *p, int v)
{
	p[0] = (u8)v; p[1] = (u8)(v >> 8);
}

static void put32(u8 *p, u32 v)
{
	p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
}

static u32 get32(const u8 *p)
{
	return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static unsigned int compared, refused, failed;
static struct t32_clm_ctx our_ctx;
static u8 our_base[0x20000];
static u8 pl[LUT_MAX];

static int run_case(const char *what, int wake, const u8 *sec, const u8 *lut, u32 lutbytes,
		    const u8 *csc)
{
	static struct t32_front_write rw[MAXW], ow[MAXW];
	u8 *base, *vlut, *info, *ctx_ref;
	unsigned int mode = csc[0x5b], zones = sec[0x23], i, rn;
	u32 roff, ooff;
	const int *m1;
	int on;

	if (mode > 5 || zones < 1 || lutbytes > sizeof(pl))
		return 0;
	m1 = t32_csc_modes[mode].m;

	/* vendor side */
	t32o_reset();
	base = t32o_alloc(0x20000);
	vlut = t32o_alloc(lutbytes + 64);
	info = t32o_alloc(64);
	if (!base || !vlut || !info)
		return 2;
	ctx_ref = t32o_alloc(128);
	if (!ctx_ref)
		return 2;
	memcpy(base + SEC_OFF, sec, SECB);
	memcpy(base + SEC_OFF + SECB, lut, lutbytes);	/* payload behind the section */
	memcpy(base + CSC_OFF, csc, 104);
	memcpy(vlut, lut, lutbytes);			/* payload behind state + 0x60 */
	memset(info, 0, 64);
	put32(info + 36, T32O_PTR(vlut));			/* 2 x 420 word tables */
	put32(info + 40, T32O_PTR(vlut) + zones * ZONE);	/* byte tables */
	memset(ctx_ref, 0, 128);
	put32(ctx_ref + 96, T32O_PTR(vlut));			/* section payload */
	t32_data_dd0a4[0] = T32O_PTR(base);
	t32_data_e28d0[0] = T32O_PTR(ctx_ref);
	t32_data_dc274[0] = T32O_PTR(info);
	t32_data_d03a0[0] = 0;				/* direct LUT port path */
	t32o_call((t32o_fn)(void (*)(void))tisp_csc_init, 0, 0, 0, 0, 0);
	t32o_log.n = 0;
	/* the framing flow: the per-frame parameter refresh, then the wake-up rewrite */
	t32o_call((t32o_fn)(void (*)(void))tisp_clm_dn_params_refresh, 0, 0, 0, 0, 0);
	if (wake) {
		t32o_log.n = 0;
		t32o_call((t32o_fn)(void (*)(void))tisp_clm_wake_up, 0, 0, 0, 0, 0);
	}
	rn = t32o_log.n;
	if (rn > MAXW)
		return 2;
	memcpy(rw, t32o_log.w, rn * sizeof(rw[0]));
	roff = get32(ctx_ref) - T32O_PTR(base);

	/* our side: the same section and payload */
	memcpy(pl, lut, lutbytes);
	memcpy(our_base + SEC_OFF, sec, SECB);
	memset(&our_ctx, 0, sizeof(our_ctx));
	our_ctx.lut = pl;
	our_ctx.lutbytes = lutbytes;
	our_ctx.mode = mode;
	if (t32_clm_dn_params_refresh(&our_ctx, 0, our_base, mode, m1, ow) < 0) {
		refused++;
		return 0;
	}
	on = wake ? t32_clm_wake_up(&our_ctx, 0, m1, ow)
		  : (int)t32_clm_dn_params_refresh(&our_ctx, 0, our_base, mode, m1, ow);
	ooff = (u32)(our_ctx.sec - our_base);
	compared++;
	if (on < 0 || (unsigned int)on != rn || roff != ooff)
		goto bad;
	for (i = 0; i < rn; i++)
		if (rw[i].reg != ow[i].reg || rw[i].val != ow[i].val)
			goto bad;
	return 0;
bad:
	failed++;
	if (failed < 5) {
		printf("MISMATCH %s: mode %u zones %u writes vendor %u ours %d, sec off vendor %05x ours %05x\n",
		       what, mode, zones, rn, on, roff, ooff);
		for (i = 0; i < rn || (int)i < on; i++)
			if (i >= rn || (int)i >= on || rw[i].reg != ow[i].reg ||
			    rw[i].val != ow[i].val) {
				printf("  #%u vendor %05x=%08x ours %05x=%08x\n", i,
				       i < rn ? rw[i].reg : 0, i < rn ? rw[i].val : 0,
				       (int)i < on ? ow[i].reg : 0, (int)i < on ? ow[i].val : 0);
				if (i > 8)
					break;
			}
	}
	return 1;
}

static void gen(u8 *sec, u8 *lut, u32 *lutbytes, u8 *csc)
{
	static const int edge[] = { 0, 1, 2, 3, 4, 5, 8, 9, 16, 17, 32, 33, 64, 65,
				    128, 129, 256, 257, 512, 513, 1024, 1025 };
	static u8 zone[ZONE + T32_CLM_TABLE];
	unsigned int zones = 2 + rnd() % 4, i, k;

	memset(sec, 0, SECB);
	for (i = 0; i < SECB; i++)
		sec[i] = (u8)rnd();
	memcpy(sec, "\x11\xda\x34\x01", 4);
	sec[0x23] = (u8)zones;
	if (rnd() % 2) {	/* saturation ranges around the ratio buckets */
		int lo1 = (int)(rnd() % 500), lo2 = (int)(rnd() % 500);

		put16(sec + 0x1a, lo1);
		put16(sec + 0x1c, lo1 + edge[rnd() % 22] + (int)(rnd() % 3 ? 0 : rnd() % 700));
		put16(sec + 0x1e, lo2);
		put16(sec + 0x20, lo2 + edge[rnd() % 22] + (int)(rnd() % 3 ? 0 : rnd() % 700));
	}
	sec[0x24] = 0;			/* no hysteresis */
	/* one zone template: colour temperature 0, random tables, byte[0] = 0 */
	memset(zone, 0, sizeof(zone));
	for (i = 0; i < T32_CLM_TABLE; i++)
		put16(zone + 2 + 2 * i, (rnd() % 4 == 0) ? (int)(rnd() % 65536) - 32768
							 : (int)(rnd() % 4096) - 2048);
	for (i = 1; i < T32_CLM_TABLE; i++)
		zone[ZONE + i] = (u8)(1 + rnd() % 255);
	for (k = 0; k < zones; k++)
		memcpy(lut + k * ZONE, zone, ZONE);
	for (k = 0; k < zones; k++)
		memcpy(lut + zones * ZONE + k * T32_CLM_TABLE, zone + ZONE, T32_CLM_TABLE);
	*lutbytes = zones * (ZONE + T32_CLM_TABLE);
	for (i = 0; i < 104; i++)
		csc[i] = (u8)rnd();
	csc[0x5b] = (u8)(rnd() % 6);
}

int main(void)
{
	const unsigned int n = getenv("T32O_NRAND") ?
		(unsigned int)atoi(getenv("T32O_NRAND")) : 3000;
	unsigned int c;

	for (c = 0; c < n; c++) {
		static u8 sec[SECB], lut[LUT_MAX], csc[104];
		u32 lutbytes;

		gen(sec, lut, &lutbytes, csc);
		run_case("dn-random", 0, sec, lut, lutbytes, csc);
		run_case("wake-random", 1, sec, lut, lutbytes, csc);
	}
	printf("t32_clmf_oracle: random %u compared (%u refused by ours), %u mismatches\n",
	       compared, refused, failed);
	if (failed)
		return 1;
	return 0;
}
