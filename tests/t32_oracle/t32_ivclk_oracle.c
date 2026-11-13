// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our IVDC clock setup (driver/t32/tx_isp_t32_ivdc_clk.h)
 * against the unmodified vendor ivdc_set_clk_parent.isra.8 and
 * ivdc_activate_module (Matt Davis' recovered C, PR #15 @ 2e2e06bc).  A
 * random image (subdev, private data, clock array) runs through ours, is
 * restored and runs through the vendor; compared: every byte, d98b4, the
 * return values and the ordered log of the hook calls (spin lock with flags,
 * clock getters, devm_clk_get/put, clk_get / set_parent / put with the clock
 * names by text, get/set rate, prepare_enable, isp_printf level / format
 * text / arguments).
 * Exit 0 = all equal, 1 = first mismatch.
 */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_ivdc_clk.h"

#define R(f)	((t32o_fn)(void (*)(void))(f))
/* vendor rodata (data.c, decoded) */
char t32_data_c3724[] = "sclka", t32_data_c372c[] = "mpll", t32_data_c3734[] = "vpll";
char t32_data_c5c68[] = "mux_ispa", t32_data_c5c74[] = "mux_vpu";
char t32_data_c5c1c[] = "get %s failed!\n", t32_data_c5c2c[] = "The %s was not found!\n";
char t32_data_c5c44[] = "Failed to modify the parent clock.\n";
u32 t32_data_ce220[3], t32_data_d98b4;

u64 ivdc_set_clk_parent_isra_8(u32, u32, u32, u32);
u64 ivdc_activate_module(u32, u32, u32, u32);

static u32 rng = 0x1f123bb5u;
static u32 rnd(void)
{
	rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
	return rng;
}

#define MAXE 40
static struct { unsigned int n; u32 e[MAXE][6]; } EV[2];
static int side;
static u32 sc[16];	/* per-case script: hook return values */
static unsigned int ngr;
static void ev(u32 a, u32 b, u32 c, u32 d)
{
	unsigned int n = EV[side].n++;

	if (n < MAXE) {
		EV[side].e[n][0] = a; EV[side].e[n][1] = b; EV[side].e[n][2] = c; EV[side].e[n][3] = d;
	}
}
static u32 hs(const char *s)
{
	u32 h = 2166136261u;

	while (*s)
		h = (h ^ (u8)*s++) * 16777619u;
	return h;
}
static const char *str(u32 p, const char *o) { return side ? (const char *)T32O_HOST(p) : o; }

/* candidate parent names handed out by get_clka_name / get_clkv_name */
static char nm[5][8] = { "sclka", "mpll", "vpll", "mpl", "vpllx" };
static u32 h_dget(u32 dev, const char *n) { ev(1, dev, hs(n), 0); return sc[0]; }
static u32 h_dput(u32 dev, u32 clk) { ev(2, dev, clk, 0); return sc[1]; }
static u32 h_get(u32 dev, const char *n) { ev(3, dev, hs(n), 0); return sc[2]; }
static int h_setp(u32 clk, u32 p) { ev(4, clk, p, 0); return (int)sc[3]; }
static void h_put(u32 clk) { ev(5, clk, 0, 0); }
static u32 h_clka(void) { ev(6, 0, 0, 0); return sc[4]; }
static u32 h_clkv(void) { ev(7, 0, 0, 0); return sc[5]; }
static const char *h_aname(void) { ev(8, 0, 0, 0); return nm[sc[6] % 5]; }
static const char *h_vname(void) { ev(9, 0, 0, 0); return nm[sc[7] % 5]; }
static u32 h_rate(u32 clk) { ev(10, clk, 0, 0); return sc[8 + ngr++ % 2]; }
static void h_srate(u32 clk, u32 r) { ev(11, clk, r, 0); }
static void h_en(u32 clk) { ev(12, clk, 0, 0); }
static const struct t32_ivdc_clk_os co = { h_dget, h_dput, h_get, h_setp, h_put, h_clka, h_clkv, h_aname, h_vname,
					   h_rate, h_srate, h_en };
static void h_lock(u8 *l, u32 *f) { ev(13, T32O_PTR(l), 0, 0); *f = sc[10]; }
static void h_unlock(u8 *l, u32 f) { ev(14, T32O_PTR(l), f, 0); }
static const struct t32_ivdc_os ios = { .lock = h_lock, .unlock = h_unlock };

#define A4 u32 a1, u32 a2, u32 a3
#define U3 (void)a1; (void)a2; (void)a3
u64 private_devm_clk_get(u32 d, u32 n, u32 a2, u32 a3) { (void)a2; (void)a3; return h_dget(d, str(n, 0)); }
u64 private_devm_clk_put(u32 d, u32 c, u32 a2, u32 a3) { (void)a2; (void)a3; return h_dput(d, c); }
u64 clk_get(u32 d, u32 n, u32 a2, u32 a3) { (void)a2; (void)a3; return h_get(d, str(n, 0)); }
u64 clk_set_parent(u32 c, u32 p, u32 a2, u32 a3) { (void)a2; (void)a3; return (u32)h_setp(c, p); }
u64 clk_put(u32 c, A4) { U3; h_put(c); return 0; }
u64 get_isp_clka(u32 a0, A4) { (void)a0; U3; return h_clka(); }
u64 get_isp_clkv(u32 a0, A4) { (void)a0; U3; return h_clkv(); }
u64 get_clka_name(u32 a0, A4) { (void)a0; U3; return T32O_PTR(h_aname()); }
u64 get_clkv_name(u32 a0, A4) { (void)a0; U3; return T32O_PTR(h_vname()); }
u64 private_clk_get_rate(u32 c, A4) { U3; return h_rate(c); }
u64 private_clk_set_rate(u32 c, u32 r, u32 a2, u32 a3) { (void)a2; (void)a3; h_srate(c, r); return 0; }
u64 private_clk_prepare_enable(u32 c, A4) { U3; h_en(c); return 0; }
u64 __private_spin_lock_irqsave(u32 l, u32 f, u32 a2, u32 a3) { (void)a2; (void)a3; u32 v; h_lock(T32O_HOST(l), &v); t32_g_wr32(T32O_HOST(f), 0, v); return 0; }
u64 private_spin_unlock_irqrestore(u32 l, u32 f, u32 a2, u32 a3) { (void)a2; (void)a3; h_unlock(T32O_HOST(l), f); return 0; }

int isp_printf(unsigned int level, const char *fmt, ...)
{
	unsigned int n = EV[side].n++, k = 3;
	const char *f;
	va_list ap;

	va_start(ap, fmt);
	if (n < MAXE) {
		EV[side].e[n][0] = 20; EV[side].e[n][1] = level; EV[side].e[n][2] = hs(fmt);
		for (f = fmt; *f && k < 6; f++) {
			if (*f != '%')
				continue;
			f++;
			if (*f == 's') {
				unsigned long p = va_arg(ap, unsigned long);

				EV[side].e[n][k++] = hs(side ? (const char *)T32O_HOST((u32)p) : (const char *)p);
			} else {
				EV[side].e[n][k++] = va_arg(ap, unsigned int);
			}
		}
	}
	va_end(ap);
	return (int)sc[11];
}

#define O_SD	0x0000u
#define O_PRIV	0x0200u
#define O_CLKS	0x0400u		/* 8 words */
#define O_DEV	0x0440u		/* device word of the isra call */
#define IMGSZ	0x0480u
static u8 *img, save0[IMGSZ], save1[IMGSZ];

static int fail(const char *what, unsigned int c, unsigned int op)
{
	printf("t32_ivclk_oracle: FAIL case %u op %u: %s\n", c, op, what);
	return 1;
}

int main(void)
{
	unsigned int c, i, n = 40000, eff[2] = { 0 }, act = 0;
	struct t32_ivdc iv;

	t32_data_ce220[0] = T32O_PTR(t32_data_c3724);
	t32_data_ce220[1] = T32O_PTR(t32_data_c372c);
	t32_data_ce220[2] = T32O_PTR(t32_data_c3734);
	img = t32o_alloc(IMGSZ);
	for (c = 0; c < n; c++) {
		unsigned int op = c % 2;
		u8 *sd = img + O_SD, *priv = img + O_PRIV;
		u32 sdw = T32O_PTR(sd), ro = 0, rr = 0, d0 = rnd(), dref, pn = rnd() % 5, mx = rnd() % 2;

		for (i = 0; i < IMGSZ; i++)
			img[i] = (u8)rnd();
		for (i = 0; i < 16; i++)
			sc[i] = rnd();
		sc[0] = rnd() % 4 == 0 ? 0xfffff001u + rnd() % 0xfff : rnd() % 8 == 0 ? 0xfffff000u : rnd();
		sc[3] = rnd() % 2 ? 0 : rnd();
		if (rnd() % 2)
			sc[8] = sc[4];
		if (rnd() % 2)
			sc[9] = sc[5];
		t32_g_wr32(sd, T32_IVDC_SD_PRIV, rnd() % 8 == 0 ? 0 : rnd() % 8 == 0 ? 0xfffff001u : T32O_PTR(priv));
		t32_g_wr32(sd, T32_IVDC_SD_CLKS, T32O_PTR(img + O_CLKS));
		t32_g_wr32(sd, T32_IVDC_SD_CLK_NUM, rnd() % 9);
		t32_g_wr32(priv, T32_IVDC_PRIV_STATE, rnd() % 4 ? 1 : rnd() % 2 ? 2 : rnd());
		switch (rnd() % 10) {
		case 0: sdw = 0; break;
		case 1: sdw = 0xfffff001u + rnd() % 0xfff; break;
		default: break;
		}
		memset(&iv, 0, sizeof(iv));
		iv.os = &ios;
		iv.d98b4 = d0;
		memcpy(save0, img, IMGSZ);
		for (side = 0; side < 2; side++) {
			memcpy(img, save0, IMGSZ);
			EV[side].n = 0;
			ngr = 0;
			memset(EV[side].e, 0, sizeof(EV[side].e));
			if (!side) {
				if (op == 0)
					ro = t32_ivdc_set_clk_parent(&co, img + O_DEV, nm[pn], mx ? "mux_vpu" : "mux_ispa");
				else
					ro = (u32)t32_ivdc_activate_module(&iv, &co, T32O_HOST(sdw));
				memcpy(save1, img, IMGSZ);
			} else {
				t32_data_d98b4 = d0;
				if (op == 0)
					rr = (u32)t32o_call(R(ivdc_set_clk_parent_isra_8), T32O_PTR(img + O_DEV), T32O_PTR(nm[pn]),
							    T32O_PTR(mx ? t32_data_c5c74 : t32_data_c5c68), 0, 0);
				else
					rr = (u32)t32o_call(R(ivdc_activate_module), sdw, 0, 0, 0, 0);
				dref = t32_data_d98b4;
			}
		}
		if (ro != rr)
			return fail("return value", c, op);
		if (memcmp(img, save1, IMGSZ))
			return fail("memory image", c, op);
		if (iv.d98b4 != dref)
			return fail("d98b4", c, op);
		if (EV[0].n != EV[1].n || memcmp(EV[0].e, EV[1].e, sizeof(EV[0].e)))
			return fail("hook call log", c, op);
		if (memcmp(img, save0, IMGSZ))
			eff[op]++;
		if (op && dref == 1 && d0 != 1)
			act++;
	}
	printf("t32_ivclk_oracle: %u cases, 0 mismatches; changed memory per op: %u %u; activated %u\n", n, eff[0],
	       eff[1], act);
	return 0;
}
