// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle, CCM per-frame: our stateful model (struct t32_ccm in
 * driver/t32/tx_isp_t32_ccm.h: t32_ccm_ct_update / t32_ccm_ev_update and the
 * interpolation calls behind them) against the vendor flow tisp_ccm_ct_update
 * (0x8cda8) and tisp_ccm_ev_update (0x8c8fc) -> tisp_ccm_interp_by_ct /
 * _by_ev -> tisp_ccm_calc_para_refresh -> tisp_ccm_write_reg, taken from
 * Matt Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc).
 *
 * Every case starts with the init (both sides, as t32_ccm_oracle), then runs
 * a random sequence of steps on both: ct_update, ev_update, the bare interp
 * calls with force 0/1/2/255, and pokes of the switches the vendor's API and
 * tuning code change at run time (section bytes 0x35f, 0x360..0x363, the
 * fixed-set word, the thresholds at +0x34 and +0x248, the knot count and
 * hysteresis, the API override flags and matrix).  After every step compared:
 * return values, every register write in order, the parameter block, the
 * handed-out matrix, the exposure / colour-temperature cache, the API block
 * and all 872 section bytes.  Inputs aim at the cache edges (threshold +-1,
 * band edges, equal values) and also run section 12 of every *.bin in
 * T32_IQ_DIR.
 *
 * Exit 0 = equal; 1 = mismatch; 2 = strict and no bins.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_iq.h"
#include "tx_isp_t32_ccm.h"

#define SECB	T32_CCM_SEC_BYTES
#define SEC_OFF	0x7e40u

static u32 rs = 0x51c3a7u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static void put32(u8 *p, u32 v)
{
	p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
}

static unsigned int steps, failed, refused, cases, ct_hit, ct_miss, ev_hit, ev_miss, ovr_steps;

/* the two sides of one case */
static struct t32_ccm st;
static u8 osec[SECB];
static u8 *vsec, *vcp, *vmat, *vapi;
static u32 *vct;
static struct t32_front_write ow[T32_CCM_MAX_WRITES];

static u32 get32(const u8 *p)
{
	return p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

/* compare all state; returns nonzero on a difference (and says what) */
static int cmp_state(const char *what, unsigned int on, u32 rv_o, u32 rv_v)
{
	unsigned int i;
	const char *bad = 0;
	int idx = 0;

	if (rv_o != rv_v)
		bad = "return";
	else if (t32o_log.n != on)
		bad = "write count";
	else {
		for (i = 0; i < on; i++)
			if (t32o_log.w[i].reg != ow[i].reg || t32o_log.w[i].val != ow[i].val) {
				bad = "write";
				idx = (int)i;
				break;
			}
	}
	if (!bad && memcmp(vcp, st.cp, sizeof(st.cp)))
		bad = "cp";
	if (!bad)
		for (i = 0; i < 9; i++)
			if (get32(vmat + 4 * i) != st.mat_out[i]) { bad = "mat_out"; idx = (int)i; }
	if (!bad && (vct[0] != st.ev_lo || vct[1] != st.ev_hi || vct[2] != st.ct || vct[3] != st.ct_code))
		bad = "cache";
	if (!bad && (vapi[0] != st.api_on || vapi[1] != st.api_ev))
		bad = "api flags";
	if (!bad)
		for (i = 0; i < 9; i++)
			if (get32(vapi + 4 + 4 * i) != st.api_mat[i]) { bad = "api_mat"; idx = (int)i; }
	if (!bad && memcmp(vsec, osec, SECB)) {
		bad = "section";
		for (i = 0; i < SECB; i++)
			if (vsec[i] != osec[i]) { idx = (int)i; break; }
	}
	if (!bad)
		return 0;
	if (failed < 6) {
		printf("MISMATCH %s: %s [%d] (vendor rv %u writes %u; ours rv %u writes %u)\n", what, bad, idx,
		       rv_v, t32o_log.n, rv_o, on);
		if (!strcmp(bad, "cp"))
			for (i = 0; i < sizeof(st.cp); i++)
				if (vcp[i] != st.cp[i])
					printf("  cp+0x%x vendor %02x ours %02x\n", i, vcp[i], st.cp[i]);
		if (!strcmp(bad, "cache"))
			printf("  vendor %08x %08x %08x %08x ours %08x %08x %08x %08x\n", vct[0], vct[1], vct[2], vct[3],
			       st.ev_lo, st.ev_hi, st.ct, st.ct_code);
	}
	return 1;
}

static void poke(u32 off, u8 v)
{
	osec[off] = v;
	vsec[off] = v;
}

static void poke32(u32 off, u32 v)
{
	put32(osec + off, v);
	put32(vsec + off, v);
}

/* a colour temperature near something interesting */
static u32 pick_ct(void)
{
	const u8 *lut = st.lut;
	int knots = osec[0x364], hyst = osec[0x365];
	u32 base;

	switch (rnd() % 8) {
	case 0: return rnd() % 12000;
	case 1: base = st.ct; break;
	case 2: base = get32(lut + 40 * (rnd() % (u32)knots)); break;
	case 3: base = get32(lut + 40 * (rnd() % (u32)knots)) + (rnd() & 1 ? (u32)hyst : -(u32)hyst); break;
	case 4: return rnd();					/* huge / negative as signed */
	case 5: base = st.ct + T32_CCM_S16(osec, 0x248); break;	/* the cache threshold */
	case 6: base = st.ct - T32_CCM_S16(osec, 0x248); break;
	default: base = 5000; break;
	}
	return rnd() % 5 < 3 ? base : base + (rnd() % 5) - 2;
}

static void pick_ev(u32 *lo, u32 *hi)
{
	u32 thr = get32(osec + 0x34);
	unsigned long long cur = (unsigned long long)st.ev_hi << 32 | st.ev_lo, v;

	switch (rnd() % 9) {
	case 0: *lo = rnd(); *hi = rnd() & 0x3ff; return;
	case 1: *lo = rnd(); *hi = rnd(); return;
	case 2: *lo = 0; *hi = 256 + (rnd() % 3) - 1; return;
	case 3: *lo = rnd() % 3; *hi = 256; return;
	case 4: v = cur + thr + (rnd() % 3) - 1; break;
	case 5: v = cur - thr + (rnd() % 3) - 1; break;
	case 6: v = cur + (rnd() % 4); break;
	case 7: *lo = st.ev_lo; *hi = st.ev_hi; return;
	default: {
		/* on / around an exposure knot threshold (ev = hi << 22 | lo >> 10) */
		u32 t = get32(osec + 4 + 4 * (rnd() % 12)) + (rnd() % 3) - 1;

		*hi = t >> 22;
		*lo = t << 10 | (rnd() & 0x3ff);
		return;
	}
	}
	*lo = (u32)v;
	*hi = (u32)(v >> 32);
}

static void random_poke(void)
{
	u32 r = rnd() % 16, v;
	unsigned int i;

	switch (r) {
	case 0: poke(0x35f, rnd() % 4 ? (u8)(1 + rnd() % 3) : 0); break;
	case 1: poke(0x360, (u8)(rnd() % 4)); break;
	case 2: poke(0x361, (u8)(rnd() % 5)); break;
	case 3: poke(0x362, (u8)(rnd() % 3)); break;
	case 4: poke(0x363, (u8)(rnd() % 3)); break;
	case 5: v = (rnd() & 1) ? 0 : (rnd() & 1) ? 1 : rnd() % 3; poke32(0x38, v); break;
	case 6: poke32(0x34, (rnd() & 1) ? rnd() % 64 : (rnd() & 1) ? 0xffffffffu : rnd() >> (rnd() % 32)); break;
	case 7: v = (rnd() & 1) ? rnd() % 200 : rnd() % 8000;
		poke(0x248, (u8)v); poke(0x249, (u8)(v >> 8)); break;
	case 8: poke(0x365, (u8)(rnd() % 4 ? rnd() % 200 : rnd() % 4)); break;
	case 9: st.api_on = vapi[0] = (u8)(rnd() % 4 ? 1 : (rnd() & 1) ? 0 : 0x80); break;
	case 10: st.api_ev = vapi[1] = (u8)(rnd() % 3 ? 0 : 1 + rnd() % 2); break;
	case 11:
		for (i = 0; i < 9; i++) {
			v = rnd() % 4 ? (u32)((int)(rnd() % 0x4000) - 0x2000) : rnd();
			st.api_mat[i] = v;
			put32(vapi + 4 + 4 * i, v);
		}
		break;
	case 12:	/* fixed matrix coefficients of the section */
		for (i = 0; i < 9; i++)
			poke32(0x3c + 4 * i, rnd() % 3 ? (u32)((int)(rnd() % 0x4000) - 0x2000) : rnd());
		break;
	case 13: {	/* fewer knots than the table holds */
		u32 k = st.lutbytes / 40;

		poke(0x364, (u8)(1 + rnd() % (k > 255 ? 255 : k)));
		break;
	}
	case 14:	/* exposure tables of the section */
		for (i = 0; i < 16; i++) {
			u32 o = (rnd() % 2) ? 0xe0 + rnd() % 0x180 : 0x200 + rnd() % 0x150;

			poke(o, (u8)rnd());
		}
		break;
	default:	/* a handful of random section bytes below the id-free tail */
		for (i = 0; i < 4; i++) {
			u32 o = 4 + rnd() % (SECB - 4);

			if (o != 0x364)
				poke(o, (u8)rnd());
		}
		break;
	}
}

static int one_step(const char *what, unsigned int k)
{
	unsigned int on = 0;
	u32 rv_o = 0, rv_v = 0, lo, hi, ct, force;
	u64 v;
	char tag[96];

	t32o_log.n = 0;
	steps++;
	switch (rnd() % 8) {
	case 0: case 1:
		ct = pick_ct();
		on = (unsigned)t32_ccm_ct_update(&st, ct, ow);
		v = t32o_call((t32o_fn)(void (*)(void))tisp_ccm_ct_update, 0, 0, ct, 0, 0);
		/* the vendor returns the interp code: 0 = recomputed and written */
		rv_o = on == 0;
		rv_v = (u32)v != 0;
		if (on) ct_miss++; else ct_hit++;
		break;
	case 2: case 3:
		pick_ev(&lo, &hi);
		on = (unsigned)t32_ccm_ev_update(&st, lo, hi, ow);
		v = t32o_call((t32o_fn)(void (*)(void))tisp_ccm_ev_update, 0, 0, lo, hi, 0);
		rv_o = on == 0;
		rv_v = (u32)v != 0;
		if (on) ev_miss++; else ev_hit++;
		break;
	case 4:
		ct = pick_ct();
		force = (u32)(rnd() % 4 == 0 ? 0 : rnd() % 4 == 0 ? 1 : (rnd() & 1) ? 2 : 0xff) | (rnd() % 8 == 0 ? 0x100 : 0);
		rv_o = (u32)t32_ccm_ct_interp(&st, ct, force);
		rv_v = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_ccm_interp_by_ct, 0, ct, force, 0, 0);
		break;
	case 5: case 6:
		pick_ev(&lo, &hi);
		force = (u32)(rnd() % 4 == 0 ? 0 : rnd() % 4 == 0 ? 1 : (rnd() & 1) ? 2 : 0xff) | (rnd() % 8 == 0 ? 0x100 : 0);
		rv_o = (u32)t32_ccm_ev_interp(&st, lo, hi, force);
		rv_v = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_ccm_interp_by_ev, 0, 0, lo, hi, force);
		break;
	case 7:
		if (rnd() % 3) {
			random_poke();
			break;
		}
		/* the bare register writer in every mode (also the ones the updates do not use) */
		force = rnd() % 6;
		if (rnd() % 8 == 0)
			force |= 0x100;
		on = t32_ccm_write(&st, force, ow);
		t32o_call((t32o_fn)(void (*)(void))tisp_ccm_write_reg, 0, force, 0, 0, 0);
		break;
	default:
		random_poke();
		break;
	}
	if (!t32_ccm_tuned(&st))
		ovr_steps++;
	snprintf(tag, sizeof(tag), "%s step %u", what, k);
	return cmp_state(tag, on, rv_o, rv_v);
}

/* the sequence: init on both sides, then random steps */
static int run_case(const char *what, const u8 *sec, const u8 *lut, u32 lutbytes, unsigned int nsteps)
{
	u8 *base, *vlut, *info, *vst;
	unsigned int on, k;

	memcpy(osec, sec, SECB);
	on = (unsigned)t32_ccm_start(&st, osec, lut, lutbytes, ow);
	if ((int)on < 0) {
		refused++;
		return 0;
	}
	cases++;
	t32o_reset();
	base = t32o_alloc(SEC_OFF + SECB);
	vlut = t32o_alloc(lutbytes ? lutbytes : 4);
	info = t32o_alloc(64);
	memcpy(base + SEC_OFF, sec, SECB);
	memcpy(vlut, lut, lutbytes);
	put32(info + 24, T32O_PTR(vlut));
	t32_data_dd0a4[0] = T32O_PTR(base);
	t32_data_dc274[0] = T32O_PTR(info);
	t32o_call((t32o_fn)(void (*)(void))tisp_ccm_init, 0, 0, 0, 0, 0);
	vst = T32O_HOST(t32_data_e28f0[0]);
	vsec = T32O_HOST(get32(vst));
	vct = T32O_HOST(get32(vst + 4));
	vcp = T32O_HOST(get32(vst + 8));
	vmat = T32O_HOST(get32(vst + 12));
	vapi = T32O_HOST(get32(vst + 16));
	if (cmp_state(what, on, 0, 0))
		goto bad;
	for (k = 0; k < nsteps; k++)
		if (one_step(what, k))
			goto bad;
	return 0;
bad:
	failed++;
	return 1;
}

static int coef(void)
{
	return rnd() % 4 ? (int)(rnd() % 0x4000) - 0x2000 : (int)(rnd() % 0x10000) - 0x8000;
}

static void random_cases(unsigned int n)
{
	static u8 sec[SECB], lut[255 * 40];
	unsigned int c, i, k;

	for (c = 0; c < n; c++) {
		int knots, hyst, ct;

		for (i = 0; i < SECB; i++)
			sec[i] = (u8)rnd();
		put32(sec, T32_CCM_ID);
		sec[0x35f] = rnd() % 8 ? (u8)(1 + rnd() % 255) : 0;
		put32(sec + 0x38, rnd() % 8 ? rnd() % 4 + 2 : rnd() % 2);
		put32(sec + 0x34, rnd() % 4 == 0 ? 0 : rnd() % 4 == 1 ? 0xffffffffu : rnd() >> (rnd() % 32));
		sec[0x248] = (u8)rnd();
		sec[0x249] = (u8)(rnd() % 4 ? rnd() % 4 : rnd());
		/* ascending exposure thresholds, leading zeros sometimes */
		{
			u32 t = rnd() % 3 ? 0 : rnd() % 2000;

			for (i = 0; i < 12; i++) {
				if (rnd() % 5 == 0 && i > 0)
					t = t;	/* equal neighbours */
				else
					t += rnd() % 5 == 0 ? rnd() % 100000 : rnd() % 3000;
				put32(sec + 4 + 4 * i, t);
			}
		}
		knots = c % 20 == 7 ? (int)(rnd() % 255) + 1 : (int)(rnd() % 10) + 1;
		hyst = rnd() % 4 ? (int)(rnd() % 400) & 0xff : (int)(rnd() % 8);
		sec[0x364] = (u8)knots;
		sec[0x365] = (u8)hyst;
		ct = 1500 + (int)(rnd() % 3000);
		for (k = 0; k < (unsigned int)knots; k++) {
			put32(lut + 40 * k, (u32)ct);
			for (i = 0; i < 9; i++)
				put32(lut + 40 * k + 4 + 4 * i, (u32)coef());
			ct += (int)(rnd() % 2500) + (rnd() % 6 == 0 ? 0 : 1);
		}
		run_case("random", sec, lut, (u32)knots * 40U, 60);
	}
}

static int bin_cases(const char *dir)
{
	DIR *d = opendir(dir);
	struct dirent *e;
	int nbins = 0;

	if (!d)
		return 0;
	while ((e = readdir(d))) {
		char path[1024];
		static u8 bin[1u << 20];
		FILE *f;
		size_t bytes, l = strlen(e->d_name);
		u32 off, len = 0;
		int rep;

		if (l < 4 || strcmp(e->d_name + l - 4, ".bin"))
			continue;
		snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
		f = fopen(path, "rb");
		if (!f)
			continue;
		bytes = fread(bin, 1, sizeof(bin), f);
		fclose(f);
		off = t32_iq_section(bin, (u32)bytes, T32_SEC_CCM, &len);
		if (!off || len < SECB)
			continue;
		nbins++;
		for (rep = 0; rep < 12; rep++)
			run_case(e->d_name, bin + off, bin + off + len, (u32)bytes - off - len, 80);
	}
	closedir(d);
	return nbins;
}

int main(void)
{
	const char *dir = getenv("T32_IQ_DIR");
	int strict = getenv("T40_ORACLE_STRICT") && atoi(getenv("T40_ORACLE_STRICT"));
	unsigned int rc_r, rs_r;
	int nbins;

	random_cases(getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 2000);
	rc_r = cases;
	rs_r = steps;
	nbins = bin_cases(dir ? dir : "../external/ingenic-sdk/sensor-iq/t32");
	printf("t32_ccmf_oracle: random %u sequences (%u steps, %u refused by ours), IQ bins %d (%u sequences, %u steps); ct updates %u hit/%u run, ev updates %u hit/%u run, %u steps with the API override; %u mismatches\n",
	       rc_r, rs_r, refused, nbins, cases - rc_r, steps - rs_r, ct_hit, ct_miss, ev_hit, ev_miss, ovr_steps, failed);
	if (failed)
		return 1;
	if (!nbins) {
		printf("t32_ccmf_oracle: IQ-bin part: SKIP (set T32_IQ_DIR to a directory of T32 *.bin)\n");
		return strict ? 2 : 0;
	}
	return 0;
}
