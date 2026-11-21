/*
 * t33_ivdc_timeout_host: the bounded IVDC soft-reset wait (openimp #756, beyond vendor).
 *
 * Drives t32_ivdc_core_ops_init (driver/t32/tx_isp_t32_ivdc.h) with fake OS hooks:
 *  1. normal: the busy bit clears after 3 udelay calls -> 0, state 3, mode word written, no CPM pulse
 *     (same mode), no timeout counted (the vendor path).
 *  2. stuck: the busy bit never clears -> -110 after 2 x 5 rounds x 200 udelay(1000) (2 s), exactly one CPM
 *     reset pulse (the retry), state not 3, no mode word.
 *  3. stuck, then the retry works (bit clears during the retry) -> 0, state 3, 1 timeout counted.
 *  4. forced (force_timeouts = 1, test hook ivdc_test_reset_timeouts): -> first wait times out although the
 *     bit is clear, the retry succeeds -> 0.
 *  5. forced 2: -> -110 (the hook drives the full fallback on a device).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_ivdc.h"

int isp_printf(unsigned int level, const char *fmt, ...) { (void)level; (void)fmt; return 0; }

static u8 *busy;
static u32 nud, clear_at, msleeps;
static volatile u32 cpmw[64];
static u32 pulses;

static void f_lock(u8 *l, u32 *f) { (void)l; *f = 0; }
static void f_unlock(u8 *l, u32 f) { (void)l; (void)f; }
static void f_void(void) {}
static void f_clk(u32 c) { (void)c; }
static void f_msleep(u32 ms) { (void)ms; msleeps++; if (cpmw[T32_CPM_CLKGATE / 4] & T32_CPM_IVDC_RST) pulses++; }
static void f_udelay(u32 us)
{
	(void)us;
	if (++nud == clear_at)
		t32_g_wr32(busy, 0, t32_g_rd32(busy, 0) & ~1u);
}

static const struct t32_ivdc_os os = { .cpm = (volatile u8 *)cpmw, .lock = f_lock, .unlock = f_unlock, .clka = f_void,
				       .clkv = f_void, .clk_disable_unprepare = f_clk, .msleep = f_msleep, .udelay = f_udelay };

static int fails;
static void expect(const char *what, long got, long want)
{
	if (got != want) {
		printf("t33_ivdc_timeout_host: FAIL %s: %ld, expected %ld\n", what, got, want);
		fails++;
	}
}

/* one core start; the busy bit is set by the start write and cleared at udelay call 'at' (0 = never) */
static int run(struct t32_ivdc *d, u8 *img, u32 at, u32 force, u32 *state)
{
	u8 *sd = img, *priv = img + 0x200, *regs = img + 0x400;
	u32 on = 1;
	int r;

	memset(img, 0, 0x800);
	t32_g_wr32(sd, T32_IVDC_SD_PRIV, T32O_PTR(priv));
	t32_g_wr32(priv, T32_IVDC_PRIV_REGS, T32O_PTR(regs));
	t32_g_wr32(priv, T32_IVDC_PRIV_STATE, 2);
	busy = regs;
	t32_g_wr32(regs, 0, 1);		/* the hardware keeps bit 0 set while the reset runs */
	nud = 0; clear_at = at; pulses = 0; msleeps = 0;
	d->force_timeouts = force;
	r = t32_ivdc_core_ops_init(d, sd, &on);
	*state = t32_g_rd32(priv, T32_IVDC_PRIV_STATE);
	return r;
}

int main(void)
{
	struct t32_ivdc d;
	u8 *img = t32o_alloc(0x800);
	u32 st, mode_reg;
	int r;

	memset(&d, 0, sizeof(d));
	d.mode = 1; d.started_mode = 1; d.os = &os;

	r = run(&d, img, 3, 0, &st);			/* 1. normal */
	mode_reg = t32_g_rd32(img + 0x400, T32_IVDC_REG_MODE);
	expect("normal ret", r, 0); expect("normal state", st, 3); expect("normal udelay", nud, 200);
	expect("normal mode word", mode_reg, 0x00041100); expect("normal CPM pulses", pulses, 0); expect("normal timeouts", d.timeouts, 0);

	r = run(&d, img, 0, 0, &st);			/* 2. stuck */
	expect("stuck ret", r, -110); expect("stuck udelay", nud, 2 * T32_IVDC_RESET_ROUNDS * 200);
	expect("stuck CPM pulses", pulses, 1); expect("stuck state", st, 2);
	expect("stuck mode word", t32_g_rd32(img + 0x400, T32_IVDC_REG_MODE), 0); expect("stuck timeouts", d.timeouts, 2);

	d.timeouts = 0;
	r = run(&d, img, T32_IVDC_RESET_ROUNDS * 200 + 150, 0, &st);	/* 3. the retry works */
	expect("retry ret", r, 0); expect("retry state", st, 3); expect("retry CPM pulses", pulses, 1); expect("retry timeouts", d.timeouts, 1);

	d.timeouts = 0;
	r = run(&d, img, T32_IVDC_RESET_ROUNDS * 200 + 3, 1, &st);	/* 4. forced once (the bit clears in the retry) */
	expect("forced1 ret", r, 0); expect("forced1 state", st, 3); expect("forced1 timeouts", d.timeouts, 1);
	expect("forced1 hook used up", d.force_timeouts, 0);

	d.timeouts = 0;
	r = run(&d, img, 3, 2, &st);			/* 5. forced twice */
	expect("forced2 ret", r, -110); expect("forced2 state", st, 2); expect("forced2 timeouts", d.timeouts, 2);

	if (fails)
		return 1;
	printf("t33_ivdc_timeout_host: PASS (normal 0 after 200 udelay, stuck -110 after 2 s and one CPM retry, retry recovers, hook forces 1 / 2)\n");
	return 0;
}
