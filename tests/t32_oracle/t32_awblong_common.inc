/*
 * Common body of the t32_awblgN_oracle programs: our phases 1 .. LG_PHASE of t32_awb_long_alogrithm
 * (driver/t32/tx_isp_t32_awblong.h) against the vendor tisp_awb_long_alogrithm (0x3d85c) from Matt Davis' mechanical
 * MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc), built with trunc_long.sh so that it stops at the label that ends
 * phase LG_PHASE (T32O_STOP in the conf).  The oracle builds the vendor's 47 pointer context and the channel context.
 * Compared at the stop: every byte of par, state and hardware shadow, and the named local stack words of the vendor.
 * Exit 0 = equal; 1 = mismatch; 2 = strict and no bins.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_iq.h"
#include "tx_isp_t32_awblong.h"

u64 tisp_awb_long_alogrithm(u32 a0, u32 a1, u32 a2, u32 a3);
extern u32 t32_data_e16b4[4];
#define SECB	6740u
#define PARB	sizeof(((struct t32_awb_par *)0)->raw)
#define STB	sizeof(((struct t32_awb_state *)0)->raw)
#define HWB	4292u

#include "t32_awbgen.inc"

static unsigned int cases, failed, nrefused, nstop, nearly;
static unsigned int cov[16];

#if LG_PHASE >= 5
/*
 * Colour temperature axes around the statistics phases 1 .. 4 produce (our phases on a copy), so that the detection does
 * not always end in its fallback.
 */
static void aim_axes(u8 *par, u8 *stt, u8 *hw)
{
	static u8 spar[PARB], sst[STB], shw[HWB];
	struct t32_awb sa = { (struct t32_awb_par *)spar, (struct t32_awb_state *)sst, (struct t32_awb_hw *)shw };
	struct t32_awb_lg sl;
	u32 row, col, os, ax, mn[2] = { 0xffffffffu, 0xffffffffu }, mx[2] = { 0, 0 }, k, i;

	memcpy(spar, par, PARB);
	memcpy(sst, stt, STB);
	memcpy(shw, hw, HWB);
	t32_awb_lg_entry(&sa, &sl);
	if (t32_awb_lg_zones(&sa, &sl))
		return;
	t32_awb_lg_means(&sa, &sl);
	t32_awb_lg_shares(&sa, &sl);
	t32_awb_lg_clear(&sa, &sl);
	os = sl.os & 31;
	for (row = 0; row < sl.rows; row++)
		for (col = 0; col < sl.cols; col++)
			for (k = 0; k < 2; k++) {
				u32 v = g32(sst, 4 * (4096 / 4 * k + row * 32 + col));

				if (v) {
					if (v < mn[k]) mn[k] = v;
					if (v > mx[k]) mx[k] = v;
				}
			}
	if (rnd() % 8 == 0)
		return;
	for (k = 0; k < 2; k++) {
		u32 lo, hi, step;

		if (mx[k] < mn[k])
			return;
		lo = mn[k] >> os;
		hi = (mx[k] >> os) + 1;
		if (lo > 20)
			lo -= rnd() % 20;
		step = (hi - lo) / 14 + 1;
		ax = 60 + 60 * k;
		for (i = 0; i < 15; i++)
			p32(par, ax + 4 * i, lo + i * step + rnd() % (step / 2 + 1));
	}
}
#endif

/* the inputs of the long algorithm on top of what gen() makes for ct_detect */
static void gen_long(u8 *par, u8 *stt, u8 *hw, const u8 *sec, u32 tbl)
{
	u32 z, i, den;

	gen(par, stt, sec, tbl);
	for (i = 0; i < HWB; i++)
		hw[i] = (u8)rnd();
	for (z = 0; z < 1024; z++) {
		static const u32 off[2][4] = { { 62632, 58536, 66728, 74920 }, { 42152, 38056, 46248, 54440 } };
		u32 g;

		for (g = 0; g < 2; g++) {
			u32 ratio_r = 100 + rnd() % 3900, ratio_b = 100 + rnd() % 3900;

			den = rnd() % 6 == 0 ? 0 : rnd() % 20 == 0 ? rnd() : 1 + rnd() % 60000;
			p32(stt, off[g][0] + 4 * z, den);
			p32(stt, off[g][1] + 4 * z, rnd() % 20 == 0 ? rnd() : (u32)((u64)den * ratio_r >> 10));
			p32(stt, off[g][2] + 4 * z, rnd() % 20 == 0 ? rnd() : (u32)((u64)den * ratio_b >> 10));
			p32(stt, off[g][3] + 4 * z, rnd() % 5 == 0 ? 0 : rnd() % 20 == 0 ? rnd() : rnd() % 2000);
		}
	}
	if (rnd() % 10 == 0) {
		p16(par, 3308, 30 + rnd() % 4);						/* out_shift around 31: negative half */
		if (rnd() % 2) {							/* few zones: the carry into the high word shows in the mean */
			p16(par, 3186, 1 + rnd() % 2);
			p16(par, 3190, 1 + rnd() % 3);
		}
	}
	p32(par, 4, rnd() % 4 == 0 ? 0 : rnd() % 4 == 0 ? rnd() : rnd() % 1500);			/* sat_thr */
	p16(par, 3438, rnd() % 4);									/* sat_mode */
	p32(par, 52, rnd() % 8 ? 600 + rnd() % 2000 : rnd());
	p32(par, 56, rnd() % 8 ? 600 + rnd() % 2000 : rnd());
	p16(par, 3290, rnd() % 10 == 0 ? rnd() : 1 + rnd() % 40);
	p16(par, 3292, rnd() % 10 == 0 ? rnd() : 1 + rnd() % 40);
	p32(par, 8, rnd() % 3 ? 0 : rnd() % 3 ? 1 : rnd());
	p32(par, 12, rnd() % 4 ? rnd() % 5 : rnd());
	p32(par, 16, rnd() % 4 ? 200 + rnd() % 3000 : rnd());
	p32(par, 20, rnd() % 4 ? 200 + rnd() % 3000 : rnd());
	p32(par, 24, rnd() % 4 ? 200 + rnd() % 3000 : rnd());
	p32(par, 28, rnd() % 4 ? 200 + rnd() % 3000 : rnd());
	p16(par, 3310, rnd() % 16 == 0 ? rnd() : rnd() % 18);
	p16(par, 3312, rnd());
	p16(par, 3314, rnd() % 4 ? rnd() % 600 : rnd());
	p16(par, 3316, rnd() % 4 ? rnd() % 600 : rnd());
	/* mean zone values: the reference next to the current ones, sometimes equal */
	{
		u32 r = rnd() % 4 ? 200 + rnd() % 1500 : rnd(), b = rnd() % 4 ? 200 + rnd() % 1500 : rnd();

		p16(stt, 107608, r);
		p16(stt, 107610, b);
		p16(stt, 107612, rnd() % 4 ? r + rnd() % 9 - 4 : rnd());
		p16(stt, 107614, rnd() % 4 ? b + rnd() % 9 - 4 : rnd());
		p16(stt, 107616, rnd() % 3 ? r + rnd() % 21 - 10 : rnd());
		p16(stt, 107618, rnd() % 3 ? b + rnd() % 21 - 10 : rnd());
	}
	{	/* the reference means next to the means phase 2 will compute: run our phases 1 and 2 on a copy */
		static u8 spar[PARB], sst[STB], shw[HWB];
		struct t32_awb sa = { (struct t32_awb_par *)spar, (struct t32_awb_state *)sst, (struct t32_awb_hw *)shw };
		struct t32_awb_lg sl;
		u32 m0, m1;

		memcpy(spar, par, PARB);
		memcpy(sst, stt, STB);
		memcpy(shw, hw, HWB);
		t32_awb_lg_entry(&sa, &sl);
		if (!t32_awb_lg_zones(&sa, &sl)) {
			t32_awb_lg_means(&sa, &sl);
			m0 = g16(sst, 107608);
			m1 = g16(sst, 107610);
			switch (rnd() % 8) {
			case 0:					/* the change exactly at the limit */
				p16(stt, 107616, m0 + 5);
				p16(stt, 107618, m1 - 3);
				p16(par, 3316, 8);
				break;
			case 1:
				p16(stt, 107616, m0 - 5);
				p16(stt, 107618, m1 + 3);
				p16(par, 3316, 9);
				break;
			case 2:					/* the change sum wraps at 16 bits */
				p16(stt, 107616, m0 + 40000);
				p16(stt, 107618, m1 + 30000);
				p16(par, 3316, 5000);
				break;
			case 3:
				p16(stt, 107616, m0 + rnd() % 5);
				p16(stt, 107618, m1 - rnd() % 5);
				break;
			}
		}
	}
	stt[79036] = rnd() % 3 ? 1 : (u8)rnd();
	stt[79044] = rnd() % 3 ? 1 : (u8)rnd();
	if (rnd() % 2) {
		stt[107596] = 0;
		stt[107598] = 0;
	}
	if (rnd() % 3 == 0)
		for (i = 0; i < 15; i++) {
			p32(stt, 79048 + 4 * i, 400 + rnd() % 1000);
			p32(stt, 79108 + 4 * i, 400 + rnd() % 1000);
			p32(stt, 79168 + 4 * i, 2500 + rnd() % 4000);
		}
	if (rnd() % 3 == 0) {	/* the last two entries equal */
		p32(stt, 79100, g32(stt, 79104));
		p32(stt, 79160, g32(stt, 79164));
	}
	if (rnd() % 3 == 0) {	/* par 16 / 20 equal to par 24 / 28 */
		p32(par, 24, g32(par, 16));
		p32(par, 28, g32(par, 20));
	}
#if LG_PHASE >= 5
	p32(par, 312, 1 + rnd() % 40);		/* ct_detect pass limit per point: the vendor's loop does not end for a huge one */
	aim_axes(par, stt, hw);
#endif
}

struct ours {
	struct t32_awb_lg l;
	struct t32_awb_ctd d;
};

/* our phases 1 .. LG_PHASE; returns -1 when the case is refused, 1 when the vendor leaves early */
static int run_ours(struct t32_awb *a, struct ours *o, u32 tbl)
{
		t32_awb_lg_entry(a, &o->l);
#if LG_PHASE >= 1
	if (t32_awb_lg_zones(a, &o->l))
		return -1;
#endif
#if LG_PHASE >= 2
	t32_awb_lg_means(a, &o->l);
	cov[0] += o->l.os == 31;
#if LG_PHASE >= 4
	{
		const struct t32_awb_state *s_ = a->st;
		u32 dr_ = (u16)(s_->avg[4] > s_->avg[0] ? s_->avg[4] - s_->avg[0] : s_->avg[0] - s_->avg[4]);
		u32 db_ = (u16)(s_->avg[5] > s_->avg[1] ? s_->avg[5] - s_->avg[1] : s_->avg[1] - s_->avg[5]);

		cov[2] += s_->lg_have == 1 && s_->lg_calm == 1 && (u16)(dr_ + db_) == o->l.still && !(t32_awb_u32at(s_->raw, 107596) & 0x00ff00ffU);
		cov[1] += s_->lg_have == 1 && s_->lg_calm == 1 && (u16)(dr_ + db_) < o->l.still && !(t32_awb_u32at(s_->raw, 107596) & 0x00ff00ffU);
	}
#endif
	cov[3] += o->l.cntr == 0;

#endif
#if LG_PHASE >= 3
	t32_awb_lg_shares(a, &o->l);
	t32_awb_lg_clear(a, &o->l);
#endif
#if LG_PHASE >= 4
	if (t32_awb_lg_gate(a, &o->l))
		return 1;
#endif
#if LG_PHASE >= 5
	if (t32_awb_lg_detect(a, &o->l, &o->d, tbl))
		return -1;
	cov[1] += o->l.valid[0] == 0;
#endif
	return 0;
}

/* the local stack words (index = vendor sp offset / 4) our locals stand for */
static const char *check_stack(const struct ours *o, const u32 *stk)
{
	const struct t32_awb_lg *l = &o->l;
	struct { u8 slot; u32 v; } w[] = {
		{ 8, l->valid[0] }, { 9, l->valid[1] }, { 10, l->gain[0] }, { 11, l->gain[1] }, { 12, l->out[0] }, { 13, l->out[1] },
		{ 14, l->one }, { 15, l->os }, { 16, l->cols }, { 17, l->rows }, { 18, l->p24 }, { 19, l->p28 }, { 20, l->p16 }, { 21, l->p20 },
		{ 22, LG_PHASE >= 2 ? l->half_hi : l->mr }, { 23, LG_PHASE >= 2 ? stk[23] : l->mb },	/* sp92 holds a spilled count once phase 2 ran */ { 24, l->sat_thr }, { 25, l->p12 }, { 26, l->area },
		{ 27, l->p8 }, { 28, l->w3312 }, { 29, l->small }, { 30, l->still },
	};
	unsigned int i;

	for (i = 0; i < sizeof(w) / sizeof(w[0]); i++)
		if (stk[w[i].slot] != w[i].v) {
			static char msg[80];

			snprintf(msg, sizeof(msg), "stack sp+%u: vendor %08x ours %08x", 4 * w[i].slot, stk[w[i].slot], w[i].v);
			return msg;
		}
#if LG_PHASE >= 2
	if (t32o_lg_reg[16 + 2] != l->win)
		return "s2";
#endif
	return 0;
}

static int run_case(const char *what, const u8 *sec)
{
	static u8 par[PARB], opar[PARB], stt[STB], ost[STB], hw[HWB], ohw[HWB];
	static struct ours o;
	u8 *vpar, *vst, *vhw, *vctx, *vtab;
	struct t32_awb a;
	const char *bad = 0;
	u32 i, ch = rnd() % 4, tbl = rnd() % 2 ? 1264 : 364;
	int r;

	gen_long(par, stt, hw, sec, tbl);
	memcpy(opar, par, PARB);
	memcpy(ost, stt, STB);
	memcpy(ohw, hw, HWB);

	t32o_reset();
	t32o_lg_stop = 0;
	t32o_ct_stop = 0;
	memset(t32o_lg_stack, 0, sizeof(t32o_lg_stack));
	vctx = t32o_alloc(16);
	vtab = t32o_alloc(256);
	vpar = t32o_alloc(PARB);
	vst = t32o_alloc(STB);
	vhw = t32o_alloc(HWB);
	memcpy(vpar, par, PARB);
	memcpy(vst, stt, STB);
	memcpy(vhw, hw, HWB);
	memset(vctx, 0, 16);
	p32(vctx, 0, T32O_PTR(vpar));
	p32(vctx, 4, T32O_PTR(vst));
	p32(vctx, 12, T32O_PTR(vhw));
	t32_data_e16b4[ch] = T32O_PTR(vctx);
	ctx_build(vtab, T32O_PTR(vpar), T32O_PTR(vst), T32O_PTR(vhw), tbl);
	if (getenv("T32O_TRACE"))
		fprintf(stderr, "case %u: vendor\n", cases + nrefused);
	t32o_call((t32o_fn)(void (*)(void))tisp_awb_long_alogrithm, T32O_PTR(vtab), ch, 0, 0, 0);

	a.par = (struct t32_awb_par *)opar;
	a.st = (struct t32_awb_state *)ost;
	a.hw = (struct t32_awb_hw *)ohw;
	memset(&o, 0xa5, sizeof(o));
	if (getenv("T32O_TRACE"))
		fprintf(stderr, "case %u: ours\n", cases + nrefused);
	r = run_ours(&a, &o, tbl);
	if (r < 0) {
		nrefused++;
		return 0;
	}
	cases++;
	if (t32o_lg_stop)
		nstop++;
	else
		nearly++;
	if (LG_PHASE < 4 && !t32o_lg_stop)
		bad = "vendor did not reach the label";
	else if ((r == 1) == (t32o_lg_stop != 0))
		bad = r == 1 ? "ours left early, vendor went on" : "ours went on, vendor left early";
	if (!bad && t32o_lg_stop)
		bad = check_stack(&o, t32o_lg_stack);
	if (!bad && memcmp(vpar, opar, PARB))
		bad = "par";
	else if (!bad && memcmp(vst, ost, STB))
		bad = "state";
	else if (!bad && memcmp(vhw, ohw, HWB))
		bad = "hw";
	if (!bad)
		return 0;
	if (failed++ < 5) {
		printf("MISMATCH %s: %s (rows %u cols %u)\n", what, bad, g16(par, 3186), g16(par, 3190));
		for (i = 0; i < PARB; i++)
			if (vpar[i] != opar[i]) { printf("  par+%u vendor %02x ours %02x\n", i, vpar[i], opar[i]); break; }
		for (i = 0; i < STB; i++)
			if (vst[i] != ost[i]) { printf("  st+%u vendor %02x ours %02x\n", i, vst[i], ost[i]); break; }
		for (i = 0; i < HWB; i++)
			if (vhw[i] != ohw[i]) { printf("  hw+%u vendor %02x ours %02x\n", i, vhw[i], ohw[i]); break; }
	}
	return 1;
}

static int bin_cases(const char *dir)
{
	DIR *d = opendir(dir);
	struct dirent *e;
	int nbins = 0;

	if (!d)
		return 0;
	while ((e = readdir(d))) {
		char pth[1024];
		static u8 b[1u << 20];
		FILE *f;
		size_t bytes, l = strlen(e->d_name);
		u32 off, len = 0, k;

		if (l < 4 || strcmp(e->d_name + l - 4, ".bin"))
			continue;
		snprintf(pth, sizeof(pth), "%s/%s", dir, e->d_name);
		f = fopen(pth, "rb");
		if (!f)
			continue;
		bytes = fread(b, 1, sizeof(b), f);
		fclose(f);
		off = t32_iq_section(b, (u32)bytes, T32_IQ_SEC_AWB, &len);
		if (!off || len < SECB)
			continue;
		nbins++;
		for (k = 0; k < 40; k++)
			run_case(e->d_name, b + off);
	}
	closedir(d);
	return nbins;
}

static void on_alarm(int sig, siginfo_t *si, void *uc_)
{
	ucontext_t *uc = uc_;

	(void)sig; (void)si;
	fprintf(stderr, "T32O_WATCHDOG: no progress, rip %llx\n", (unsigned long long)uc->uc_mcontext.gregs[REG_RIP]);
	_exit(3);
}

int main(void)
{
	if (getenv("T32O_WATCHDOG")) {
		struct sigaction sa;

		memset(&sa, 0, sizeof(sa));
		sa.sa_sigaction = on_alarm;
		sa.sa_flags = SA_SIGINFO;
		sigaction(SIGALRM, &sa, 0);
		alarm((unsigned)atoi(getenv("T32O_WATCHDOG")));
	}
	const char *dir = getenv("T32_IQ_DIR");
	int strict = getenv("T40_ORACLE_STRICT") && atoi(getenv("T40_ORACLE_STRICT"));
	unsigned int c, rc_r, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 3000;
	int nbins;

	for (c = 0; c < n; c++)
		run_case("random", 0);
	rc_r = cases;
	nbins = bin_cases(dir ? dir : "../external/ingenic-sdk/sensor-iq/t32");
	printf("t32_awblg%d_oracle: random %u, IQ bins %d (%u cases), refused %u, stopped at the label %u, vendor left earlier %u, coverage %u/%u/%u/%u, %u mismatches\n",
	       LG_PHASE, rc_r, nbins, cases - rc_r, nrefused, nstop, nearly, cov[0], cov[1], cov[2], cov[3], failed);
	if (failed)
		return 1;
	if (!nbins) {
		printf("t32_awblg%d_oracle: IQ-bin part: SKIP (set T32_IQ_DIR to a directory of T32 *.bin)\n", LG_PHASE);
		return strict ? 2 : 0;
	}
	return 0;
}
