// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: GIB per-frame paths.  Runs the unmodified vendor
 * functions tisp_gib_dn_params_refresh / tisp_gib_wake_up (Matt Davis'
 * recovered C, PR #15 @ 2e2e06bc) and our t32_gib_{dn_params_refresh,
 * wake_up} from driver/t32/tx_isp_t32_gib.h from the same initial state and
 * compares every register write (address, value, order), the section bytes
 * and the working block.
 *
 * The vendor keeps the Bayer pointer table as state of tisp_gib_init, so each
 * case first runs the reference init (channel 0) with the Bayer pattern, then
 * overwrites the section (at tuning base + 26940), the gain words (ctx +4),
 * the working block (ctx +8) and the held black levels (ctx +16) with random
 * data.  sec[0x220] / sec[0x221] / sec[0x223] and the hold flag get small
 * values so every hold / write back branch of tisp_gib_interp_by_again(_s)
 * is reached.  Bayer 15 is refused by ours and not run.
 */
#define _GNU_SOURCE
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_gib.h"

#define SECB	T32_GIB_SEC_BYTES
#define CPB	T32_GIB_VALUES
#define SEC_OFF	26940u
#define MAXW	T32_GIB_MAX_WRITES

extern u64 tisp_gib_dn_params_refresh(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_gib_wake_up(u32 a0, u32 a1, u32 a2, u32 a3);

static u32 rs = 0x51b3e7u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static void put32(u8 *p, u32 v)
{
	p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
}

static u32 le32(const u8 *p)
{
	return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static unsigned int compared, refused, failed;
static const char *stage = "start";
static u8 *dbg_ctx;

static void segv(int sig, siginfo_t *si, void *uc)
{
	char b[256];
	int n;

	(void)sig;
	(void)uc;
	n = snprintf(b, sizeof(b), "t32_gibf_oracle: SEGV in %s, addr %p, ctx words %08x %08x %08x %08x %08x\n",
		     stage, si->si_addr, dbg_ctx ? *(u32 *)(dbg_ctx + 0) : 0, dbg_ctx ? *(u32 *)(dbg_ctx + 4) : 0,
		     dbg_ctx ? *(u32 *)(dbg_ctx + 8) : 0, dbg_ctx ? *(u32 *)(dbg_ctx + 12) : 0,
		     dbg_ctx ? *(u32 *)(dbg_ctx + 16) : 0);
	if (n > 0)
		(void)!write(2, b, (size_t)n);
	_exit(3);
}

enum { CASE_DN, CASE_WAKE };

/* memory readable and writable? (non-destructive: pipe out, pipe back in) */
static int rw_ok(const void *p, size_t n)
{
	int fd[2], ok = 0;

	if (pipe(fd))
		return 0;
	if (write(fd[1], p, n) == (ssize_t)n && read(fd[0], (void *)p, n) == (ssize_t)n)
		ok = 1;
	close(fd[0]);
	close(fd[1]);
	return ok;
}


static u32 gain_word(void)
{
	return (rnd() % 12) << 16 | (rnd() & 0xffff);
}

static void gen_section(u8 *sec)
{
	static const u16 slx[5] = { 0x114, 0x128, 0x13c, 0x150, 0x164 };
	unsigned int i, k, j;

	for (i = 0; i < SECB; i++)
		sec[i] = (rnd() % 3) ? (u8)rnd() : (u8)(rnd() & 0x3f);
	if (rnd() % 3 == 0)
		put32(sec + 4, 0);
	sec[0x220] = (u8)(rnd() % 4);
	sec[0x221] = (u8)(rnd() % 4);
	sec[0x223] = (u8)(rnd() % 3);
	if (rnd() % 2)
		for (k = 0; k < 5; k++) {
			u8 *p = sec + slx[k];
			unsigned int x = rnd() % 8, y = rnd() % 8;

			for (j = 0; j < 5; j++) {
				x += rnd() % 4 ? 1 + rnd() % 30 : 0;
				y += rnd() % 3 ? rnd() % 200 : 0;
				p[2 * j] = (u8)x; p[2 * j + 1] = 0;
				p[10 + 2 * j] = (u8)y; p[11 + 2 * j] = 0;
			}
		}
}

static int run_case(const char *what, int which, u32 bayer, u32 mode)
{
	struct t32_front_write ow[MAXW];
	struct t32_gib_ctx oc;
	u8 sec[SECB], cp[CPB], rsec[SECB], rcp[CPB], osec[SECB], ocp[CPB];
	u8 hold[T32_GIB_HOLD_BYTES], rhold[T32_GIB_HOLD_BYTES], ohold[T32_GIB_HOLD_BYTES];
	u8 *base, *info, *ctx, *vsec, *vcp, *vgw, *vhold;
	static u8 ogw[8];
	unsigned int mark, ref_n, i;
	int our_n;

	if (bayer == 15) {
		refused++;
		return 0;
	}
	gen_section(sec);
	for (i = 0; i < CPB; i++)
		cp[i] = (u8)rnd();
	put32(ogw, gain_word());
	put32(ogw + 4, gain_word());
	for (i = 0; i < T32_GIB_HOLD_BYTES; i++)
		hold[i] = (u8)rnd();
	hold[0] = (u8)(rnd() % 3);
	hold[1] = rnd() % 4 ? 0 : (u8)rnd();

	t32o_reset();
	base = t32o_alloc(SEC_OFF + SECB + 64);
	info = t32o_alloc(64);
	memcpy(base + SEC_OFF, sec, SECB);
	put32(info + 8, bayer);
	t32_data_dd0a4[0] = T32O_PTR(base);
	stage = "ref init";
	t32o_call((t32o_fn)(void (*)(void))tisp_gib_init, 0, T32O_PTR(info), 0, 0, 0);
	stage = "ctx setup";
	ctx = T32O_HOST(t32_data_e17c0[0]);
	if (!ctx || !rw_ok(ctx, 20)) {
		fprintf(stderr, "t32_gibf_oracle: no vendor context after init\n");
		exit(2);
	}
	vsec = base + SEC_OFF;
	dbg_ctx = ctx;
	put32(ctx, T32O_PTR(vsec));
	memcpy(vsec, sec, SECB);
	/* ctx +4: gain words object, +8: working block (pool objects of init) */
	vgw = T32O_HOST(le32(ctx + 4));
	vcp = T32O_HOST(le32(ctx + 8));
	vhold = T32O_HOST(le32(ctx + 16));
	if (le32(ctx + 4) < 0x10000 || le32(ctx + 8) < 0x10000 || le32(ctx + 16) < 0x10000 || !rw_ok(vgw, 8) ||
	    !rw_ok(vcp, CPB) || !rw_ok(vhold, T32_GIB_HOLD_BYTES)) {
		fprintf(stderr, "t32_gibf_oracle: gain words / working block / hold not found in the vendor context\n");
		exit(2);
	}
	/* the gain words are values (index << 16 | fraction), not pointers */
	memcpy(vgw, ogw, 8);
	memcpy(cp, vcp, 0x40);	/* +0..0x3f: kept from the vendor init (pointers/state the writers use) */
	memcpy(vcp, cp, CPB);
	memcpy(vhold, hold, sizeof(hold));
	mark = t32o_log.n;
	stage = which == CASE_DN ? "ref dn_params_refresh" : "ref wake_up";
	if (which == CASE_DN)
		t32o_call((t32o_fn)(void (*)(void))tisp_gib_dn_params_refresh, 0, mode, 0, 0, 0);
	else
		t32o_call((t32o_fn)(void (*)(void))tisp_gib_wake_up, 0, 0, 0, 0, 0);
	ref_n = t32o_log.n - mark;
	if (ref_n > MAXW) {
		fprintf(stderr, "ref_n %u > MAXW\n", ref_n);
		exit(2);
	}
	memcpy(rsec, vsec, SECB);
	memcpy(rcp, vcp, CPB);
	memcpy(rhold, vhold, sizeof(hold));
	if (memcmp(vgw, ogw, 8)) {
		fprintf(stderr, "t32_gibf_oracle: vendor changed the gain words\n");
		exit(2);
	}

	memcpy(osec, sec, SECB);
	memcpy(ocp, cp, CPB);
	oc.sec = osec;
	oc.cp = ocp;
	oc.gw = ogw;
	memcpy(ohold, hold, sizeof(hold));
	oc.hold = ohold;
	stage = "ours";
	if (which == CASE_DN)
		our_n = t32_gib_dn_params_refresh(&oc, 0, mode, bayer, ow);
	else
		our_n = t32_gib_wake_up(&oc, 0, bayer, ow);

	compared++;
	if (our_n < 0 || (unsigned int)our_n != ref_n)
		goto bad;
	for (i = 0; i < ref_n; i++)
		if (t32o_log.w[mark + i].reg != ow[i].reg || t32o_log.w[mark + i].val != ow[i].val)
			goto bad;
	if (memcmp(rsec, osec, SECB) || memcmp(rcp, ocp, CPB) || memcmp(rhold, ohold, sizeof(hold)))
		goto bad;
	return 0;
bad:
	if (failed++ < 5) {
		printf("MISMATCH %s: bayer %u mode %u writes vendor %u ours %d (220 %u 221 %u 223 %u hold %u sec4 %u)\n", what, bayer, mode, ref_n, our_n, sec[0x220], sec[0x221], sec[0x223], hold[0] | hold[1] << 8, le32(sec + 4));
		for (i = 0; i < ref_n || (int)i < our_n; i++)
			if (i >= ref_n || (int)i >= our_n || t32o_log.w[mark + i].reg != ow[i].reg ||
			    t32o_log.w[mark + i].val != ow[i].val) {
				printf("  #%u vendor %04x=%08x ours %04x=%08x\n", i,
				       i < ref_n ? t32o_log.w[mark + i].reg : 0, i < ref_n ? t32o_log.w[mark + i].val : 0,
				       (int)i < our_n ? ow[i].reg : 0, (int)i < our_n ? ow[i].val : 0);
				if (i > 6)
					break;
			}
		for (i = 0; i < SECB; i++)
			if (rsec[i] != osec[i]) {
				printf("  sec+0x%x vendor %02x ours %02x\n", i, rsec[i], osec[i]);
				break;
			}
		for (i = 0; i < CPB; i++)
			if (rcp[i] != ocp[i]) {
				printf("  cp+0x%x vendor %02x ours %02x\n", i, rcp[i], ocp[i]);
				break;
			}
		for (i = 0; i < sizeof(hold); i++)
			if (rhold[i] != ohold[i]) {
				printf("  hold+0x%x vendor %02x ours %02x\n", i, rhold[i], ohold[i]);
				break;
			}
	}
	return 1;
}

int main(void)
{
	const unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 3000;
	unsigned int c, b;
	static char altstk[65536];
	stack_t ss;
	struct sigaction sa;

	ss.ss_sp = altstk;
	ss.ss_size = sizeof(altstk);
	ss.ss_flags = 0;
	sigaltstack(&ss, NULL);
	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = segv;
	sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
	sigaction(SIGSEGV, &sa, NULL);

	for (c = 0; c < n; c++) {
		u32 bayer = rnd() % 32;
		u32 mode = (rnd() & 1) ? 1 : rnd() % 5;

		run_case("dn-random", CASE_DN, bayer, mode);
		run_case("wake-random", CASE_WAKE, bayer, 0);
	}
	for (b = 0; b < 32; b++) {
		run_case("dn-mode1", CASE_DN, b, 1);
		run_case("dn-mode0", CASE_DN, b, 0);
		run_case("wake", CASE_WAKE, b, 0);
	}
	printf("t32_gibf_oracle: random+boundary %u compared (%u refused by ours), %u mismatches\n",
	       compared, refused, failed);
	return failed ? 1 : 0;
}
