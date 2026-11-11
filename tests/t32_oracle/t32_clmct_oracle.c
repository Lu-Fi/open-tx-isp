// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: driver/t32/tx_isp_t32_clmct.h (t32_clm_ct_update_st /
 * t32_clm_ct_interp = tisp_clm_ct_update / tisp_clm_ct_interp on the kept
 * CLM block state) against the vendor functions (Matt Davis' recovered C,
 * PR #15 @ 2e2e06bc), d03a0 = 0 (direct mode).
 *
 * Per case: section 20 (1..5 zones, hysteresis 0..40), zone colour
 * temperatures rising (some equal, some negative as s16), random tables and
 * byte tables (first byte of the byte tables near the zone temperatures,
 * as the vendor's blend check reads it), a state with random stale tables, a
 * random state colour temperature, zone code (also 0xff, also equal to the
 * one of the new temperature) and threshold (mostly 1, as the init leaves it);
 * then a sequence of 1..6 updates with temperatures around the zones (equal,
 * +-1, inside / outside the hysteresis, below / above all zones).  Compared
 * after every update: the return value of the interp (direct call on a copy),
 * every byte of the vendor state (ct, tables, threshold, zone code) against
 * ours, every register write (addr / value / order).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_front.h"
#include "tx_isp_t32_clmct.h"

#define STB	3264u
extern u32 t32_data_e28d0[4], t32_data_dc274[4];
u64 tisp_clm_ct_update(u32, u32, u32, u32), tisp_clm_ct_interp(u32, u32, u32, u32);

static u32 rs = 0x51c3a9e5u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static u8 sec[64], lut[5 * (T32_CLM_ZONE_BYTES + T32_CLM_TABLE)], st0[STB];
static unsigned int failed, compared, nw, nrc[3];

static u32 o_ld32(const u8 *p, u32 o) { return p[o] | (u32)p[o + 1] << 8 | (u32)p[o + 2] << 16 | (u32)p[o + 3] << 24; }
static u32 o_ld16(const u8 *p, u32 o) { return p[o] | (u32)p[o + 1] << 8; }
static void o_wr32(u8 *p, u32 o, u32 v) { p[o] = (u8)v; p[o + 1] = (u8)(v >> 8); p[o + 2] = (u8)(v >> 16); p[o + 3] = (u8)(v >> 24); }
static void s16w(u8 *p, int v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }

static u32 pick_ct(unsigned int zones)
{
	unsigned int z = rnd() % zones, m = rnd() % 8;
	int c = (short)(lut[z * T32_CLM_ZONE_BYTES] | lut[z * T32_CLM_ZONE_BYTES + 1] << 8);

	switch (m) {
	case 0: return (u32)c;
	case 1: return (u32)(c + 1);
	case 2: return (u32)(c - 1);
	case 3: return (u32)(c + (int)sec[0x24]);
	case 4: return (u32)(c - (int)sec[0x24] + (int)(rnd() % 3) - 1);
	case 5: return rnd() % 12000;
	case 6: return rnd();
	}
	return (u32)(c + (int)(rnd() % 600) - 300);
}

static int one(unsigned int c, unsigned int zones, unsigned int nup)
{
	struct t32_front_write w[1024];
	struct t32_clm_ctx o;
	u8 *vst = t32o_alloc(STB), *vsec = t32o_alloc(64), *vlut = t32o_alloc(sizeof(lut)), *info = t32o_alloc(64);
	u8 *tst = t32o_alloc(STB);
	u32 p1 = T32O_PTR(vlut), p2 = T32O_PTR(vlut + zones * T32_CLM_ZONE_BYTES), k, thr;
	unsigned int u, i;

	memcpy(vsec, sec, 64);
	memcpy(vlut, lut, sizeof(lut));
	memcpy(vst, st0, STB);
	o_wr32(vst, 0, T32O_PTR(vsec));
	memset(info, 0, 64);
	memcpy(info + 36, &p1, 4);
	memcpy(info + 40, &p2, 4);
	t32_data_e28d0[0] = T32O_PTR(vst);
	t32_data_dc274[0] = T32O_PTR(info);
	t32_data_d03a0[0] = 0;

	memset(&o, 0, sizeof(o));
	o.sec = sec;
	o.lut = lut;
	o.lutbytes = zones * (T32_CLM_ZONE_BYTES + T32_CLM_TABLE);
	o.ct = o_ld32(st0, 96);
	o.zone = st0[3260];
	memcpy(o.tab, st0 + 100, sizeof(o.tab));
	memcpy(o.tb, st0 + 2202, sizeof(o.tb));
	thr = o_ld16(st0, 2200);
	for (u = 0; u < nup; u++) {
		u32 ct = pick_ct(zones), force = (rnd() % 8 == 0) ? 1 + (rnd() & 0x100) : (rnd() & 0x100);
		struct t32_clm_ctx oc = o;
		int vrc, orc, on;

		/* the interp alone (return value, forced or not) on a copy of the vendor state */
		memcpy(tst, vst, STB);
		t32_data_e28d0[0] = T32O_PTR(tst);
		vrc = (int)(u32)t32o_call((t32o_fn)(void (*)(void))tisp_clm_ct_interp, 0, ct, force, 0, 0);
		orc = t32_clm_ct_interp(&oc, thr, ct, force);
		nrc[orc + 1]++;
		if (vrc != orc) {
			if (failed++ < 8)
				printf("t32_clmct_oracle: FAIL case %u/%u: interp ct %u force %x vendor %d ours %d\n", c, u, ct, force, vrc, orc);
			return 1;
		}
		/* the update on the real state */
		t32_data_e28d0[0] = T32O_PTR(vst);
		t32o_log.n = 0;
		t32o_call((t32o_fn)(void (*)(void))tisp_clm_ct_update, 0, (u32)rnd(), ct, 0, 0);
		on = t32_clm_ct_update_st(&o, thr, ct, w);
		compared++;
		if (o_ld32(vst, 96) != o.ct || vst[3260] != o.zone || memcmp(vst + 100, o.tab, sizeof(o.tab)) ||
		    memcmp(vst + 2202, o.tb, sizeof(o.tb)) || o_ld16(vst, 2200) != thr || memcmp(vsec, sec, 64)) {
			if (failed++ < 8)
				printf("t32_clmct_oracle: FAIL case %u/%u: state (ct %u: vendor %u ours %u, zone %u / %u)\n", c, u,
				       ct, o_ld32(vst, 96), o.ct, vst[3260], o.zone);
			return 1;
		}
		if (t32o_log.n != (unsigned int)on) {
			if (failed++ < 8)
				printf("t32_clmct_oracle: FAIL case %u/%u: %u vendor writes, %d ours\n", c, u, t32o_log.n, on);
			return 1;
		}
		for (i = 0; i < t32o_log.n; i++)
			if (!t32o_reg_eq(t32o_log.w[i].reg, w[i].reg) || t32o_log.w[i].val != w[i].val) {
				if (failed++ < 8)
					printf("t32_clmct_oracle: FAIL case %u/%u: write %u\n", c, u, i);
				return 1;
			}
		nw += (unsigned int)on;
		(void)k;
	}
	return 0;
}

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 3000, c, z, i;

	for (c = 0; c < n; c++) {
		unsigned int zones = 1 + rnd() % 5;
		int ct = (rnd() % 8 == 0) ? -(int)(rnd() % 3000) : (int)(1000 + rnd() % 3000);

		t32o_reset();
		for (i = 0; i < 64; i++)
			sec[i] = (u8)rnd();
		sec[0x23] = (u8)zones;
		sec[0x24] = (u8)(rnd() % 41);
		for (i = 0; i < sizeof(lut); i++)
			lut[i] = (u8)rnd();
		for (z = 0; z < zones; z++) {
			s16w(lut + z * T32_CLM_ZONE_BYTES, ct);
			/* the first byte-table byte is compared with ct by the blend check: near the temperatures */
			lut[zones * T32_CLM_ZONE_BYTES + z * T32_CLM_TABLE] = (u8)(rnd() % 3 ? (u32)(ct + (int)(rnd() % 64) - 32) : rnd());
			if (rnd() % 6)
				ct += 1 + (int)(rnd() % 2500);
		}
		for (i = 0; i < STB; i++)
			st0[i] = (u8)rnd();
		o_wr32(st0, 96, rnd() % 4 ? pick_ct(zones) : rnd());
		st0[3260] = rnd() % 4 == 0 ? T32_CLMCT_ZONE_INIT : rnd() % 3 == 0 ? (u8)(rnd() % 12) : t32_clm_zone_code(sec, lut, o_ld32(st0, 96));
		st0[2200] = rnd() % 4 ? T32_CLMCT_THR : (u8)rnd();
		st0[2201] = rnd() % 8 ? 0 : (u8)rnd();
		if (one(c, zones, 1 + rnd() % 6) && failed > 20)
			break;
	}
	printf("t32_clmct_oracle: %u updates, return 1 / 0 / -1: %u / %u / %u, %u writes, %u mismatches\n",
	       compared, nrc[2], nrc[1], nrc[0], nw, failed);
	return failed ? 1 : 0;
}
