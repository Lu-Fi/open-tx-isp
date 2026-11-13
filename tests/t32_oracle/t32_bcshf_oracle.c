// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle, BCSH per-frame: our stateful model (struct t32_bcsh in
 * driver/t32/tx_isp_t32_bcsh.h: t32_bcsh_ev_update, t32_bcsh_ct_update,
 * t32_bcsh_frame_done and the interpolations / calc / write behind them)
 * against the vendor tisp_bcsh_ev_update (0x7defc) and tisp_bcsh_ct_update
 * (0x7e420) -> tisp_bcsh_interp_by_ev / _by_ct, tisp_bcsh_calc_para_refresh,
 * tisp_bcsh_set_write_reg_event and tisp_bcsh_irq_frame_down -> tisp_bcsh_write_reg,
 * taken from Matt Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc).
 * tisp_bcsh_adjust_hue gets its 22 stack words by the build-time fix of
 * fix_bcsh_adjust_hue.sh (see there).
 *
 * Every case starts with the init on both sides, then a random sequence of ev and
 * ct updates (also with section byte 0x4b1 set: the raw-exposure mode), frame-done
 * calls, the bare interp / calc (modes 0..4) / register-write calls with force
 * 0/1/2/255 and pokes of the switches the API and tuning code change (section bytes
 * 0x492..0x496, 0x498.., thresholds, the API flag and matrix, the user settings).
 * After every step compared: return value, register writes in order, the parameter
 * set, the matrix block, the API set, the band table, the caches and counters and all
 * 1204 section bytes.  Inputs: section 19 / 9 of every *.bin in T32_IQ_DIR,
 * perturbed copies of them and random sections.
 *
 * Exit 0 = equal; 1 = mismatch; 2 = strict and no bins.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_iq.h"
#include "tx_isp_t32_front.h"
#include "tx_isp_t32_bcsh.h"

#define SECB	T32_BCSH_SEC_BYTES
#define SEC_OFF	53588u
#define CSC_OFF	27488u

static u32 rs = 0x4d29b1u;
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

static unsigned int steps, failed, refused, cases, ev_ran, ct_ran, ev_all, ct_all, api_steps, mode_b;
#define NBASE 80
static u8 base_sec[NBASE][T32_BCSH_SEC_BYTES], base_lut[NBASE][10 * 40], base_csc[NBASE][104];
static u32 base_lutlen[NBASE];
static int nbase;

static int lastk;
static u32 lastforce;
static struct t32_bcsh st;
static u8 osec[SECB];
static u8 *vsec, *vcp, *vm112, *vap, *vp5, *vblk;
static struct t32_front_write ow[T32_BCSH_MAX_WRITES + 16];

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
			if (!t32o_reg_eq(t32o_log.w[i].reg, ow[i].reg) || t32o_log.w[i].val != ow[i].val) {
				bad = "write";
				break;
			}
	if (!bad && memcmp(vcp, st.cp, sizeof(st.cp)))
		bad = "cp";
	if (!bad && memcmp(vm112, st.m112, sizeof(st.m112)))
		bad = "m112";
	if (!bad && memcmp(vap, st.ap, sizeof(st.ap)))
		bad = "ap";
	if (!bad && memcmp(vp5, st.p5, sizeof(st.p5)))
		bad = "p5";
	if (!bad && (get32(vblk) != st.ev_lo || get32(vblk + 4) != st.ev_hi || get32(vblk + 8) != st.ct ||
		     get32(vblk + 12) != st.ct_code))
		bad = "cache";
	if (!bad && (vblk[16] != st.ctr_ev || vblk[17] != st.ctr_ct || vblk[18] != st.pend || vblk[19] != st.b19))
		bad = "counters";
	if (!bad && memcmp(vsec, osec, SECB))
		bad = "section";
	if (!bad)
		return 0;
	if (failed < 6) {
		printf("MISMATCH %s: %s (vendor rv %d writes %u; ours rv %d writes %u) 4b1=%u 492=%u kind %d mode %u d0=%x ce=%x\n", what, bad, rv_v,
		       t32o_log.n, rv_o, on, osec[0x4b1], osec[0x492], lastk, lastforce, t32_tp_le16(osec + 0xd0), t32_tp_le16(osec + 0xce));
		if (!strcmp(bad, "cp"))
			for (i = 0; i < sizeof(st.cp); i++)
				if (vcp[i] != st.cp[i]) {
					printf("  cp+0x%x vendor %02x ours %02x\n", i, vcp[i], st.cp[i]);
					if (i > 0 && failed > 3)
						break;
				}
		if (!strcmp(bad, "section"))
			for (i = 0; i < SECB; i++)
				if (vsec[i] != osec[i])
					printf("  sec+0x%x vendor %02x ours %02x\n", i, vsec[i], osec[i]);
		if (!strcmp(bad, "cache") || !strcmp(bad, "counters"))
			printf("  vendor %x %x %x %x | %u %u %u %u  ours %x %x %x %x | %u %u %u %u\n", get32(vblk), get32(vblk + 4),
			       get32(vblk + 8), get32(vblk + 12), vblk[16], vblk[17], vblk[18], vblk[19], st.ev_lo, st.ev_hi,
			       st.ct, st.ct_code, st.ctr_ev, st.ctr_ct, st.pend, st.b19);
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

static u32 pick_ct(void)
{
	const u8 *lut = st.lut;
	int knots = osec[0x4af], hyst = osec[0x4b0];
	u32 base;

	switch (rnd() % 8) {
	case 0: return rnd() % 12000;
	case 1: base = st.ct; break;
	case 2: base = get32(lut + 40 * (rnd() % (u32)knots)); break;
	case 3: base = get32(lut + 40 * (rnd() % (u32)knots)) + (rnd() & 1 ? (u32)hyst : -(u32)hyst); break;
	case 4: return rnd();
	case 5: base = st.ct + t32_tp_le16(osec + 156); break;
	case 6: base = st.ct - t32_tp_le16(osec + 156); break;
	default: base = 5000; break;
	}
	return rnd() % 5 < 3 ? base : base + (rnd() % 5) - 2;
}

static void pick_ev(u32 *lo, u32 *hi)
{
	unsigned long long cur = (unsigned long long)st.ev_hi << 32 | st.ev_lo, v;
	u32 thr = get32(osec + 100), t;

	switch (rnd() % 9) {
	case 0: *lo = rnd(); *hi = rnd() & 0x3ff; return;
	case 1: *lo = rnd(); *hi = rnd(); return;
	case 2: *lo = 0; *hi = 256 + (rnd() % 3) - 1; return;
	case 3: *lo = rnd() % 3; *hi = 256; return;
	case 4: *lo = st.ev_lo; *hi = st.ev_hi; return;
	case 5: v = cur + thr + (rnd() % 3) - 1; break;
	case 6: v = cur - thr + (rnd() % 3) - 1; break;
	case 7:		/* on / around a threshold of either mode */
		if (osec[0x4b1]) {
			*lo = get32(osec + 0x34 + 4 * (rnd() % 12)) + (rnd() % 3) - 1;
			*hi = rnd() % 4 ? 0 : rnd() % 300;
		} else {
			t = get32(osec + 4 + 4 * (rnd() % 12)) + (rnd() % 3) - 1;
			*hi = t >> 22;
			*lo = t << 10 | (rnd() & 0x3ff);
		}
		return;
	default: *lo = rnd() << (rnd() % 20); *hi = rnd() % 5; return;
	}
	*lo = (u32)v;
	*hi = (u32)(v >> 32);
}

static void random_poke(void)
{
	unsigned int i;
	u32 r = rnd() % 24, v;

	switch (r) {
	case 0: poke(0x493, (u8)(rnd() % 3 ? rnd() % 3 : rnd())); break;
	case 1: poke(0x494, (u8)(rnd() % 4)); break;
	case 2: poke(0x496, (u8)(rnd() % 3)); break;
	case 3: poke(0x492, (u8)(rnd() % 5)); break;
	case 4: poke(0x4b1, (u8)(rnd() % 3 ? 0 : 1 + rnd() % 2)); break;
	case 5: poke(0x498, (u8)(rnd() % 3)); poke(0x499, (u8)(rnd() % 4)); break;
	case 6: poke(0x461, (u8)(rnd() % 3)); poke(0x462, (u8)(rnd() % 4)); break;
	case 7: poke(0x467, (u8)(rnd() % 3)); poke(0x468, (u8)(rnd() % 4)); break;
	case 8: poke(0x49e, (u8)(rnd() % 3)); poke(0x49f, (u8)(rnd() % 4)); break;
	case 9: st.ap[4] = vap[4] = (u8)(rnd() % 3 ? 1 : (rnd() & 1) ? 0 : 0x80); break;
	case 10:
		for (i = 0; i < 9; i++) {
			v = rnd() % 3 ? (u32)((int)(rnd() % 0x40000) - 0x20000) : rnd();
			put32(st.ap + 8 + 4 * i, v);
			put32(vap + 8 + 4 * i, v);
		}
		break;
	case 11: for (i = 0; i < 4; i++) { st.ap[i] = vap[i] = (u8)rnd(); } break;
	case 12: st.b19 = vblk[19] = (u8)(rnd() % 3 ? 1 : rnd() % 3); break;
	case 13: poke32(100, rnd() % 3 ? rnd() % 200 : rnd() >> (rnd() % 32)); break;
	case 14: { u32 q = rnd() % 4 ? rnd() % 4000 : rnd(); poke(156, (u8)q); poke(157, (u8)(q >> 8)); break; }
	case 15: poke(0x4b0, (u8)(rnd() % 4 ? rnd() % 200 : rnd() % 4)); break;
	case 16: poke(0x4af, (u8)(1 + rnd() % (st.lutbytes / 40 > 10 ? 10 : st.lutbytes / 40))); break;
	case 17: {	/* ascending thresholds of the active mode */
		u32 x = rnd() % 3 ? 0 : rnd() % 2000, o = osec[0x4b1] ? 0x34 : 4;

		for (i = 0; i < 12; i++) {
			x = osec[0x4b1] ? (i ? get32(osec + o + 4 * (i - 1)) - rnd() % 3000 : 0x7fffffff / 2) :
					  x + (rnd() % 6 ? rnd() % 5000 : 0);
			poke32(o + 4 * i, x);
		}
		break;
	}
	case 18: poke(0x45c, (u8)(rnd() % 4)); poke(0x45d, (u8)(rnd() % 4)); poke(0x45a, (u8)(rnd() % 2)); break;
	case 19: poke(0x466, (u8)(rnd() % 4)); poke(0x49d, (u8)(rnd() % 4)); poke(0x497, (u8)(rnd() % 2)); break;
	case 20:
		for (i = 0; i < 9; i++)
			poke32(0x68 + 4 * i, rnd() % 3 ? (u32)((int)(rnd() % 4096) - 2048) : rnd());
		break;
	case 21:	/* counters close to the wrap, pending request anywhere */
		st.ctr_ev = vblk[16] = (u8)(250 + rnd() % 6);
		st.ctr_ct = vblk[17] = (u8)(250 + rnd() % 6);
		st.pend = vblk[18] = (u8)(rnd() % 18);
		break;
	case 22:	/* inputs of the tail (limits, clip points) */
		v = rnd() % 2000;
		poke(0xce, (u8)v);
		poke(0xcf, (u8)(v >> 8));
		v += rnd() % 3 ? rnd() % 1500 : 0;
		poke(0xd0, (u8)v);
		poke(0xd1, (u8)(v >> 8));
		for (i = 0x56; i < 0x5a; i++)
			st.ap[i] = vap[i] = (u8)(rnd() % 3 ? rnd() : 0xff);
		break;
	default:
		for (i = 0; i < 5; i++) {
			u32 o = 4 + rnd() % (SECB - 4);

			if (o != 0x4af)
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
	u32 lo, hi, lo2, hi2, ct, force;
	char tag[96];

	t32o_log.n = 0;
	steps++;
	lastk = (int)(rnd() % 12);
	switch (lastk) {
	case 0: case 1:
		pick_ev(&lo, &hi);
		pick_ev(&lo2, &hi2);
		{
			u8 before = st.ctr_ev;
			u32 elo = st.ev_lo, ehi = st.ev_hi;

			t32_bcsh_ev_update(&st, lo, hi, lo2, hi2);
			ev_all++;
			if (elo != st.ev_lo || ehi != st.ev_hi || before == 255)
				ev_ran++;
		}
		t32o_arg5 = hi2;
		t32o_call((t32o_fn)(void (*)(void))tisp_bcsh_ev_update, 0, 0, lo, hi, lo2);
		break;
	case 2: case 3:
		ct = pick_ct();
		{
			u32 octs = st.ct;

			t32_bcsh_ct_update(&st, ct);
			ct_all++;
			if (octs != st.ct)
				ct_ran++;
		}
		t32o_call((t32o_fn)(void (*)(void))tisp_bcsh_ct_update, 0, 0, ct, 0, 0);
		break;
	case 4: case 5:
		on = (unsigned)t32_bcsh_frame_done(&st, ow);
		t32o_call((t32o_fn)(void (*)(void))tisp_bcsh_irq_frame_down, 0, 0, 0, 0, 0);
		break;
	case 6:
		pick_ev(&lo, &hi);
		force = pick_force();
		rv_o = (int)t32_bcsh_ev_interp(&st, lo, hi, force);
		rv_v = (int)(u32)t32o_call((t32o_fn)(void (*)(void))tisp_bcsh_interp_by_ev, 0, 0, lo, hi, force);
		break;
	case 7:
		ct = pick_ct();
		force = pick_force();
		rv_o = (int)t32_bcsh_ct_interp(&st, ct, force);
		rv_v = (int)(u32)t32o_call((t32o_fn)(void (*)(void))tisp_bcsh_interp_by_ct, 0, 0, ct, 0, force);
		break;
	case 8:
		force = rnd() % 6;
		if (rnd() % 8 == 0)
			force |= 0x100;
		lastforce = force;
		t32_bcsh_calc(&st, force);
		t32o_call((t32o_fn)(void (*)(void))tisp_bcsh_calc_para_refresh, 0, force, 0, 0, 0);
		break;
	case 9:
		on = t32_bcsh_write(&st, ow);
		t32o_call((t32o_fn)(void (*)(void))tisp_bcsh_write_reg, 0, 0, 0, 0, 0);
		break;
	default:
		random_poke();
		break;
	}
	if (osec[0x4b1])
		mode_b++;
	if (!(osec[0x493] == 1 || (osec[0x493] == 0 && osec[0x494] < 2) || (s8)st.ap[4] == 0))
		api_steps++;
	snprintf(tag, sizeof(tag), "%s step %u", what, k);
	return cmp_state(tag, on, rv_o, rv_v);
}

static int run_case_mode(const char *what, const u8 *sec, const u8 *lut, u32 lutbytes, const u8 *csc, unsigned int nsteps, u8 dsm)
{
	u8 *base, *info, *vlut, *c;
	unsigned int on, k;

	if (csc[0x5b] > 5)
		return 0;
	memcpy(osec, sec, SECB);
	on = (unsigned)t32_bcsh_start(&st, osec, lut, lutbytes, csc, ow);
	if ((int)on < 0) {
		refused += !dsm;
		return 0;
	}
	cases++;
	t32o_reset();
	base = t32o_alloc(0x10000);
	info = t32o_alloc(64);
	vlut = t32o_alloc(lutbytes + 8);
	memcpy(base + SEC_OFF, sec, SECB);
	memcpy(base + CSC_OFF, csc, 104);
	memcpy(vlut, lut, lutbytes);
	put32(info + 28, T32O_PTR(vlut));
	t32_data_dd0a4[0] = T32O_PTR(base);
	t32_data_dc274[0] = T32O_PTR(info);
	t32_data_d03a0[0] = dsm;	/* 1: tisp_reg_write queues into the register shadow (logged tagged, same values) */
	t32o_call((t32o_fn)(void (*)(void))tisp_csc_init, 0, 0, 0, 0, 0);
	t32o_log.n = 0;
	t32o_call((t32o_fn)(void (*)(void))tisp_bcsh_init, 0, 0, 0, 0, 0);
	c = T32O_HOST(t32_data_e2450[0]);
	vsec = T32O_HOST(get32(c));
	vblk = T32O_HOST(get32(c + 4));
	vcp = T32O_HOST(get32(c + 8));
	vm112 = T32O_HOST(get32(c + 12));
	vap = T32O_HOST(get32(c + 16));
	vp5 = T32O_HOST(get32(c + 20));
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

static int run_case(const char *what, const u8 *sec, const u8 *lut, u32 lutbytes, const u8 *csc, unsigned int nsteps)
{
	int bad = run_case_mode(what, sec, lut, lutbytes, csc, nsteps, 0);

	bad += run_case_mode(what, sec, lut, lutbytes, csc, nsteps, 1);	/* d03a0 = 1: only the write addresses may differ */
	t32_data_d03a0[0] = 0;
	return bad;
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
		u32 off, len = 0, coff, clen = 0;

		if (l < 4 || strcmp(e->d_name + l - 4, ".bin"))
			continue;
		snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
		f = fopen(path, "rb");
		if (!f)
			continue;
		bytes = fread(bin, 1, sizeof(bin), f);
		fclose(f);
		off = t32_iq_section(bin, (u32)bytes, T32_SEC_BCSH, &len);
		coff = t32_iq_section(bin, (u32)bytes, 9, &clen);
		if (!off || len < SECB || !coff || clen < 104)
			continue;
		nbins++;
		if (nbase < NBASE) {
			u32 ll = bin[off + 0x4af] * 40U;

			memcpy(base_sec[nbase], bin + off, SECB);
			memcpy(base_lut[nbase], bin + off + len, ll > 400 ? 400 : ll);
			memcpy(base_csc[nbase], bin + coff, 104);
			base_lutlen[nbase++] = ll > 400 ? 400 : ll;
		}
		run_case(e->d_name, bin + off, bin + off + len, (u32)bytes - off - len, bin + coff, 100);
	}
	closedir(d);
	return nbins;
}

int main(void)
{
	const char *dir = getenv("T32_IQ_DIR");
	int strict = getenv("T40_ORACLE_STRICT") && atoi(getenv("T40_ORACLE_STRICT"));
	unsigned int c, i, k, rc_c, rc_s, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 1500;
	int nbins;

	nbins = bin_cases(dir ? dir : "../external/ingenic-sdk/sensor-iq/t32");
	rc_c = cases;
	rc_s = steps;
	for (c = 0; c < n && nbase; c++) {
		static u8 sec[SECB], lut[10 * 40], csc[104];
		unsigned int b = rnd() % nbase, np = rnd() % 6;

		memcpy(sec, base_sec[b], SECB);
		memcpy(lut, base_lut[b], base_lutlen[b]);
		memcpy(csc, base_csc[b], 104);
		while (np--) {
			unsigned int r = rnd() % (SECB + 104);

			if (r < SECB && r >= 4)
				sec[r] = (u8)(rnd() % 3 ? sec[r] + (rnd() % 5) - 2 : rnd());
			else if (r >= SECB && r - SECB != 0x5b)
				csc[r - SECB] = (u8)(rnd() % 3 ? csc[r - SECB] + (rnd() % 5) - 2 : rnd());
		}
		sec[0x4b1] = (u8)(rnd() % 5 ? 0 : 1 + rnd() % 3);
		csc[0x5b] = base_csc[b][0x5b];
		run_case("perturbed", sec, lut, base_lutlen[b], csc, 60);
	}
	for (c = 0; c < n; c++) {
		static u8 sec[SECB], lut[10 * 40], csc[104];
		unsigned int knots = 1 + rnd() % 10;
		u32 ct = 1500 + rnd() % 2500;

		for (i = 0; i < SECB; i++)
			sec[i] = (c % 3) ? (u8)rnd() : (u8)(rnd() & 7);
		put32(sec, 0x0134dae2);
		sec[0x4af] = (u8)knots;
		sec[0x4b0] = (u8)(rnd() % 4 ? rnd() % 255 : rnd() % 6);
		sec[0x4b1] = (u8)(rnd() % 5 ? 0 : 1 + rnd() % 3);
		sec[0x492] = (u8)(rnd() % 4);
		{
			u32 x = rnd() % 3 ? 0 : rnd() % 3000;

			for (i = 0; i < 12; i++) {
				x += rnd() % 5 ? rnd() % 6000 : 0;
				put32(sec + 4 + 4 * i, x);
				put32(sec + 0x34 + 4 * i, 0x40000000u - (i * 1000 + rnd() % 1000) * (rnd() % 3 + 1));
			}
		}
		for (k = 0; k < knots; k++) {
			u32 t = rnd() % 3 ? ct : 5000 + (rnd() % 2 ? 1 : -1) * (sec[0x4b0] + rnd() % 3);

			if (t < ct)
				t = ct;
			put32(lut + 40 * k, t);
			for (i = 0; i < 9; i++)
				put32(lut + 40 * k + 4 + 4 * i, (c % 4) ? (u32)((int)(rnd() % 4096) - 2048) : rnd());
			ct = t + rnd() % 2500;
		}
		for (i = 0; i < 104; i++)
			csc[i] = (u8)rnd();
		csc[0x5b] = (u8)(rnd() % 6);
		csc[0x62] = (u8)(rnd() % 5 ? (rnd() % 2) : rnd());
		csc[0x63] = (u8)(rnd() % 3);
		run_case("random", sec, lut, knots * 40, csc, 60);
	}
	printf("t32_bcshf_oracle: IQ bins %d (%u sequences), perturbed + random %u sequences (%u steps, %u refused by ours); %u/%u ev updates and %u/%u ct updates recomputed; %u steps in the raw-exposure mode, %u with the API matrix; %u mismatches\n",
	       nbins, rc_c, cases - rc_c, steps - rc_s, refused, ev_ran, ev_all, ct_ran, ct_all, mode_b, api_steps, failed);
	if (failed)
		return 1;
	if (!nbins) {
		printf("t32_bcshf_oracle: IQ-bin part: SKIP (set T32_IQ_DIR to a directory of T32 *.bin)\n");
		return strict ? 2 : 0;
	}
	return 0;
}
