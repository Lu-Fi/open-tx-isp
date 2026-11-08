// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle, gamma per-frame: our stateful model (struct t32_gamma in
 * driver/t32/tx_isp_t32_gamma.h: t32_gamma_ev_update, t32_gamma_frame_done and
 * the interpolation / curve / transform steps behind them) against the vendor
 * tisp_gamma_ev_update (0x4b38c) -> tisp_gamma_interp_by_ev, _update_curve,
 * _strength_transform, _prepare_write_lut and tisp_gamma_irq_frame_done, taken
 * from Matt Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc).
 *
 * Both the single curve form and the multi-curve form (block id from
 * 0x134ff91, records of 266 bytes) run.  Every case starts with the init on
 * both sides, then a random sequence of ev updates, frame-done calls, the bare
 * interp / update_curve / transform calls with force 0/1/2/255 and pokes of
 * the switches the vendor's API changes (section bytes 0x48..0x59, knots,
 * strengths, record count, the API curve block).  After every step compared:
 * return value, every register write in order, the five state blocks, the
 * exposure cache, the pending byte, all 92 section bytes and the payload.
 * Inputs aim at the knot and record range edges and also run section 13 of
 * every *.bin in T32_IQ_DIR.
 *
 * Exit 0 = equal; 1 = mismatch; 2 = strict and no bins.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_iq.h"
#include "tx_isp_t32_gamma.h"

#define SECB	T32_GAMMA_SEC_BYTES
#define SEC_OFF	0x81a8u

static u32 rs = 0x2b97e3u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static void put32(u8 *p, u32 v)
{
	p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
}

static u32 get32(const u8 *p)
{
	return p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static unsigned int steps, failed, refused, cases, hit, ran, multi_steps, api_steps;

static struct t32_gamma st;
static u8 osec[SECB], opl[T32_GAMMA_REC_BYTES * 16 + 8];
static u8 *vsec, *vpl, *vgs, *vb12, *vcin, *vb20, *vst;
static u32 *vev;
static u32 plbytes;
static struct t32_front_write ow[T32_GAMMA_MAX_WRITES];

static int cmp_state(const char *what, unsigned int on, int rv_o, int rv_v)
{
	const char *bad = 0;
	unsigned int i;

	if (rv_o != rv_v)
		bad = "return";
	else if (t32o_log.n != on)
		bad = "write count";
	else
		for (i = 0; i < on; i++)
			if (t32o_log.w[i].reg != ow[i].reg || t32o_log.w[i].val != ow[i].val) {
				bad = "write";
				break;
			}
	if (!bad && memcmp(vgs, st.gs, sizeof(st.gs)))
		bad = "gs block";
	if (!bad && (vev[0] != st.ev_lo || vev[1] != st.ev_hi))
		bad = "exposure cache";
	if (!bad && memcmp(vb12, st.b12, sizeof(st.b12)))
		bad = "block 12";
	if (!bad && memcmp(vcin, st.cin, sizeof(st.cin)))
		bad = "curve in";
	if (!bad && memcmp(vb20, st.b20, sizeof(st.b20)))
		bad = "block 20";
	if (!bad && vst[24] != st.pending)
		bad = "pending";
	if (!bad && memcmp(vsec, osec, SECB))
		bad = "section";
	if (!bad && memcmp(vpl, opl, plbytes))
		bad = "payload";
	if (!bad)
		return 0;
	if (failed < 6) {
		printf("MISMATCH %s: %s (vendor rv %d writes %u; ours rv %d writes %u)\n", what, bad, rv_v, t32o_log.n,
		       rv_o, on);
		if (!strcmp(bad, "block 20"))
			for (i = 0; i < sizeof(st.b20); i++)
				if (vb20[i] != st.b20[i]) {
					printf("  b20+%u vendor %02x ours %02x\n", i, vb20[i], st.b20[i]);
					if (i > 8)
						break;
				}
		if (!strcmp(bad, "gs block"))
			for (i = 0; i < sizeof(st.gs); i++)
				if (vgs[i] != st.gs[i]) {
					printf("  gs+%u vendor %02x ours %02x\n", i, vgs[i], st.gs[i]);
					if (i > 8)
						break;
				}
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

/* an exposure (lo, hi) near something interesting */
static void pick_ev(u32 *lo, u32 *hi)
{
	unsigned long long cur = (unsigned long long)st.ev_hi << 32 | st.ev_lo, v;
	u32 t;

	switch (rnd() % 9) {
	case 0: *lo = rnd(); *hi = rnd() & 0x3ff; return;
	case 1: *lo = rnd(); *hi = rnd(); return;
	case 2: *lo = 0; *hi = 256 + (rnd() % 3) - 1; return;
	case 3: *lo = rnd() % 3; *hi = 256; return;
	case 4: *lo = st.ev_lo; *hi = st.ev_hi; return;		/* same exposure: cache hit */
	case 5: v = cur + (rnd() % 4); break;
	case 6:		/* on / around a strength knot (ev = hi << 22 | lo >> 10) */
		t = get32(osec + 0x18 + 4 * (rnd() % 12)) + (rnd() % 3) - 1;
		*hi = t >> 22;
		*lo = t << 10 | (rnd() & 0x3ff);
		return;
	case 7:		/* on / around a record range edge */
		if (get32(osec) >= T32_GAMMA_MULTI_ID && osec[0x5a]) {
			const u8 *rec = opl + T32_GAMMA_REC_BYTES * (rnd() % osec[0x5a]);
			t = (t32_tp_le16(rec + (rnd() & 1 ? 260 : 264)) << 16 | t32_tp_le16(rec + (rnd() & 1 ? 258 : 262))) +
			    (rnd() % 3) - 1;
			*hi = t >> 22;
			*lo = t << 10 | (rnd() & 0x3ff);
			return;
		}
		/* fall through */
	default: *lo = rnd() << (rnd() % 20); *hi = rnd() % 5; return;
	}
	*lo = (u32)v;
	*hi = (u32)(v >> 32);
}

static void random_poke(void)
{
	unsigned int i;
	u32 r = rnd() % 14;

	switch (r) {
	case 0: poke(T32_GAMMA_API_A, (u8)(rnd() % 3 ? 1 : rnd() % 4 ? rnd() % 3 : 0x81)); break;
	case 1: poke(T32_GAMMA_API_B, (u8)(rnd() % 3 ? 0 : rnd() % 4)); break;
	case 2: poke(T32_GAMMA_COPY_OUT, (u8)(rnd() % 3)); break;
	case 3: poke(T32_GAMMA_SAVE_PAYLOAD, (u8)(rnd() % 3)); break;
	case 4: poke(0x48, (u8)(rnd() % 4)); break;
	case 5: poke(0x49, (u8)rnd()); break;
	case 6:
		for (i = 0; i < 12; i++)
			poke(0x4a + i, (u8)(rnd() % 3 ? rnd() % 256 : 0xff));
		break;
	case 7: {	/* ascending knots again, sometimes equal neighbours */
		u32 x = rnd() % 4 ? 0 : rnd() % 3000;

		for (i = 0; i < 12; i++) {
			if (rnd() % 6)
				x += rnd() % 4000;
			poke32(0x18 + 4 * i, x);
		}
		break;
	}
	case 8:
		poke32(0x18 + 4 * (rnd() % 12), rnd() % 3 ? rnd() % 100000 : rnd());
		break;
	case 9:
		if (get32(osec) >= T32_GAMMA_MULTI_ID)
			poke(0x5a, (u8)(1 + rnd() % (plbytes / T32_GAMMA_REC_BYTES)));
		break;
	case 10:	/* the API curve block (both sides) */
		for (i = 0; i < 16; i++) {
			u32 o = rnd() % sizeof(st.b20);
			u8 v = (u8)rnd();

			st.b20[o] = v;
			vb20[o] = v;
		}
		break;
	case 11:	/* the payload curve data */
		for (i = 0; i < 16; i++) {
			u32 o = rnd() % plbytes;
			u8 v = (u8)rnd();

			opl[o] = v;
			vpl[o] = v;
		}
		break;
	default:
		for (i = 0; i < 4; i++) {
			u32 o = 4 + rnd() % (SECB - 4);

			if (o != 0x5a && o != T32_GAMMA_API_A && o != T32_GAMMA_API_B)
				poke(o, (u8)rnd());
		}
		break;
	}
}

static u32 pick_force(void)
{
	return (u32)(rnd() % 4 == 0 ? 0 : rnd() % 4 == 0 ? 1 : (rnd() & 1) ? 2 : 0xff) | (rnd() % 8 == 0 ? 0x100 : 0);
}

static int one_step(const char *what, unsigned int k)
{
	unsigned int on = 0;
	int rv_o = 0, rv_v = 0;
	u32 lo, hi, force;
	char tag[96];

	t32o_log.n = 0;
	steps++;
	switch (rnd() % 10) {
	case 0: case 1: case 2:
		pick_ev(&lo, &hi);
		rv_o = t32_gamma_ev_update(&st, lo, hi);
		rv_v = (int)(u32)t32o_call((t32o_fn)(void (*)(void))tisp_gamma_ev_update, 0, 0, lo, hi, 0);
		if (rv_o) hit++; else ran++;
		break;
	case 3: case 4:
		on = (unsigned)t32_gamma_frame_done(&st, ow);
		t32o_call((t32o_fn)(void (*)(void))tisp_gamma_irq_frame_done, 0, 0, 0, 0, 0);
		break;
	case 5:
		pick_ev(&lo, &hi);
		force = pick_force();
		rv_o = (int)t32_gamma_interp_ev(&st, lo, hi, force);
		rv_v = (int)(u32)t32o_call((t32o_fn)(void (*)(void))tisp_gamma_interp_by_ev, 0, 0, lo, hi, force);
		break;
	case 6:
		pick_ev(&lo, &hi);
		rv_o = (int)t32_gamma_update_curve(&st, lo, hi);
		rv_v = (int)(u32)t32o_call((t32o_fn)(void (*)(void))tisp_gamma_update_curve, 0, 0, lo, hi, 0);
		break;
	case 7:
		t32_gamma_transform(&st);
		t32o_call((t32o_fn)(void (*)(void))tisp_gamma_strength_transform, 0, 0, 0, 0, 0);
		break;
	default:
		random_poke();
		break;
	}
	if (get32(osec) >= T32_GAMMA_MULTI_ID)
		multi_steps++;
	if (!t32_gamma_tuned(osec))
		api_steps++;
	snprintf(tag, sizeof(tag), "%s step %u", what, k);
	return cmp_state(tag, on, rv_o, rv_v);
}

static int run_case(const char *what, const u8 *sec, const u8 *lut, u32 lutbytes, unsigned int nsteps)
{
	u8 *base, *info;
	unsigned int on, k;

	if (lutbytes > sizeof(opl) - 8)
		lutbytes = sizeof(opl) - 8;
	memcpy(osec, sec, SECB);
	memcpy(opl, lut, lutbytes);
	plbytes = lutbytes;
	on = (unsigned)t32_gamma_start(&st, osec, opl, lutbytes, ow);
	if ((int)on < 0) {
		refused++;
		return 0;
	}
	cases++;
	t32o_reset();
	base = t32o_alloc(SEC_OFF + SECB + 64);
	info = t32o_alloc(64);
	vpl = t32o_alloc(lutbytes + 8);
	memcpy(base + SEC_OFF, sec, SECB);
	memcpy(vpl, lut, lutbytes);
	put32(info + 32, T32O_PTR(vpl));
	t32_data_dd0a4[0] = T32O_PTR(base);
	t32_data_dc274[0] = T32O_PTR(info);
	t32_data_e1820[0] = 258;
	t32o_call((t32o_fn)(void (*)(void))tisp_gamma_init, 0, 0, 0, 0, 0);
	vst = T32O_HOST(t32_data_e1834[0]);
	vsec = T32O_HOST(get32(vst));
	vgs = T32O_HOST(get32(vst + 4));
	vev = T32O_HOST(get32(vst + 8));
	vb12 = T32O_HOST(get32(vst + 12));
	vcin = T32O_HOST(get32(vst + 16));
	vb20 = T32O_HOST(get32(vst + 20));
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

static void random_cases(unsigned int n)
{
	static u8 sec[SECB], lut[T32_GAMMA_REC_BYTES * 8 + 8];
	unsigned int c, i, j, recs;

	for (c = 0; c < n; c++) {
		u32 x = 0;
		int multi = c % 3 == 0;

		for (i = 0; i < SECB; i++)
			sec[i] = (u8)rnd();
		put32(sec, multi ? 0x134ff91 + rnd() % 3 : 0x134da11);
		sec[0x48] = (u8)(rnd() % 4);
		for (i = 0; i < 12; i++) {
			if (!(rnd() % 6) && i < 4)
				x = 0;
			else if (rnd() % 6)
				x += rnd() % 5000;
			put32(sec + 0x18 + 4 * i, x);
		}
		recs = 1 + rnd() % 8;
		sec[0x5a] = multi ? (u8)recs : (u8)(1 + rnd() % 3);
		for (i = 0; i < sizeof(lut); i++)
			lut[i] = (c % 4) ? (u8)rnd() : (u8)(rnd() & 0xf);
		if (multi) {
			/* records: curve of 129 values, then lo and hi ranges (ascending most of the time) */
			u32 edge = (rnd() % 4 ? 0 : rnd() % 1000);

			for (j = 0; j < recs; j++) {
				u32 rlo = edge + (rnd() % 5 ? rnd() % 200000 : 0), rhi = rlo + (rnd() % 8 ? rnd() % 300000 : 0);
				u8 *rec = lut + T32_GAMMA_REC_BYTES * j;

				for (i = 0; i < 129; i++) {
					rec[2 * i] = (u8)rnd();
					rec[2 * i + 1] = (u8)(rnd() % 16);
				}
				rec[258] = (u8)rlo; rec[259] = (u8)(rlo >> 8); rec[260] = (u8)(rlo >> 16); rec[261] = (u8)(rlo >> 24);
				rec[262] = (u8)rhi; rec[263] = (u8)(rhi >> 8); rec[264] = (u8)(rhi >> 16); rec[265] = (u8)(rhi >> 24);
				edge = rhi + (rnd() % 3 ? rnd() % 50 : 0);
			}
			run_case("random-multi", sec, lut, T32_GAMMA_REC_BYTES * recs, 60);
		} else {
			run_case("random", sec, lut, T32_GAMMA_CURVE_BYTES, 60);
		}
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
		off = t32_iq_section(bin, (u32)bytes, T32_SEC_GAMMA, &len);
		if (!off || len < SECB)
			continue;
		nbins++;
		for (rep = 0; rep < 8; rep++)
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
	printf("t32_gammaf_oracle: random %u sequences (%u steps, %u refused by ours), IQ bins %d (%u sequences, %u steps); ev updates %u no-op/%u run, %u steps multi-curve, %u steps API curve; %u mismatches\n",
	       rc_r, rs_r, refused, nbins, cases - rc_r, steps - rs_r, hit, ran, multi_steps, api_steps, failed);
	if (failed)
		return 1;
	if (!nbins) {
		printf("t32_gammaf_oracle: IQ-bin part: SKIP (set T32_IQ_DIR to a directory of T32 *.bin)\n");
		return strict ? 2 : 0;
	}
	return 0;
}
