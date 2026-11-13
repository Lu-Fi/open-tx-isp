// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: MDNS per-frame paths.  Runs the unmodified vendor
 * tisp_mdns_{par_refresh,refresh,dn_params_refresh,wake_up} (Matt Davis'
 * recovered C, PR #15 @ 2e2e06bc) and our t32_mdns_* port from
 * driver/t32/tx_isp_t32_mdns.h from the same state and compares every
 * register write (address, value, order), the section bytes, the context
 * fields (+0 section, +8 gain, +28 day/night word) and the saved day/night
 * tables.  The state (context, frame size, compression) comes from running
 * the vendor tisp_mdns_init on channel 0 first; only the writes after it are
 * compared.
 *
 * Data objects the refs read that env.c may not define (the wake_up shadow
 * registers and the day/night tables) are defined weak here (zero storage;
 * every case fills them with random inputs).  wake_up with the compression
 * off is refused by ours on purpose (cmp_reg_write's behaviour is unknown);
 * the oracle checks the refusal and counts it.
 * Exit: 0 ok, 1 mismatch, 2 oracle setup error.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_mdns.h"

#define SECB	T32_MDNS_SEC_BYTES
#define SEC_OFF	34652u
#define BASEB	(SEC_OFF + SECB + 64)
#define DNB	44u
#define FLAG0	0x5a5a1234u

extern u32 t32_data_e22c8[];
extern u8 t32_data_f0900[];
extern u8 t32_data_e2150[];
#define WEAK __attribute__((weak))
WEAK u8 t32_data_e2388[DNB];
WEAK u8 t32_data_e235c[DNB];
WEAK u8 t32_data_e2330[DNB];
WEAK u8 t32_data_e2304[DNB];
WEAK u8 t32_data_e22d8[DNB];
WEAK u32 t32_data_e227c[15];
WEAK u32 t32_data_e21d4[6];
WEAK u32 t32_data_e226c[4];
WEAK u32 t32_data_e2228[17];
WEAK u32 t32_data_e21ec[15];

extern u64 tisp_mdns_par_refresh(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_mdns_refresh(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_mdns_dn_params_refresh(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_mdns_wake_up(u32 a0, u32 a1, u32 a2, u32 a3);

static u32 rs = 0x51d3a7u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

enum { C_PAR, C_REFRESH, C_DN, C_WAKE };

struct tc {
	u32 w, h, cmpb, init_gain, gain, thr, flag;
	u8 ha, hb;
};

static unsigned int compared, refused, failed;
static struct t32_front_write ow[T32O_MAX_WRITES], rw[T32O_MAX_WRITES];
static u8 ourbase[BASEB], oval[T32_MDNS_VALUES + 16];

static int run_case(const char *what, int which, const struct tc *t, const u8 *sec)
{
	u8 *base, *info, *ctx;
	u8 dnin[5][DNB], rdn[5][DNB], rsec[SECB];
	u8 *const dnp[5] = { t32_data_e2388, t32_data_e235c, t32_data_e2330, t32_data_e2304, t32_data_e22d8 };
	u32 *const shp[5] = { t32_data_e227c, t32_data_e21d4, t32_data_e226c, t32_data_e2228, t32_data_e21ec };
	static const unsigned int shn[5] = { 15, 6, 4, 17, 15 };
	struct t32_mdns_shadow sh;
	u32 *shs[5];
	struct t32_mdns_ctx c;
	u32 rgain, rflag, roff, oroff;
	unsigned int nb, rn, i, k;
	int on = 0;

	shs[0] = sh.a; shs[1] = sh.b; shs[2] = sh.c; shs[3] = sh.d; shs[4] = sh.e;

	if (which == C_WAKE && t->cmpb <= 10) {
		memset(&c, 0, sizeof(c));
		memset(&sh, 0, sizeof(sh));
		c.memopt = t->cmpb * 100;
		if (t32_mdns_wake_up(&c, &sh, ow) != -1) {
			if (failed++ < 3)
				printf("MISMATCH %s: wake_up with compression off must be refused\n", what);
			return 1;
		}
		refused++;
		return 0;
	}

	t32o_reset();
	base = t32o_alloc(BASEB);
	info = t32o_alloc(64);
	if (!base || !info)
		return 2;
	memset(info, 0, 64);
	info[0] = (u8)t->w; info[1] = (u8)(t->w >> 8);
	info[4] = (u8)t->h; info[5] = (u8)(t->h >> 8);
	info[42] = (u8)t->cmpb;
	memcpy(base + SEC_OFF, sec, SECB);
	t32_data_dd0a4[0] = T32O_PTR(base);
	t32o_call((t32o_fn)(void (*)(void))tisp_mdns_init, 0, T32O_PTR(info), 0, 0, 0);
	if (!t32_data_e22c8[0]) {
		fprintf(stderr, "t32_mdnsf_oracle: t32_data_e22c8[0] is 0 after tisp_mdns_init\n");
		return 2;
	}
	ctx = T32O_HOST(t32_data_e22c8[0]);
	memcpy(ourbase, base, BASEB);

	for (k = 0; k < 5; k++) {
		for (i = 0; i < DNB; i++)
			dnin[k][i] = (u8)rnd();
		memcpy(dnp[k], dnin[k], DNB);
		for (i = 0; i < shn[k]; i++) {
			shs[k][i] = rnd();
			shp[k][i] = shs[k][i];
		}
	}
	*(u32 *)(ctx + 8) = t->init_gain;
	*(u32 *)(ctx + 28) = FLAG0;
	t32_data_f0900[0] = t->ha;
	t32_data_e2150[0] = t->hb;
	nb = t32o_log.n;

	switch (which) {
	case C_PAR:
		t32o_call((t32o_fn)(void (*)(void))tisp_mdns_par_refresh, 0, t->gain, t->thr, 0, 0);
		break;
	case C_REFRESH:
		t32o_call((t32o_fn)(void (*)(void))tisp_mdns_refresh, 0, t->gain, 0, 0, 0);
		break;
	case C_DN:
		t32o_call((t32o_fn)(void (*)(void))tisp_mdns_dn_params_refresh, 0, t->flag, 0, 0, 0);
		break;
	default:
		t32o_call((t32o_fn)(void (*)(void))tisp_mdns_wake_up, 0, 0, 0, 0, 0);
		break;
	}
	rn = t32o_log.n - nb;
	if (t32o_log.n < nb || rn > T32O_MAX_WRITES)
		return 2;
	memcpy(rw, &t32o_log.w[nb], rn * sizeof(rw[0]));
	memcpy(rsec, base + SEC_OFF, SECB);
	rgain = *(u32 *)(ctx + 8);
	rflag = *(u32 *)(ctx + 28);
	roff = *(u32 *)(ctx + 0) - T32O_PTR(base);
	for (k = 0; k < 5; k++)
		memcpy(rdn[k], dnp[k], DNB);

	memset(&c, 0, sizeof(c));
	c.sec = ourbase + SEC_OFF;
	c.val = oval;
	c.gain = t->init_gain;
	c.dn_mode = FLAG0;
	c.width = t->w;
	c.height = t->h;
	c.memopt = t->cmpb * 100;
	c.hold_a = t->ha;
	c.hold_b = t->hb;
	memcpy(c.dn, dnin, sizeof(dnin));
	t32_mdns_intp_g(c.sec, c.val, 0x10000);

	switch (which) {
	case C_PAR:
		on = t32_mdns_par_refresh(&c, t->gain, t->thr, ow);
		break;
	case C_REFRESH:
		on = t32_mdns_refresh(&c, t->gain, ow);
		break;
	case C_DN:
		on = t32_mdns_dn_params_refresh(&c, 0, ourbase, t->flag, ow);
		break;
	default:
		on = t32_mdns_wake_up(&c, &sh, ow);
		break;
	}
	oroff = (u32)(c.sec - ourbase) - SEC_OFF;
	oroff += SEC_OFF;

	compared++;
	if (on < 0 || (unsigned int)on != rn)
		goto bad;
	for (i = 0; i < rn; i++)
		if (rw[i].reg != ow[i].reg || rw[i].val != ow[i].val)
			goto bad;
	if (memcmp(rsec, ourbase + SEC_OFF, SECB))
		goto bad;
	if (rgain != c.gain || rflag != c.dn_mode || roff != oroff)
		goto bad;
	for (k = 0; k < 5; k++)
		if (memcmp(rdn[k], c.dn[k], DNB))
			goto bad;
	return 0;
bad:
	if (failed++ < 3) {
		printf("MISMATCH %s (fn %d): writes vendor %u ours %d\n", what, which, rn, on);
		for (i = 0; i < rn || (int)i < on; i++)
			if (i >= rn || (int)i >= on || rw[i].reg != ow[i].reg || rw[i].val != ow[i].val) {
				printf("  #%u vendor %05x=%08x ours %05x=%08x\n", i,
				       i < rn ? rw[i].reg : 0, i < rn ? rw[i].val : 0,
				       (int)i < on ? ow[i].reg : 0, (int)i < on ? ow[i].val : 0);
				break;
			}
		printf("  gain vendor %08x ours %08x, flag %08x/%08x, sec off %x/%x\n",
		       rgain, c.gain, rflag, c.dn_mode, roff, oroff);
		for (i = 0; i < SECB; i++)
			if (rsec[i] != ourbase[SEC_OFF + i]) {
				printf("  sec+0x%x vendor %02x ours %02x\n", i, rsec[i], ourbase[SEC_OFF + i]);
				break;
			}
		for (k = 0; k < 5; k++)
			if (memcmp(rdn[k], c.dn[k], DNB))
				printf("  dn table %u differs\n", k);
	}
	return 1;
}

static void gen_sec(u8 *sec, unsigned int c)
{
	unsigned int i;

	for (i = 0; i < SECB; i++)
		sec[i] = (c % 3) ? (u8)rnd() : (u8)(rnd() & 7);
}

static u32 rgain_(void)
{
	return rnd() % (9u << 16);
}

int main(void)
{
	const unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 900;
	static u8 sec[SECB];
	unsigned int c, f, m, hb;
	int rc;
	struct tc t;
	static const u32 dpar[6] = { 0, 1, 2, 3, 4, 5 };

	for (c = 0; c < n && !failed; c++) {
		gen_sec(sec, c);
		for (f = 0; f < 4 && !failed; f++) {
			static const char *const nm[4] = { "par", "refresh", "dn", "wake" };
			u32 thr;

			memset(&t, 0, sizeof(t));
			t.w = 64 + rnd() % 4000;
			t.h = 64 + rnd() % 3000;
			if (f == C_WAKE)
				t.cmpb = rnd() % 8 ? 11 + rnd() % 10 : 10;
			else
				t.cmpb = rnd() % 2 ? 10 : 11 + rnd() % 10;
			t.ha = rnd() & 1;
			t.hb = rnd() & 1;
			t.init_gain = rnd() % 4 == 0 ? 0xffffffffu : rgain_();
			t.flag = rnd();
			m = rnd() % 3;
			thr = m == 0 ? 256 : m == 1 ? 1 + rnd() % 5000 : rnd();
			if (f == C_REFRESH)
				thr = 256;
			t.thr = thr;
			switch (rnd() % 8) {
			case 0: t.gain = t.init_gain + thr - 1; break;
			case 1: t.gain = t.init_gain + thr; break;
			case 2: t.gain = t.init_gain + thr + 1; break;
			case 3: t.gain = t.init_gain - (thr - 1); break;
			case 4: t.gain = t.init_gain - thr; break;
			case 5: t.gain = t.init_gain - (thr + 1); break;
			case 6: t.gain = t.init_gain; break;
			default: t.gain = rgain_(); break;
			}
			rc = run_case(nm[f], (int)f, &t, sec);
			if (rc == 2)
				return 2;
		}
	}
	/* explicit boundaries: all hold-flag combinations, the -1 start, hysteresis edges */
	gen_sec(sec, 1);
	for (hb = 0; hb < 4 && !failed; hb++)
		for (m = 0; m < 6 && !failed; m++) {
			memset(&t, 0, sizeof(t));
			t.w = 1920; t.h = 1080; t.cmpb = 11 + hb;
			t.ha = hb & 1; t.hb = hb >> 1;
			t.thr = 256;
			t.init_gain = m < 2 ? 0xffffffffu : 0x20000;
			t.gain = m == 0 ? 0x10000 : m == 1 ? 0x30000 : m == 2 ? 0x20000 + 255 :
				 m == 3 ? 0x20000 + 256 : m == 4 ? 0x20000 - 256 : 0x20000 - 255;
			t.flag = dpar[m];
			rc = run_case("bound-par", C_PAR, &t, sec);
			if (rc == 2)
				return 2;
			rc = run_case("bound-refresh", C_REFRESH, &t, sec);
			if (rc == 2)
				return 2;
			rc = run_case("bound-dn", C_DN, &t, sec);
			if (rc == 2)
				return 2;
			rc = run_case("bound-wake", C_WAKE, &t, sec);
			if (rc == 2)
				return 2;
		}

	printf("t32_mdnsf_oracle: %u compared (%u wake_up refused with compression off), %u mismatches\n",
	       compared, refused, failed);
	return failed ? 1 : 0;
}
