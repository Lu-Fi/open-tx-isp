/*
 * t33_ivdc_mode_host: /dev/misc-ivdc SET_MODE and the IVDC start read one valid mode (openimp #780).
 *
 *  1. t32_ivdc_set_mode_checked (driver/t32/tx_isp_t32_ivdc_misc.h): 0 / 1 are stored, 2..4 and 0xffffffff
 *     become 0 and are reported, a failed copy (bytes left) keeps the old mode, and the live mode never holds
 *     the raw user word (the fake copy_from_user checks that it does not write into d->mode).
 *     #783: while a stream runs (live) a different mode returns -EBUSY and is not stored, the same one is OK.
 *  2. t32_ivdc_core_ops_init (tx_isp_t32_ivdc.h) reads d->mode once: a store of 4 during the reset wait
 *     (a racing SET_MODE before #780) does not change the mode word (0x00041100) nor started_mode (1).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_ivdc.h"
#include "tx_isp_t32_ivdc_misc.h"

int isp_printf(unsigned int level, const char *fmt, ...) { (void)level; (void)fmt; return 0; }

static struct t32_ivdc d;
static u32 user_word, user_left, nud, race_at, race_val;
static u8 *busy;

static u32 f_cfu(u8 *dst, u32 user, u32 n)
{
	(void)user;
	if (dst == (u8 *)&d.mode)
		return n;	/* the raw word must not land in the live mode */
	memcpy(dst, &user_word, n - user_left);
	return user_left;
}
static u32 f_ctu(u32 user, const u8 *src, u32 n) { (void)user; (void)src; (void)n; return 0; }
static const struct t32_ivdc_uos uo = { .copy_from_user = f_cfu, .copy_to_user = f_ctu };

static void f_lock(u8 *l, u32 *f) { (void)l; *f = 0; }
static void f_unlock(u8 *l, u32 f) { (void)l; (void)f; }
static void f_void(void) {}
static void f_clk(u32 c) { (void)c; }
static void f_msleep(u32 ms) { (void)ms; }
static void f_udelay(u32 us)
{
	(void)us;
	if (++nud == race_at)
		d.mode = race_val;	/* the racing store */
	if (nud == 3)
		t32_g_wr32(busy, 0, t32_g_rd32(busy, 0) & ~1u);
}
static volatile u32 cpmw[64];
static const struct t32_ivdc_os os = { .cpm = (volatile u8 *)cpmw, .lock = f_lock, .unlock = f_unlock, .clka = f_void,
				       .clkv = f_void, .clk_disable_unprepare = f_clk, .msleep = f_msleep, .udelay = f_udelay };

static int fails;
static void expect(const char *what, long got, long want)
{
	if (got != want) {
		printf("t33_ivdc_mode_host: FAIL %s: %ld, expected %ld\n", what, got, want);
		fails++;
	}
}

static long set_live(u32 old, u32 word, u32 left, u32 *bad, int live)
{
	d.mode = old; user_word = word; user_left = left;
	return t32_ivdc_set_mode_checked(&d, &uo, 0x1000, bad, live);
}
#define set(o, w, l, b) set_live(o, w, l, b, 0)

int main(void)
{
	static const u32 vals[] = { 0, 1, 2, 3, 4, 0xffffffffu };
	u8 *img = t32o_alloc(0x800), *sd = img, *priv = img + 0x200, *regs = img + 0x400;
	u32 bad, on = 1, i;
	long r;

	for (i = 0; i < sizeof(vals) / sizeof(vals[0]); i++) {
		r = set(1, vals[i], 0, &bad);
		expect("set ret", r, 0);
		expect("set mode", d.mode, vals[i] <= 1 ? vals[i] : 0);
		expect("set bad", bad, vals[i] <= 1 ? 0 : vals[i]);
	}
	r = set(1, 0, 2, &bad);				/* copy fault after 2 bytes */
	expect("fault ret", r, 2); expect("fault keeps mode", d.mode, 1); expect("fault bad", bad, 0);

	/* #783: a stream runs: a different mode is refused with -EBUSY and not stored, the same mode is fine */
	r = set_live(1, 0, 0, &bad, 1); expect("live 1->0 ret", r, -16); expect("live 1->0 mode", d.mode, 1);
	r = set_live(1, 3, 0, &bad, 1); expect("live 1->3 ret", r, -16); expect("live 1->3 mode", d.mode, 1);
	r = set_live(1, 1, 0, &bad, 1); expect("live 1->1 ret", r, 0); expect("live 1->1 mode", d.mode, 1);
	r = set_live(0, 4, 0, &bad, 1); expect("live 0->4(0) ret", r, 0); expect("live 0->4 mode", d.mode, 0);
	r = set_live(0, 1, 0, &bad, 1); expect("live 0->1 ret", r, -16); expect("live 0->1 mode", d.mode, 0);

	memset(&d, 0, sizeof(d));
	d.mode = 1; d.started_mode = 1; d.os = &os;
	memset(img, 0, 0x800);
	t32_g_wr32(sd, T32_IVDC_SD_PRIV, T32O_PTR(priv));
	t32_g_wr32(priv, T32_IVDC_PRIV_REGS, T32O_PTR(regs));
	t32_g_wr32(priv, T32_IVDC_PRIV_STATE, 2);
	busy = regs;
	t32_g_wr32(regs, 0, 1);
	nud = 0; race_at = 1; race_val = 4;
	r = t32_ivdc_core_ops_init(&d, sd, &on);
	expect("start ret", r, 0);
	expect("start mode word", t32_g_rd32(regs, T32_IVDC_REG_MODE), 0x00041100);
	expect("start started_mode", d.started_mode, 1);

	if (fails)
		return 1;
	printf("t33_ivdc_mode_host: PASS (SET_MODE validated before publish, -EBUSY for a mode change while streaming, copy fault keeps the mode, start reads the mode once)\n");
	return 0;
}
