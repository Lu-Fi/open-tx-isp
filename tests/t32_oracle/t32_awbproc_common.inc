/*
 * Common body of the t32_awbprN_oracle programs: our t32_awb_proc_* phases 1 .. PR_PHASE (driver/t32/tx_isp_t32_awbproc.h)
 * against the vendor tisp_awb_process (0x3ed34) from Matt Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc).
 * The leaves (long_par_update, spec_calculate, long_alogrithm with the colour temperature detection, set_gain) are the
 * vendor's own recovered functions here, so the whole call tree runs on the vendor side.  Phases 1 and 2 are built with
 * trunc_process.sh (stop at the label that ends the phase, T32O_STOP in the conf); phase 3 runs the unmodified function.
 * Compared: every byte of par, state and hardware shadow, the register writes in order, the completion (count, object),
 * the event pushed (count, channel, type, ct, arg), the frame counter.
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
#include "tx_isp_t32_awbproc.h"

#define LG_PHASE 6
#define T32_LG_GEN_ONLY
#include "t32_awblong_common.inc"

u64 tisp_awb_process(u32 a0, u32 a1, u32 a2, u32 a3);
extern u32 t32_data_e16a4[4];
extern u8 t32_data_e16c4[64];

static unsigned int cases, failed, nrefused, npath[8], nwr, ncompl, nrc1, nwrap;
static u32 regv[3];

static u32 hook(u32 reg)
{
	return reg == 0x16190 ? regv[0] : reg == 0x16194 ? regv[1] : reg == 0x16198 ? regv[2] : 0xdead0000u | reg;
}

static u32 ours_rd(void *p, u32 reg)
{
	(void)p;
	return hook(reg);
}

static u32 regval(void)
{
	switch (rnd() % 5) {
	case 0: return 0;
	case 1: return rnd() & 0xff;
	case 2: return rnd() & 0xffff;
	case 3: return 0x00ffffffu + rnd() % 3;
	default: return rnd();
	}
}

/* the inputs of the leaves outside the long algorithm: spec_calculate flags / registers, set_gain mode and shadow */
static void gen_proc(u8 *par, u8 *stt, u8 *hw, const u8 *sec, u32 tbl)
{
	u32 i, lo, hi, ev;

	gen_long(par, stt, hw, sec, tbl);
	/* the exposure against the two limits of long_par_update: the table choice follows ev < max(lo, hi) */
	lo = g32(par, 32);
	hi = g32(par, 36);
	if (rnd() % 3 == 0) {
		ev = lo < hi ? hi : lo;
		if (tbl == 1264 && ev)
			ev = rnd() % ev;
		else if (tbl == 364)
			ev += rnd() % 4;
		p32(stt, 107588, ev);
	} else if (rnd() % 2) {
		p32(stt, 107588, (tbl == 1264 && (lo < hi ? hi : lo)) ? 0 : (lo < hi ? hi : lo));
	}
	if (rnd() % 4) {
		/* what long_par_update puts into the state is what gen() made for the detection: the same four values in all
		 * three sets (every blend gives them back), the same flags and id */
		u32 kind = g32(stt, 38048), w;

		for (i = 0; i < 12; i++)
			p32(par, 180 + 4 * i, g32(stt, 38032 + 4 * (i & 3)));
		par[5702] = stt[28688];
		par[5703] = stt[28689];
		w = g32(par, 228);
		p32(par, 228, (w & 0xffff0000u) | (kind & 0xffff));
		w = g32(par, 236);
		p32(par, 236, (w & 0xffff0000u) | (kind & 0xffff));
		if (rnd() % 2) {
			p32(par, 232, 1 + rnd() % 3000);
			p32(par, 240, 1 + rnd() % 3000);
		}
	}
	for (i = 5704; i < 5714; i++)
		par[i] = rnd() % 4 == 0 ? (u8)rnd() : (u8)(rnd() % 3);
	stt[28706] = (u8)(rnd() % 4 ? 1 : rnd() % 3);
	stt[28707] = (u8)(rnd() % 4);
	for (i = 0; i < 3; i++)
		regv[i] = regval();
	if (rnd() % 4 == 0)
		regv[1] = 0;
	/* set_gain: the mode word, the frozen byte, the fixed values of the shadow */
	if (rnd() % 3 == 0) {
		stt[107596] = (u8)(rnd() % 3);
		stt[107597] = rnd() % 3 ? 0 : (u8)(rnd() % 3);
		stt[107598] = (u8)(rnd() % 13);
	}
	p16(hw, 4266, rnd() % 3 ? rnd() % 3 : rnd());
	p16(hw, 4268, rnd() % 2 ? rnd() % 2000 : rnd());
	p16(hw, 4270, rnd() % 2 ? rnd() % 2000 : rnd());
	p16(hw, 4272, rnd() % 2 ? 2000 + rnd() % 9000 : rnd());
	p32(par, 44, rnd() % 3 ? 1500 + rnd() % 9000 : rnd());
}

static const char *compare(const u8 *vpar, const u8 *opar, const u8 *vst, const u8 *ost, const u8 *vhw, const u8 *ohw,
			   const struct t32_awb_proc *p, u32 ch, u32 vctx, u32 flag, u32 cnt0, u32 *wi)
{
	unsigned int i, nv = t32o_log.n;

	(void)ch; (void)vctx; (void)flag; (void)cnt0;
	if (nv != p->nw)
		return "write count";
	for (i = 0; i < nv; i++)
		if (t32o_log.w[i].reg != p->w[i].reg || t32o_log.w[i].val != p->w[i].val) {
			*wi = i;
			return "write";
		}
	if (memcmp(vpar, opar, PARB))
		return "par";
	if (memcmp(vst, ost, STB))
		return "state";
	if (memcmp(vhw, ohw, HWB))
		return "hw";
#if PR_PHASE >= 2
	if (t32o_complete_n != (u32)p->complete)
		return "completion count";
	if (p->complete && t32o_complete_arg != vctx + 16)
		return "completion object";
#endif
#if PR_PHASE >= 3
	if (t32o_ev_n != 1 || t32o_ev_ch != ch)
		return "event count / channel";
	if (t32o_ev_w[2] != p->ev.type || t32o_ev_w[4] != p->ev.ct || t32o_ev_w[5] != p->ev.arg)
		return "event";
#endif
	return 0;
}

static int run_case(const char *what, const u8 *sec)
{
	static u8 par[PARB], opar[PARB], stt[STB], ost[STB], hw[HWB], ohw[HWB];
	static struct t32_awb_ctd d;
	static struct t32_awb_proc p;
	u8 *vpar, *vst, *vhw, *vctx, *vtab;
	struct t32_awb a;
	const char *bad = 0;
	u32 i, ch = rnd() % 4, tbl = rnd() % 2 ? 1264 : 364, flag, cnt0, wi = 0;
	u16 ocnt = (u16)0;
	int r;

	gen_proc(par, stt, hw, sec, tbl);
	memcpy(opar, par, PARB);
	memcpy(ost, stt, STB);
	memcpy(ohw, hw, HWB);
	flag = rnd() % 3 ? 1 : rnd() % 3 ? 0 : rnd();
	switch (rnd() % 6) {
	case 0: cnt0 = 99 + rnd() % 3; break;
	case 1: cnt0 = 65535 - rnd() % 2; break;
	case 2: cnt0 = rnd() & 0xffff; break;
	default: cnt0 = rnd() % 101; break;
	}

	t32o_reset();
	t32o_pr_stop = 0;
	t32o_lg_stop = 0;
	t32o_ct_stop = 0;
	t32o_complete_n = t32o_complete_arg = t32o_ev_n = t32o_ev_ch = 0;
	memset(t32o_ev_w, 0, sizeof(t32o_ev_w));
	vctx = t32o_alloc(16);
	vtab = t32o_alloc(256);
	vpar = t32o_alloc(PARB);
	vst = t32o_alloc(STB);
	vhw = t32o_alloc(HWB);
	memcpy(vpar, par, PARB);
	memcpy(vst, stt, STB);
	memcpy(vhw, hw, HWB);
	memset(vctx, 0, 16);
	memset(vtab, 0, 256);
	p32(vctx, 0, T32O_PTR(vpar));
	p32(vctx, 4, T32O_PTR(vst));
	p32(vctx, 12, T32O_PTR(vhw));
	t32_data_e16b4[ch] = T32O_PTR(vctx);
	t32_data_e16a4[ch] = T32O_PTR(vtab);
	memset(t32_data_e16c4, 0, sizeof(t32_data_e16c4));
	p32(t32_data_e16c4, 16 * ch, flag);
	t32_data_e1690[0] = (u8)cnt0;
	t32_data_e1690[1] = (u8)(cnt0 >> 8);
	if (getenv("T32O_TRACE"))
		fprintf(stderr, "case %u: vendor\n", cases + nrefused);
	t32o_rd_hook = hook;
	t32o_call((t32o_fn)(void (*)(void))tisp_awb_process, ch, 0, 0, 0, 0);
	t32o_rd_hook = 0;

	a.par = (struct t32_awb_par *)opar;
	a.st = (struct t32_awb_state *)ost;
	a.hw = (struct t32_awb_hw *)ohw;
	memset(&d, 0xa5, sizeof(d));
	memset(&p, 0xa5, sizeof(p));
	ocnt = (u16)cnt0;
	(void)ocnt;
	if (getenv("T32O_TRACE"))
		fprintf(stderr, "case %u: ours\n", cases + nrefused);
#if PR_PHASE == 1
	r = t32_awb_proc_calc(&a, &d, ours_rd, 0, &p);
#elif PR_PHASE == 2
	r = t32_awb_proc_calc(&a, &d, ours_rd, 0, &p);
	if (!r)
		t32_awb_proc_gain(&a, flag, &p);
#else
	r = t32_awb_process(&a, &d, ours_rd, 0, flag, &ocnt, &p);
#endif
	if (r < 0) {
		nrefused++;
		return 0;
	}
	cases++;
	npath[p.path & 7]++;
	nrc1 += p.rc == 1;
	nwr += p.nw;
#if PR_PHASE >= 2
	ncompl += p.complete;
#endif
	if (PR_PHASE < 3 && !t32o_pr_stop)
		bad = "vendor did not reach the label";
	else if (PR_PHASE >= 3 && t32o_pr_stop)
		bad = "stopped";
	if (!bad)
		bad = compare(vpar, opar, vst, ost, vhw, ohw, &p, ch, T32O_PTR(vctx), flag, cnt0, &wi);
#if PR_PHASE >= 3
	if (!bad) {
		u32 vc = t32_data_e1690[0] | (u32)t32_data_e1690[1] << 8;

		nwrap += vc == 0;
		if (vc != ocnt)
			bad = "frame counter";
	}
#endif
	if (!bad)
		return 0;
	if (failed++ < 5) {
		printf("MISMATCH %s: %s (rows %u cols %u) writes vendor %u ours %u path %d rc %d\n", what, bad, g16(par, 3186), g16(par, 3190),
		       t32o_log.n, p.nw, p.path, p.rc);
		if (!strcmp(bad, "write"))
			printf("  #%u vendor %05x=%08x ours %05x=%08x\n", wi, t32o_log.w[wi].reg, t32o_log.w[wi].val, p.w[wi].reg, p.w[wi].val);
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
	printf("paths: calm %u, p8==0 off/on %u/%u, p8!=0 apply %u, hold %u, smooth %u; long left early %u\n", npath[1], npath[2], npath[3],
	       npath[4], npath[5], npath[6], nrc1);
	printf("t32_awbpr%d_oracle: random %u, IQ bins %d (%u cases), refused %u, %u register writes, completions %u, counter zero %u, %u mismatches\n",
	       PR_PHASE, rc_r, nbins, cases - rc_r, nrefused, nwr, ncompl, nwrap, failed);
	if (failed)
		return 1;
	if (!nbins) {
		printf("t32_awbpr%d_oracle: IQ-bin part: SKIP (set T32_IQ_DIR to a directory of T32 *.bin)\n", PR_PHASE);
		return strict ? 2 : 0;
	}
	return 0;
}
